#ifndef _MAIN_H
#define _MAIN_H
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "ssd1306.h"

#include <stdbool.h>
#include "user_task.h"
#include "modbus_master.h"
#include "oled_ssd1306.h"

#include "relay_driver.h"
#include "relay_uart.h"
#include "driver/i2c_master.h"

#include "esp_timer.h"  

#include <math.h>

#include "mqtt.h"
#include "ota.h"

extern const char *TAG;

#endif