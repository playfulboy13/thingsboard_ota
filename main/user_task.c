#include "user_task.h"


uint8_t output_state=0x00;

// Node test
const uint8_t test_nodes[] = {0x01, 0x02, 0x03};
const uint8_t test_states[] = {0x01, 0x02, 0x04, 0x08}; // LED/relay giả lập

void gpio_init_config(void)
{
    gpio_reset_pin(DS_PIN);
    gpio_set_direction(DS_PIN,GPIO_MODE_OUTPUT);
    gpio_set_level(DS_PIN,0);
    
    gpio_reset_pin(SH_CP_PIN);
    gpio_set_direction(SH_CP_PIN,GPIO_MODE_OUTPUT);
    gpio_set_level(SH_CP_PIN,0);

    gpio_reset_pin(ST_CP_PIN);
    gpio_set_direction(ST_CP_PIN,GPIO_MODE_OUTPUT);
    gpio_set_level(ST_CP_PIN,0);

    output_state=0x00;
    xuat_1_byte(output_state);
}

void xuat_1_byte(uint8_t data)
{
    for(int i=7;i>=0;i--)
    {
        DS((~data>>i)&0x01);
        XUNG_DICH()
    }
    XUNG_CHOT()
}

void led1_on(void)
{
    output_state|=(1<<LED1_BIT);
    xuat_1_byte(output_state);
}
void led1_off(void)
{
    output_state&=~(1<<LED1_BIT);
    xuat_1_byte(output_state);
}

void led2_on(void)
{
    output_state|=(1<<LED2_BIT);
    xuat_1_byte(output_state);
}
void led2_off(void)
{
    output_state&=~(1<<LED2_BIT);
    xuat_1_byte(output_state);
}

void led3_on(void)
{
    output_state|=(1<<LED3_BIT);
    xuat_1_byte(output_state);
}
void led3_off(void)
{
    output_state&=~(1<<LED3_BIT);
    xuat_1_byte(output_state);
}

// Hàm gửi lệnh relay qua UART
void send_relay_cmd(uint8_t node_id, uint8_t state)
{
    uint8_t frame[4];
    frame[0] = 0xAA;      // START
    frame[1] = node_id;   // Node ID
    frame[2] = state;     // Relay state
    frame[3] = 0x55;      // END

    // Gửi qua UART
    uart_write_bytes(UART_PORT, (const char*)frame, 4);

    // In log
   // printf("[ESP32] Sent: Node %02X, State 0x%02X\r\n", node_id, state);
}

// Task tổng quát quét node và relay
void Task1(void *pvParameters)
{
    while (1)
    {
        if (g_current_mode == NET_MODE_WIFI)
        {
            // WiFi → sáng liên tục
            led3_on();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else
        {
            // Ethernet → nháy
            led3_on();
            vTaskDelay(pdMS_TO_TICKS(200));
            led3_off();
            vTaskDelay(pdMS_TO_TICKS(800));
        }
    }
}



void TaskLed(void *pvParameters)
{
    while (1)
    {
        if (wifi_connected == true)
        {
            // --- LED1: báo WiFi ---
            led1_on();
            vTaskDelay(pdMS_TO_TICKS(50));
            led1_off();
            vTaskDelay(pdMS_TO_TICKS(1000));

            // --- LED2: báo MQTT ---
            if (mqtt_connected == true)
            {
                led2_on();
                vTaskDelay(pdMS_TO_TICKS(300));
                led2_off();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            else
            {
                // MQTT chưa kết nối -> LED2 nháy chậm khác kiểu
                led2_on();
                vTaskDelay(pdMS_TO_TICKS(100));
                led2_off();
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
        else
        {
            // WiFi chưa kết nối -> LED1 nháy đều
            led1_on();
           
            vTaskDelay(pdMS_TO_TICKS(500));
            led1_off();
           
            vTaskDelay(pdMS_TO_TICKS(500));

            // Khi WiFi chưa có, MQTT chắc chắn cũng chưa dùng được
             // --- LED2: báo MQTT ---
            if (mqtt_connected == true)
            {
                led2_on();
                vTaskDelay(pdMS_TO_TICKS(300));
                led2_off();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }

}

void TaskTest(void *pvParameters)
{
    while(1)
    {
        ESP_LOGI(TAG,"OTA VER 1.2\r\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}