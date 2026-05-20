#ifndef RELAY_DRIVER_H
#define RELAY_DRIVER_H

#include <main.h>


// Pin 74HC595
#define RELAY_DS_PIN    25  // Dữ liệu
#define RELAY_SHCP_PIN  26  // Clock
#define RELAY_STCP_PIN  27  // Latch

void relay_driver_init(void);
void relay_driver_update(uint8_t bitmask);          // Cập nhật toàn bộ relay 1–6
void relay_driver_control(uint8_t relay_id, bool state);  // Điều khiển từng relay (1–6)

#endif
