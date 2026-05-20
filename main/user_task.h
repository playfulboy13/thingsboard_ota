#ifndef _USER_TASK
#define _USER_TASK
#include "main.h"
#include "relay_driver.h"

#include "mqtt.h"


void Task1(void *pvParameters);
void relay_task(void *pvParameters);
void uart0_task(void *pvParameters);
#define BUF_SIZE 256

extern char uart0_buffer[BUF_SIZE];
extern bool uart0_data_ready;  // cờ báo có dữ liệu mới


#endif