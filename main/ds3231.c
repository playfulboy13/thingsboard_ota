#include "ds3231.h"
#include <string.h>
#include "esp_log.h"

SemaphoreHandle_t i2c_mutex;

ds3231_time_t rtc_time = {0}; // Biến toàn cục lưu thời gian

#define NTP_SYNC_INTERVAL_SEC (24 * 60 * 60) // 1 ngày
static time_t last_ntp_sync_time = 0;

static uint8_t bcd2bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

static uint8_t bin2bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

// Không khởi tạo I2C nữa, chỉ lưu lại mutex để sử dụng khi truy cập
esp_err_t ds3231_init(SemaphoreHandle_t mutex) {
    i2c_mutex = mutex;
    if (i2c_mutex == NULL) {
        ESP_LOGE(TAG, "I2C mutex is NULL");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ds3231_get_time(ds3231_time_t *time) {
    if (i2c_mutex == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;

    uint8_t data[7];
    uint8_t reg = 0x00;
    esp_err_t err;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDRESS << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 6, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &data[6], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    xSemaphoreGive(i2c_mutex);

    if (err != ESP_OK) return err;

    time->sec = bcd2bin(data[0]);
    time->min = bcd2bin(data[1]);
    time->hour = bcd2bin(data[2] & 0x3F);
    time->day_of_week = bcd2bin(data[3]);
    time->day = bcd2bin(data[4]);
    time->month = bcd2bin(data[5] & 0x1F);
    time->year = 2000 + bcd2bin(data[6]);

    memcpy(&rtc_time, time, sizeof(ds3231_time_t));
    return ESP_OK;
}

esp_err_t ds3231_set_time(const ds3231_time_t *time) {
    if (i2c_mutex == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;

    uint8_t data[8];
    data[0] = 0x00;
    data[1] = bin2bcd(time->sec);
    data[2] = bin2bcd(time->min);
    data[3] = bin2bcd(time->hour);
    data[4] = bin2bcd(time->day_of_week);
    data[5] = bin2bcd(time->day);
    data[6] = bin2bcd(time->month);
    data[7] = bin2bcd(time->year % 100);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 8, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    xSemaphoreGive(i2c_mutex);

    return err;
}

float ds3231_get_temperature(void) {
    if (i2c_mutex == NULL) return -999.0;
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return -999.0;

    uint8_t data[2];
    esp_err_t err;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x11, true);  // nhiệt độ bắt đầu tại 0x11
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDRESS << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 1, I2C_MASTER_ACK);        // MSB
    i2c_master_read_byte(cmd, &data[1], I2C_MASTER_NACK); // LSB
    i2c_master_stop(cmd);

    err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    xSemaphoreGive(i2c_mutex);

    if (err != ESP_OK) return -999.0;

    int8_t msb = (int8_t)data[0]; // signed
    float lsb = (data[1] >> 6) * 0.25f;

    return msb + lsb;
}

float temp_ds3231=0.0;

void sync_time_from_ntp(void)
{
    // Kiểm tra SNTP đã chạy chưa, chỉ init nếu chưa
    static bool sntp_inited = false;
    if (!sntp_inited) {
        ESP_LOGI(TAG, "Khởi tạo SNTP lần đầu...");
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_init();
        sntp_inited = true;
    } else {
        ESP_LOGI(TAG, "SNTP đã chạy, bỏ qua init");
    }

    // Chờ NTP đồng bộ
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2022 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Đang chờ đồng bộ NTP... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year < (2022 - 1900)) {
        ESP_LOGW(TAG, "Đồng bộ thời gian NTP thất bại!");
        return;
    }

    // Cập nhật múi giờ GMT+7
    setenv("TZ", "ICT-7", 1);
    tzset();

    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "NTP đồng bộ: %02d:%02d:%02d %02d/%02d/%04d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);

    // Cập nhật vào DS3231
    ds3231_time_t rtc_set = {
        .sec = timeinfo.tm_sec,
        .min = timeinfo.tm_min,
        .hour = timeinfo.tm_hour,
        .day_of_week = timeinfo.tm_wday == 0 ? 7 : timeinfo.tm_wday,
        .day = timeinfo.tm_mday,
        .month = timeinfo.tm_mon + 1,
        .year = timeinfo.tm_year + 1900
    };
    ds3231_set_time(&rtc_set);
    ESP_LOGI(TAG, "Đã cập nhật giờ vào DS3231.");

    last_ntp_sync_time = now;
}

void rtc_task(void *pvParameters)
{
    ds3231_time_t now;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 21,
        .scl_io_num = 22,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);

    i2c_mutex = xSemaphoreCreateMutex();
    ds3231_init(i2c_mutex);

    /* 🔹 Đồng bộ NTP lần đầu */
    sync_time_from_ntp();

    /* 🔹 Đọc lại DS3231 & gửi time sang STM32 */
    if (ds3231_get_time(&now) == ESP_OK) {
        uart_send_time();   // <<< GỬI NGAY SAU NTP
        ESP_LOGI(TAG, "Sent time sync to STM32 after NTP");
    }

    while (1) {
        if (ds3231_get_time(&now) == ESP_OK) {
            temp_ds3231 = ds3231_get_temperature();
            // ESP_LOGI(TAG,
            //     "Time: %02d:%02d:%02d %02d/%02d/%04d | Temp: %.2f°C",
            //     now.hour, now.min, now.sec,
            //     now.day, now.month, now.year,
            //     temp_ds3231);
        } else {
            ESP_LOGE(TAG, "Failed to read time");
        }

        /* 🔹 Đồng bộ lại NTP mỗi 24h */
        time_t current_time;
        time(&current_time);

        if (difftime(current_time, last_ntp_sync_time) > NTP_SYNC_INTERVAL_SEC) {
            ESP_LOGI(TAG, "Đã quá 24h - sync NTP lại...");

            sync_time_from_ntp();

            /* 🔹 Sau khi sync → gửi lại cho STM32 */
            if (ds3231_get_time(&now) == ESP_OK) {
                uart_send_time();
                ESP_LOGI(TAG, "Sent time sync to STM32 after NTP re-sync");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
