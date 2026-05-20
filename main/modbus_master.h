#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include "main.h"
#include <stdint.h>

#define RS485_TX_PIN    14
#define RS485_RX_PIN    35
#define RS485_RE_DE_PIN 32

void modbus_master_init(void);
void modbus_master_task(void *pvParameters);

typedef struct {
    float temp1, temp2, v_bat, num, s1, s2, humidity, t_dh, lux;
    float aht_hum, aht_temp, moisture_capacitive, bme280_pressure;
    float dht11_data0, dht11_data1;
} SensorData;

extern SensorData sensorData;

#endif // MODBUS_MASTER_H
