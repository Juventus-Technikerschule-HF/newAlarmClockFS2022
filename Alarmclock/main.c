/*
 * main.c
 *
 * Created: 08.03.2022 21:35:00
 * Author : ...
 */ 

#include "avr_compiler.h"
#include "pmic_driver.h"
#include "TC_driver.h"
#include "clksys_driver.h"
#include "sleepConfig.h"
#include "port_driver.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "stack_macros.h"

#include "mem_check.h"

#include "init.h"
#include "utils.h"
#include "errorHandler.h"
#include "NHD0420Driver.h"
#include "ButtonHandler.h"


void vLedBlink(void *pvParameters);
void vTime(void *pvParameters);
void vButtonTask(void *pvParameters);
void vInterfaceTask(void *pvParameters);


//Eventgroup and Defines for Alarmclock
#define ALARMCLOCK_COUNT1SECOND			1<<0
#define ALARMCLOCK_ALARM_ON				1<<1
#define ALARMCLOCK_BUTTON1_SHORT		1 << 2
#define ALARMCLOCK_BUTTON1_LONG			1 << 3
#define ALARMCLOCK_BUTTON2_SHORT		1 << 4
#define ALARMCLOCK_BUTTON2_LONG			1 << 5
#define ALARMCLOCK_BUTTON3_SHORT		1 << 6
#define ALARMCLOCK_BUTTON3_LONG			1 << 7
#define ALARMCLOCK_BUTTON4_SHORT		1 << 8
#define ALARMCLOCK_BUTTON4_LONG			1 << 9
#define ALARMCLOCK_BUTTON_ALL			0x03FC
EventGroupHandle_t egAlarmClock;

#define POS_HOURS	0
#define POS_MINUTES 1
#define POS_SECONDS 2

#define VALUETYPE_CLOCK	0
#define VALUETYPE_ALARM 1

#define HOURS			0
#define MINUTES			1
#define SECONDS			2

static struct {
	int8_t seconds;
	int8_t minutes;
	int8_t hours;
} globalTimeStorage;
static struct {
	int8_t seconds;
	int8_t minutes;
	int8_t hours;
} globalAlarmStorage;

TaskHandle_t testTaskHandle;

typedef enum {
	MENU_MAINSCREEN,
	MENU_SETCLOCK,
	MENU_SETALARM
}menuMode_t;

void drawTime(void);
void drawAlarm(void);
void drawPointer(int line, int pos);
void changeValue(int valueType, int timeType, int8_t value);
bool checkIfAlarm();

