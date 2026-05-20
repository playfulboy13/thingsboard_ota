#ifndef __OLED_SSD1306_H__
#define __OLED_SSD1306_H__



#include "main.h"

// GPIO cấu hình
#define I2C_SDA_GPIO   21
#define I2C_SCL_GPIO   22
#define OLED_WIDTH     128
#define OLED_HEIGHT    64

// Biến toàn cục OLED
extern SSD1306_t oled_dev;
extern SemaphoreHandle_t i2c_mutex;

// Prototype
void oled_init(void);
void oled_task(void *pvParameters);

#endif
