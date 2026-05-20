#ifndef _MAIN_H
#define _MAIN_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"

#include "mqtt_client.h"
#include "mqtt.h"

#include "w5500_lan.h"
#include "lora_uart.h"
#include "ds3231.h"
#include "user_task.h"
#include "ota.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_spi_flash.h"
#include "esp_heap_caps.h"
#include "edge_controller.h"

extern const char* TAG;



void TaskNetwork(void *pvParameters);
void TaskButtonRuntime(void *pvParameters);
#endif
