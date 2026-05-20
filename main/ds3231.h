#ifndef __DS3231_H__
#define __DS3231_H__

#include "driver/i2c.h"
#include "esp_err.h"
#include "main.h"
#include "esp_sntp.h"

#include "lora_uart.h"


#define DS3231_I2C_ADDRESS   0x68

typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day_of_week; // 1-7
    uint8_t day;
    uint8_t month;
    uint16_t year;
} ds3231_time_t;

extern ds3231_time_t rtc_time;

esp_err_t ds3231_init(SemaphoreHandle_t i2c_mutex);
esp_err_t ds3231_get_time(ds3231_time_t *time);
esp_err_t ds3231_set_time(const ds3231_time_t *time);
void rtc_task(void *pvParameters);
float ds3231_get_temperature(void);
void sync_time_from_ntp(void);

extern float temp_ds3231;

extern SemaphoreHandle_t i2c_mutex;
#endif // __DS3231_H__
