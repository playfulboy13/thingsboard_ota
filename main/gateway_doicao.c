#include "main.h"

const char *TAG="HOMEKIT_NHA";

void app_main(void)
{
   ESP_ERROR_CHECK(nvs_flash_init());

    ota_get_version(g_current_fw_version,
                    sizeof(g_current_fw_version));

    ESP_LOGI(TAG,
             "Current FW Version: %s",
             g_current_fw_version);

    if(strcmp(g_current_fw_version, "0.0") == 0)
    {
        ota_save_version("1.0");
    }

   
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL, 0);
    modbus_master_init();
    relay_driver_init();
    relay_uart_init();
    oled_init();

    wifi_init();
   
    xTaskCreate(Task1,"Task1",4096,NULL,5,NULL);
    //xTaskCreate(mqtt_publish_message_task,"mqtt_publish_message_task",4096,NULL,5,NULL);
   // xTaskCreate(relay_task, "relay_task", 2048, NULL, 5, NULL);
    xTaskCreate(relay_uart_heartbeat_task, "heartbeat_task", 2048, NULL, 5, NULL);  
    xTaskCreate(oled_task, "oled_ds1307_task", 4096, NULL, 5, NULL);
    xTaskCreate(relay_uart_read_task, "relay_uart_read_task", 4096, NULL,5, NULL);
    xTaskCreate(uart0_task, "uart0_task", 4096, NULL, 5, NULL);

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