#include "oled_ssd1306.h"

SSD1306_t oled_dev;
SemaphoreHandle_t i2c_mutex = NULL;

void oled_init(void) {
    // Tạo mutex
    i2c_mutex = xSemaphoreCreateMutex();

    // Init OLED (thư viện tự tạo bus I2C)
    i2c_master_init(&oled_dev, I2C_SDA_GPIO, I2C_SCL_GPIO, -1);  // -1 nếu không dùng RESET
    ssd1306_init(&oled_dev, OLED_WIDTH, OLED_HEIGHT);

    ssd1306_clear_screen(&oled_dev, false);
    ssd1306_contrast(&oled_dev, 0xFF);
    ssd1306_display_text(&oled_dev, 0, "ESP32 OLED READY", 16, false);
}

void oled_task(void *pvParameters) {
    int x = 0;
    int dir = 1;
    char mqtt_line[20];
    char rssi_line[20];
    char anim_line[20];
    bool mqtt_status = false;

    while (1) {
        // Đọc RSSI WiFi
        int rssi = 0;
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi = ap_info.rssi;
        }

        mqtt_status = mqtt_connected;  // Biến toàn cục bạn cập nhật nơi khác
        snprintf(rssi_line, sizeof(rssi_line), "RSSI: %ddBm", rssi);
        snprintf(mqtt_line, sizeof(mqtt_line), "MQTT: %s", mqtt_status ? "CONNECTED" : "OFFLINE");

        // Vẽ bóng chạy dòng dưới
        memset(anim_line, ' ', sizeof(anim_line));
        anim_line[19] = '\0';
        anim_line[x] = 0xFF;  // block

        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100))) {
            // Dòng vàng đầu tiên: MQTT trạng thái, dùng font lớn
            ssd1306_display_text_x3(&oled_dev, 0, mqtt_line, strlen(mqtt_line), false);

            // Dòng vàng thứ 2: RSSI
            ssd1306_display_text(&oled_dev, 2, rssi_line, strlen(rssi_line), false);

            // Hiệu ứng phía dưới (dòng xanh)
            ssd1306_display_text(&oled_dev, 6, anim_line, strlen(anim_line), false);

            xSemaphoreGive(i2c_mutex);
        }

        // Đổi hướng nếu đến biên
        x += dir;
        if (x >= 18) dir = -1;
        if (x <= 0)  dir = 1;

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
