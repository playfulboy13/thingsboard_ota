#ifndef LORA_UART_H
#define LORA_UART_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

#define UART1_PORT UART_NUM_1
#define UART1_TX_PIN 17
#define UART1_RX_PIN 16

#define XOR_KEY 0x5A
#define HEADER1 0xAA
#define HEADER2 0x55

#define NODE_COUNT        6
#define SENSOR_COUNT      8
#define FAKE_SENSOR_COUNT 30

#define MAX_PAYLOAD_SIZE  768   // 🔥 giới hạn cứng chống overflow
#define UART_RX_BUF_SIZE  1024

#define NRF_ACK_MAX_LEN   32

typedef struct {
    float sensor[SENSOR_COUNT];
    uint8_t cnt;
    int8_t rssi;
    int8_t snr;
    uint8_t online;
    uint32_t rtt_ms;
    float link_speed_real;
    float link_speed_phy;
    bool updated;
} node_info_t;

typedef struct {
    float fake_sensor[FAKE_SENSOR_COUNT];
    uint8_t lost_rate;
    uint16_t total_packets;
} lora_info_t;

extern node_info_t node_data[NODE_COUNT];
extern lora_info_t lora_info;

extern uint8_t relay_state_mask;

extern char ack[NRF_ACK_MAX_LEN + 1];
extern char nrf_ack_str[NRF_ACK_MAX_LEN + 1];

void configure_uart1(void);
void uart_receive_task(void *arg);
uint16_t crc16_modbus(const uint8_t *buf, uint16_t len);
void uart_send_relay(uint8_t relay, uint8_t state);
void uart_send_time(void);
void uart_test_tx_task(void *arg);

#endif