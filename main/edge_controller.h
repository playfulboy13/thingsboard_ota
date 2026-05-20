#ifndef _EDGE_CONTROLLER_H
#define _EDGE_CONTROLLER_H

#include "main.h"
#include <math.h>
#include "lora_uart.h"

#define PRESS_HISTORY_SIZE 30   // 30 mẫu ~ 60s (2s/task)

extern float pressHistory[PRESS_HISTORY_SIZE];
extern uint32_t timeHistory[PRESS_HISTORY_SIZE];

extern int pressIndex;
extern int pressFilled;

int isNodeOnline(int idx);
float safePositive(float v);
float calcAvgPressureOnline(void);
float calcHeightDiff(float pNode, float pRef);
float calcRainProb(float p, float t, float h);
float calcET(float tempC, float lightVal, float humidity, int hour);
const char* calcSoilStatus(float moisture, float ec);
void updatePressureHistory(float p, uint32_t nowSec);
float calcPressureTrendTimed(uint32_t nowSec, uint32_t windowSec);


#endif