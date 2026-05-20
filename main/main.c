#include "main.h"


/* ================ CONFIG ================= */
const char *TAG = "W5500";

void app_main(void)
{
    gpio_init_config();
    configure_uart1();
    ESP_ERROR_CHECK(nvs_flash_init());
    g_current_mode = load_network_mode_from_nvs();
    if (g_current_mode == NET_MODE_WIFI)
    {
        wifi_init();
    }
    else
    {
        w5500_init();
    }

    ota_get_version(g_current_fw_version,
                    sizeof(g_current_fw_version));

    ESP_LOGI(TAG,
             "Current FW Version: %s",
             g_current_fw_version);

    if(strcmp(g_current_fw_version, "0.0") == 0)
    {
        ota_save_version("1.0");
    }

    xTaskCreate(uart_receive_task,"uart_receive_task",8192,NULL,5,NULL);
    xTaskCreate(Task1,"Task1",4096,NULL,5,NULL);
    xTaskCreate(TaskLed,"TaskLed",4096,NULL,5,NULL);
   // xTaskCreate(uart_test_tx_task,"uart_test_tx",4096,NULL,5,NULL);
   // xTaskCreate(TaskNetwork,"TaskNetwork",4096,NULL,5,NULL);
   xTaskCreate(TaskButtonRuntime, "TaskButton", 4096, NULL, 5, NULL);
   //xTaskCreate(TaskTest,"Test",4096,NULL,1,NULL);


    /* Delay test firmware stability */
    vTaskDelay(pdMS_TO_TICKS(10000));

    if(mqtt_connected)
    {
        ESP_LOGI(TAG,
                 "Firmware verified OK");

        esp_ota_mark_app_valid_cancel_rollback();
    }
    else
    {
        ESP_LOGE(TAG,
                 "Firmware invalid -> rollback");

        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

void TaskButtonRuntime(void *pvParameters)
{
    const TickType_t debounce_time = pdMS_TO_TICKS(40);
    const TickType_t poll_time     = pdMS_TO_TICKS(10);

    int stable_state = 1;          // trạng thái đã xác nhận
    int last_read    = 1;          // trạng thái đọc gần nhất
    TickType_t last_change_time = 0;

    while (1)
    {
        int current = gpio_get_level(RESET_BTN_GPIO);
        TickType_t now = xTaskGetTickCount();

        // Nếu có thay đổi mức đọc được
        if (current != last_read)
        {
            last_change_time = now;   // reset timer debounce
            last_read = current;
        }

        // Nếu mức giữ ổn định đủ lâu
        if ((now - last_change_time) > debounce_time)
        {
            // Nếu khác với trạng thái ổn định trước đó
            if (stable_state != current)
            {
                stable_state = current;

                // Chỉ xử lý khi nhấn xuống (active low)
                if (stable_state == 0)
                {
                    ESP_LOGI(TAG, "Button short press detected");

                    // Toggle mode
                    if (g_current_mode == NET_MODE_WIFI)
                        g_current_mode = NET_MODE_ETHERNET;
                    else
                        g_current_mode = NET_MODE_WIFI;

                    save_network_mode_to_nvs(g_current_mode);

                    ESP_LOGI(TAG, "Restarting to apply new mode...");
                    vTaskDelay(pdMS_TO_TICKS(300));
                    esp_restart();
                }
            }
        }

        vTaskDelay(poll_time);
    }
}
