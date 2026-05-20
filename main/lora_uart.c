#include "lora_uart.h"
#include "esp_log.h"
#include <string.h>


/* ===== GLOBAL ===== */
node_info_t node_data[NODE_COUNT] = {0};
lora_info_t lora_info = {0};

uint8_t relay_state_mask = 0;

char ack[NRF_ACK_MAX_LEN + 1];
char nrf_ack_str[NRF_ACK_MAX_LEN + 1] = {0};

/* 🔥 chuyển sang static → không dùng stack */
static uint8_t buf[UART_RX_BUF_SIZE];
static uint8_t payload[MAX_PAYLOAD_SIZE];

/* ===== UART INIT ===== */
void configure_uart1(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_ERROR_CHECK(uart_param_config(UART1_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART1_PORT, UART1_TX_PIN, UART1_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(uart_driver_install(UART1_PORT, 2048, 2048, 0, NULL, 0));

    ESP_LOGI(TAG, "UART1 initialized");
}

/* ===== CRC ===== */
uint16_t crc16_modbus(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for(uint16_t i=0;i<len;i++){
        crc ^= buf[i];
        for(uint8_t j=0;j<8;j++){
            crc = (crc & 1) ? (crc>>1)^0xA001 : (crc>>1);
        }
    }
    return crc;
}

void uart_receive_task(void *arg)
{
    while (1)
    {
        int len = uart_read_bytes(UART1_PORT, buf, sizeof(buf), pdMS_TO_TICKS(1000));
        if (len <= 0) continue;

        for (int i = 0; i < len - 6; i++)
        {
            /* ===== HEADER ===== */
            if (buf[i] != HEADER1 || buf[i+1] != HEADER2)
                continue;

            if (i + 4 >= len) continue;

            uint16_t plen = buf[i+2] | (buf[i+3] << 8);

            if (plen == 0 || plen > MAX_PAYLOAD_SIZE) {
                ESP_LOGW(TAG, "Invalid plen=%d", plen);
                continue;
            }

            uint16_t frame_len = 4 + plen + 2;

            if (frame_len > UART_RX_BUF_SIZE) continue;
            if (i + frame_len > len) continue;

            /* ===== CRC ===== */
            uint16_t crc_recv = buf[i+4+plen] | (buf[i+5+plen] << 8);
            uint16_t crc_calc = crc16_modbus(&buf[i], 4+plen);

            if (crc_recv != crc_calc) {
                ESP_LOGW(TAG, "CRC FAIL");
                continue;
            }

            /* ===== DECRYPT ===== */
            for (int k=0; k<plen; k++)
                payload[k] = buf[i+4+k] ^ XOR_KEY;

            int offset = 0;

            /* ===== NODE ===== */
            for (int n=0; n<NODE_COUNT; n++)
            {
                if (offset + 4 > plen) break;

                node_data[n].cnt = payload[offset++];
                node_data[n].rssi = (int8_t)payload[offset++];
                node_data[n].snr = (int8_t)payload[offset++];
                node_data[n].online = payload[offset++];

                if (offset + 12 > plen) break;

                memcpy(&node_data[n].rtt_ms, &payload[offset], 4); offset += 4;
                memcpy(&node_data[n].link_speed_real, &payload[offset], 4); offset += 4;
                memcpy(&node_data[n].link_speed_phy, &payload[offset], 4); offset += 4;

                for (int s=0; s<SENSOR_COUNT; s++)
                {
                    if (offset + 4 > plen) break;
                    memcpy(&node_data[n].sensor[s], &payload[offset], 4);
                    offset += 4;
                }

                node_data[n].updated = true;
            }

            /* ===== FAKE ===== */
            for (int f=0; f<FAKE_SENSOR_COUNT; f++)
            {
                if (offset + 4 > plen) break;

                memcpy(&lora_info.fake_sensor[f], &payload[offset], 4);
                offset += 4;
            }

            /* ===== LORA INFO ===== */
            if (offset + 2 <= plen)
            {
                lora_info.lost_rate = payload[offset++];
                lora_info.total_packets = payload[offset++];
            }

            /* ===== ACK ===== */
            if (offset + 1 <= plen)
            {
                uint8_t ack_len = payload[offset++];

                if (ack_len > 0 &&
                    ack_len < NRF_ACK_MAX_LEN &&
                    offset + ack_len <= plen)
                {
                    memcpy(nrf_ack_str, &payload[offset], ack_len);
                    nrf_ack_str[ack_len] = 0;
                }
            }

            // /* ===================================================== */
            // /* ===================== LOG DATA ======================= */
            // /* ===================================================== */

            // ESP_LOGI(TAG, "\n========== FRAME ==========");
            // ESP_LOGI(TAG, "plen: %d | frame_len: %d", plen, frame_len);

            // /* ===== NODE LOG ===== */
            // for (int n = 0; n < NODE_COUNT; n++)
            // {
            //     if (!node_data[n].updated) continue;

            //     float rtt  = *(float*)&node_data[n].rtt_ms;
            //     float real = *(float*)&node_data[n].link_speed_real;
            //     float phy  = *(float*)&node_data[n].link_speed_phy;

            //     ESP_LOGI(TAG, "\n-- NODE %d --", n);
            //     ESP_LOGI(TAG, "cnt=%d | rssi=%d | snr=%d | online=%d",
            //              node_data[n].cnt,
            //              node_data[n].rssi,
            //              node_data[n].snr,
            //              node_data[n].online);

            //     ESP_LOGI(TAG, "rtt=%.2f ms | real=%.2f | phy=%.2f",
            //              rtt, real, phy);

            //     /* sensor */
            //     char line[256];
            //     int l = 0;

            //     l += sprintf(line + l, "sensor: ");

            //     for (int s = 0; s < SENSOR_COUNT; s++)
            //     {
            //         l += sprintf(line + l, "%.2f ", node_data[n].sensor[s]);

            //         if ((s + 1) % 4 == 0)
            //         {
            //             ESP_LOGI(TAG, "%s", line);
            //             l = 0;
            //             line[0] = 0;
            //         }
            //     }

            //     if (l > 0)
            //         ESP_LOGI(TAG, "%s", line);

            //     node_data[n].updated = false;
            // }

            // /* ===== FAKE LOG ===== */
            // ESP_LOGI(TAG, "\n-- FAKE SENSOR --");

            // char line[256];
            // int l = 0;

            // for (int f = 0; f < FAKE_SENSOR_COUNT; f++)
            // {
            //     l += sprintf(line + l, "%.2f ", lora_info.fake_sensor[f]);

            //     if ((f + 1) % 6 == 0)
            //     {
            //         ESP_LOGI(TAG, "%s", line);
            //         l = 0;
            //         line[0] = 0;
            //     }
            // }

            // if (l > 0)
            //     ESP_LOGI(TAG, "%s", line);

            // /* ===== LORA ===== */
            // ESP_LOGI(TAG, "\n-- LORA --");
            // ESP_LOGI(TAG, "lost_rate=%d%% | total_packets=%d",
            //          lora_info.lost_rate,
            //          lora_info.total_packets);

            // /* ===== ACK ===== */
            // ESP_LOGI(TAG, "\n-- ACK --");
            // ESP_LOGI(TAG, "ack: %s", nrf_ack_str);

            // ESP_LOGI(TAG, "===========================\n");

            /* skip frame */
            i += frame_len - 1;
        }
    }
}

/* ===== SEND ===== */
void uart_send_relay(uint8_t relay, uint8_t state)
{
    uint8_t frame[5] = {
        0xAA, 0x01, relay, state,
        0xAA ^ 0x01 ^ relay ^ state
    };
    uart_write_bytes(UART1_PORT, (const char*)frame, 5);
}

void uart_send_time(void)
{
    uint8_t frame[10];

    frame[0]=0xAA;
    frame[1]=0x03;

    frame[2]=rtc_time.hour;
    frame[3]=rtc_time.min;
    frame[4]=rtc_time.sec;
    frame[5]=rtc_time.day_of_week;
    frame[6]=rtc_time.day;
    frame[7]=rtc_time.month;
    frame[8]=(uint8_t)(rtc_time.year%100);

    uint8_t crc=0;
    for(int i=0;i<9;i++) crc ^= frame[i];

    frame[9]=crc;

    uart_write_bytes(UART1_PORT,(const char*)frame,10);
}

/* ===== TEST ===== */
void uart_test_tx_task(void *arg)
{
    while(1)
    {
        uart_send_relay(0,1);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}