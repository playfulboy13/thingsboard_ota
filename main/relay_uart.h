#ifndef RELAY_UART_H
#define RELAY_UART_H

#include "main.h"


#define UART_PORT UART_NUM_1
#define UART_TX_PIN 17
#define UART_RX_PIN 16
#define UART_BAUDRATE 9600

void relay_uart_init(void);
void relay_uart_send(char command);

// Điều khiển relay 7–10 (qua UART)
void relay_uart_control(uint8_t relay_id, bool state);

// Điều khiển TRIAC (giá trị 1–5)
void relay_uart_set_triac(uint8_t level);

// Task gửi heartbeat mỗi giây
void relay_uart_heartbeat_task(void *pvParameters);
void relay_uart_read_task(void *pvParameters);

typedef struct {
    bool relay[4];          // Relay A–D
    uint8_t triac_level;    // 1..5
    float vsepic;           // điện áp
    uint64_t last_alive_us; // timestamp ALIVE
} pic_status_t;

// Biến toàn cục
extern pic_status_t g_pic_status;

extern float g_vsepic;         // giá trị điện áp tính ra

#endif
