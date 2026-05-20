#include "user_task.h"


char uart0_buffer[BUF_SIZE];
bool uart0_data_ready = false;  // cờ báo có dữ liệu mới

void Task1(void *pvParameters)
{
    while (1)
    {
        /* =========================================
           WIFI LED -> RELAY 6
        ========================================= */

        if (wifi_connected == true)
        {
            relay_driver_control(6, true);
            vTaskDelay(pdMS_TO_TICKS(50));

            relay_driver_control(6, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else
        {
            relay_driver_control(6, true);
            vTaskDelay(pdMS_TO_TICKS(500));

            relay_driver_control(6, false);
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        /* =========================================
           MQTT LED -> RELAY 5
        ========================================= */

        if (mqtt_connected == true)
        {
            relay_driver_control(5, true);
            vTaskDelay(pdMS_TO_TICKS(300));

            relay_driver_control(5, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else
        {
            relay_driver_control(5, true);
            vTaskDelay(pdMS_TO_TICKS(100));

            relay_driver_control(5, false);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}
    

static uint8_t relay_state_bitmask = 0x00;

static bool lastV20 = false, lastV21 = false, lastV22 = false, lastV23 = false;

void relay_task(void *pvParameters) {
  
    while (1) {
       
       

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void uart0_task(void *pvParameters)
{
    uint8_t data[BUF_SIZE];
    int index = 0;

    while (1) {
        int len = uart_read_bytes(UART_NUM_0, data, BUF_SIZE - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                if (data[i] == '\n' || index >= BUF_SIZE - 1) {
                    uart0_buffer[index] = '\0';  // kết thúc chuỗi
                    uart0_data_ready = true;     // báo có dữ liệu mới
                    ESP_LOGI(TAG, "UART0 Received: %s", uart0_buffer);
                    index = 0; // reset buffer
                } else {
                    uart0_buffer[index++] = data[i];
                }
            }
        }
    }
}