int main(void)
{
    resetReason_t reason = getResetReason();

	vInitClock();
	vInitDisplay();
	
	egAlarmClock = xEventGroupCreate();
	
	xTaskCreate(vLedBlink, (const char *) "ledBlink", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	xTaskCreate(vTime, (const char *) "timeTask", configMINIMAL_STACK_SIZE+10, NULL, 3, NULL);
	xTaskCreate(vInterfaceTask, (const char *) "uiTask", configMINIMAL_STACK_SIZE + 10, NULL, 2, NULL);
	xTaskCreate(vButtonTask, (const char *) "buttonTask", configMINIMAL_STACK_SIZE+10, NULL, 3, NULL);
	
	vTaskStartScheduler();
	return 0;
}

void vInterfaceTask(void *pvParameters) {
	(void) pvParameters;
	static menuMode_t menuMode = MENU_MAINSCREEN;
	static uint8_t selector = POS_HOURS;
	static uint8_t alarmState = 0;
	uint16_t buttonstate = 0x0000;
	PORTE.DIRSET = 0x08;
	while(egAlarmClock == NULL) {
		vTaskDelay(1);
	}
	for(;;) {
		PORTE.OUTCLR = 0x08;
		buttonstate = xEventGroupGetBits(egAlarmClock);
		vDisplayClear();
		vDisplayWriteStringAtPos(0,0, "Alarm-Clock 1.0");
		switch(menuMode) {
			case MENU_MAINSCREEN:
			drawTime();
			drawAlarm();
			if(alarmState == 0) {
				vDisplayWriteStringAtPos(3,0,"ON - ___ - ALR - CLK");
				} else {
				vDisplayWriteStringAtPos(3,0,"__ - OFF - ALR - CLK");
			}
			if(buttonstate & ALARMCLOCK_BUTTON4_LONG) {
				menuMode = MENU_SETCLOCK;
				xEventGroupClearBits(egAlarmClock, ALARMCLOCK_ALARM_ON);
				selector = 0;
			}
			if(buttonstate & ALARMCLOCK_BUTTON3_LONG) {
				menuMode = MENU_SETALARM;
				xEventGroupClearBits(egAlarmClock, ALARMCLOCK_ALARM_ON);
				selector = 0;
			}
			if(buttonstate & ALARMCLOCK_BUTTON1_LONG) {
				alarmState = 1;
				xEventGroupClearBits(egAlarmClock, ALARMCLOCK_ALARM_ON);
			}
			if(buttonstate & ALARMCLOCK_BUTTON2_LONG) {
				alarmState = 0;
				xEventGroupClearBits(egAlarmClock, ALARMCLOCK_ALARM_ON);
			}
			if(alarmState == 1) {
				vDisplayWriteStringAtPos(2,0,"ON>");
			}
			break;
			case MENU_SETCLOCK:
			drawTime();
			drawPointer(2,selector);
			vDisplayWriteStringAtPos(3,0,"UP - DWN - NEX - BAK");
			if(buttonstate & ALARMCLOCK_BUTTON1_SHORT) {
				changeValue(VALUETYPE_CLOCK, selector, 1);
			}
			if(buttonstate & ALARMCLOCK_BUTTON2_SHORT) {
				changeValue(VALUETYPE_CLOCK, selector, -1);
			}
			if(buttonstate & ALARMCLOCK_BUTTON3_SHORT) {
				selector++;
				if(selector > POS_SECONDS) {
					selector = POS_HOURS;
				}
			}
			if(buttonstate & ALARMCLOCK_BUTTON4_LONG) {
				menuMode = MENU_MAINSCREEN;
			}
			break;
			case MENU_SETALARM:
			drawAlarm();
			drawPointer(1,selector);
			vDisplayWriteStringAtPos(3,0,"UP - DWN - NEX - BAK");
			if(buttonstate & ALARMCLOCK_BUTTON1_SHORT) {
				changeValue(VALUETYPE_ALARM, selector, 1);
			}
			if(buttonstate & ALARMCLOCK_BUTTON2_SHORT) {
				changeValue(VALUETYPE_ALARM, selector, -1);
			}
			if(buttonstate & ALARMCLOCK_BUTTON3_SHORT) {
				selector++;
				if(selector > POS_SECONDS) {
					selector = POS_HOURS;
				}
			}
			if(buttonstate & ALARMCLOCK_BUTTON4_LONG) {
				menuMode = MENU_MAINSCREEN;
			}
			break;
		}
		if((checkIfAlarm()) && (alarmState == 1)) {
			xEventGroupSetBits(egAlarmClock, ALARMCLOCK_ALARM_ON);
		}
		xEventGroupClearBits(egAlarmClock, ALARMCLOCK_BUTTON_ALL);
		PORTE.OUTSET = 0x08;
		vTaskDelay(200 / portTICK_RATE_MS);
	}
}

void vButtonTask(void * pvParameters) {
	initButtons();
	for(;;) {
		updateButtons();
		if(getButtonPress(BUTTON1) == SHORT_PRESSED) {
			xEventGroupSetBits(egAlarmClock, ALARMCLOCK_BUTTON1_SHORT);
		}
		if(getButtonPress(BUTTON2) == SHORT_PRESSED) {
			xEventGroupSetBits(egAlarmClock, ALARMCLOCK_BUTTON2_SHORT);
		}
		if(getButtonPress(BUTTON3) == SHORT_PRESSED) {
			xEventGroupSetBits(egAlarmClock, ALARMCLOCK_BUTTON3_SHORT);
		}
		if(getButtonPress(BUTTON4) == SHORT_PRESSED) {
			xEventGroupSetBits(egAlarmClock, ALARMCLOCK_BUTTON4_SHORT);
		}
		if(getButtonPress(BUTTON1) == LONG_PRESSED) {
			xEventGroupSetBits(egAlarmClock, ALARMCLOCK_BUTTON1_LONG);
		}
		if(getButtonPress(BUTTON2) == LONG_PRESSED) {
			xEventGroupSetBits(egAlarmClock, ALARMCLOCK_BUTTON2_LONG);
		}
		if(getButtonPress(BUTTON3) == LONG_PRESSED) {
			xEventGroupSetBits(egAlarmClock, ALARMCLOCK_BUTTON3_LONG);
		}
		if(getButtonPress(BUTTON4) == LONG_PRESSED) {
			xEventGroupSetBits(egAlarmClock, ALARMCLOCK_BUTTON4_LONG);
		}
		vTaskDelay(10/portTICK_RATE_MS);
	}
}

void vInitTimer() {
	TCD0.CTRLA = TC_CLKSEL_DIV1024_gc ;
	TCD0.CTRLB = 0x00;
	TCD0.INTCTRLA = 0x03;
	TCD0.PER = 31250-1;
}

void vTime(void *pvParameters) {
	vInitTimer();
	for(;;) {
		globalTimeStorage.hours = 18;
		globalTimeStorage.minutes = 15;
		globalTimeStorage.seconds = 0;
		globalAlarmStorage.hours = 18;
		globalAlarmStorage.minutes = 16;
		globalAlarmStorage.seconds = 0;
		for(;;) {
			xEventGroupWaitBits(egAlarmClock, ALARMCLOCK_COUNT1SECOND, pdTRUE, pdFALSE, portMAX_DELAY);
			globalTimeStorage.seconds++;
			if(globalTimeStorage.seconds>=60) {
				globalTimeStorage.minutes++;
				globalTimeStorage.seconds = 0;
			}
			if(globalTimeStorage.minutes >= 60) {
				globalTimeStorage.hours++;
				globalTimeStorage.minutes = 0;
			}
			if(globalTimeStorage.hours >= 24) {
				globalTimeStorage.hours = 0;
			}
			vTaskDelay(150 / portTICK_RATE_MS);
		}
	}
}

ISR(TCD0_OVF_vect) 
{	
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	xEventGroupSetBitsFromISR(egAlarmClock, ALARMCLOCK_COUNT1SECOND,&xHigherPriorityTaskWoken );
}

void vLedBlink(void *pvParameters) {
	PORTF.DIRSET = 0x0F;
	PORTF.OUT = 0x0F;
	while(egAlarmClock == NULL) {
		vTaskDelay(1);
	}
	for(;;) {
		xEventGroupWaitBits(egAlarmClock, ALARMCLOCK_ALARM_ON, pdFALSE, pdFALSE, portMAX_DELAY);
		PORTF.OUTCLR = 0x0F;
		vTaskDelay(30 / portTICK_RATE_MS);
		PORTF.OUTSET = 0x0F;
		vTaskDelay(100 / portTICK_RATE_MS);
	}
}



void drawTime(void) {
	vDisplayWriteStringAtPos(1,3,"Time:    00:00:00");
	if(globalTimeStorage.hours > 9) {
		vDisplayWriteStringAtPos(1,12,"%d", globalTimeStorage.hours);
		} else {
		vDisplayWriteStringAtPos(1,13,"%d", globalTimeStorage.hours);
	}
	if(globalTimeStorage.minutes > 9) {
		vDisplayWriteStringAtPos(1,15,"%d", globalTimeStorage.minutes);
		} else {
		vDisplayWriteStringAtPos(1,16,"%d", globalTimeStorage.minutes);
	}
	if(globalTimeStorage.seconds > 9) {
		vDisplayWriteStringAtPos(1,18,"%d", globalTimeStorage.seconds);
		} else {
		vDisplayWriteStringAtPos(1,19,"%d", globalTimeStorage.seconds);
	}
}
void drawAlarm(void) {
	vDisplayWriteStringAtPos(2,3,"Alarm:   00:00:00");
	if(globalAlarmStorage.hours > 9) {
		vDisplayWriteStringAtPos(2,12,"%d", globalAlarmStorage.hours);
		} else {
		vDisplayWriteStringAtPos(2,13,"%d", globalAlarmStorage.hours);
	}
	if(globalAlarmStorage.minutes > 9) {
		vDisplayWriteStringAtPos(2,15,"%d", globalAlarmStorage.minutes);
		} else {
		vDisplayWriteStringAtPos(2,16,"%d", globalAlarmStorage.minutes);
	}
	if(globalAlarmStorage.seconds > 9) {
		vDisplayWriteStringAtPos(2,18,"%d", globalAlarmStorage.seconds);
		} else {
		vDisplayWriteStringAtPos(2,19,"%d", globalAlarmStorage.seconds);
	}
}



void drawPointer(int line, int pos) {
	if(line == 1) {
		vDisplayWriteStringAtPos(1,12+pos*3,"vv");
		} else if(line == 2) {
		vDisplayWriteStringAtPos(2,12+pos*3,"^^");
	}
}



void changeValue(int valueType, int timeType, int8_t value) {
	if(valueType == VALUETYPE_CLOCK) {
		switch(timeType) {
			case HOURS:
			globalTimeStorage.hours+=value;
			if(globalTimeStorage.hours < 0) globalTimeStorage.hours = 23;
			if(globalTimeStorage.hours > 23) globalTimeStorage.hours = 0;
			break;
			case MINUTES:
			globalTimeStorage.minutes+=value;
			if(globalTimeStorage.minutes < 0) globalTimeStorage.minutes = 59;
			if(globalTimeStorage.minutes > 59) globalTimeStorage.minutes = 0;
			break;
			case SECONDS:
			globalTimeStorage.seconds+=value;
			if(globalTimeStorage.seconds < 0) globalTimeStorage.seconds = 59;
			if(globalTimeStorage.seconds > 59) globalTimeStorage.seconds = 0;
			break;
		}
		} else if(valueType == VALUETYPE_ALARM) {
		switch(timeType) {
			case HOURS:
			globalAlarmStorage.hours+=value;
			if(globalAlarmStorage.hours < 0) globalAlarmStorage.hours = 23;
			if(globalAlarmStorage.hours > 23) globalAlarmStorage.hours = 0;
			break;
			case MINUTES:
			globalAlarmStorage.minutes+=value;
			if(globalAlarmStorage.minutes < 0) globalAlarmStorage.minutes = 59;
			if(globalAlarmStorage.minutes > 59) globalAlarmStorage.minutes = 0;
			break;
			case SECONDS:
			globalAlarmStorage.seconds+=value;
			if(globalAlarmStorage.seconds < 0) globalAlarmStorage.seconds = 59;
			if(globalAlarmStorage.seconds > 59) globalAlarmStorage.seconds = 0;
			break;
		}
	}
}

bool checkIfAlarm() {
	if((globalAlarmStorage.hours == globalTimeStorage.hours) &&
	(globalAlarmStorage.minutes == globalTimeStorage.minutes) &&
	(globalAlarmStorage.seconds == globalTimeStorage.seconds)) {
		return true;
		} else {
		return false;
	}
}