#include "modbus_master.h"
#include <math.h>



#define TAG "MODBUS_MASTER"
#define UART_PORT_NUM UART_NUM_2
#define BUF_SIZE 256

// ======= Biến toàn cục và cấu trúc dữ liệu tích hợp tại đây =======


SensorData sensorData;
static float altitude = 0.0f;
static float p0 = 101325.0f;

// Hàm in dữ liệu (bạn có thể sửa thêm theo ý muốn)
static void printSensorData(void) {
    printf("Temp1: %.2f | Temp2: %.2f | VBAT: %.2f | Humidity: %.2f | Lux: %.2f\n",
           sensorData.temp1, sensorData.temp2, sensorData.v_bat, sensorData.humidity, sensorData.lux);
}
// ================================================================

uint16_t calculateCRC(uint8_t *buf, int len) {
    uint16_t crc = 0xFFFF;
    for (int pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)buf[pos]; 
        for (int i = 0; i != 8; i++) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void sendModbusRequest() {
    uint8_t request[8] = {
        0x01, 0x03, 0x00, 0x00, 0x00, 30
    };
    uint16_t crc = calculateCRC(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;

    gpio_set_level(RS485_RE_DE_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    uart_write_bytes(UART_PORT_NUM, (const char *)request, 8);
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(10));
    gpio_set_level(RS485_RE_DE_PIN, 0);
}

void readModbusResponse() {
    uint8_t response[100];
    int index = 0;
    int timeout_ms = 300;
    int elapsed = 0;

   // ESP_LOGI(TAG, "Waiting for response...");

    while (elapsed < timeout_ms && index < sizeof(response)) 
    {
        int len = uart_read_bytes(UART_PORT_NUM, &response[index], 1, pdMS_TO_TICKS(10));
        if (len > 0) 
        {
         //   ESP_LOGI(TAG, "Byte %d: %02X", index, response[index]);
            index++;
        } else
        {
            elapsed += 10;
        }
    }

    if (index < 5) 
    {
        ESP_LOGW(TAG, "Response too short");
        return;
    }

    int offset = (response[0] == 0x00) ? 1 : 0;
    if (response[offset] != 0x01 || response[offset + 1] != 0x03) 
    {
        ESP_LOGW(TAG, "Invalid header: %02X %02X", response[offset], response[offset + 1]);
        return;
    }

    int byteCount = response[offset + 2];
    if (byteCount != 60) 
    {
        ESP_LOGW(TAG, "Unexpected byte count: %d", byteCount);
        return;
    }

    float floats[15];
    for (int i = 0; i < 15; i++) {
        int pos = offset + 3 + (i * 4);
        if (pos + 3 >= index) {
            ESP_LOGW(TAG, "Not enough data for float[%d]", i);
            break;
        }

        uint32_t raw = (uint32_t)response[pos] |
                       ((uint32_t)response[pos + 1] << 8) |
                       ((uint32_t)response[pos + 2] << 16) |
                       ((uint32_t)response[pos + 3] << 24);
        memcpy(&floats[i], &raw, sizeof(float));
    }

    // Gán giá trị
    sensorData.temp1               = floats[0];
    sensorData.temp2               = floats[1];
    sensorData.v_bat               = floats[2];
    sensorData.num                 = floats[3];
    sensorData.s1                  = floats[4];
    sensorData.s2                  = floats[5];
    sensorData.humidity            = floats[6];
    sensorData.t_dh                = floats[7];
    sensorData.lux                 = floats[8];
    sensorData.aht_hum             = floats[9];
    sensorData.aht_temp            = floats[10];
    sensorData.moisture_capacitive = floats[11];
    sensorData.bme280_pressure     = floats[12];
    sensorData.dht11_data0         = floats[13];
    sensorData.dht11_data1         = floats[14];

    if (sensorData.bme280_pressure > 0.0f) {
        altitude = 44330.0f * (1.0f - powf(sensorData.bme280_pressure / p0, 0.1903f));
    }

    //printSensorData();
}




void modbus_master_task(void *pvParameters) {
    static uint32_t lastReq = 0, lastResp = 0;

    while (1) {
        uint32_t now = xTaskGetTickCount();

        if ((now - lastReq) * portTICK_PERIOD_MS >= 500) {
            sendModbusRequest();
            lastReq = now;
        }

        if ((now - lastResp) * portTICK_PERIOD_MS >= 1000) {
            readModbusResponse();
            lastResp = now;
        }
        /*
        // Sau khi gán dữ liệu vào sensorData, in toàn bộ như sau:
ESP_LOGI(TAG, "------------- Sensor Data -------------");
ESP_LOGI(TAG, "Temp1: %.2f | Temp2: %.2f | VBAT: %.2f | Humidity: %.2f", 
         sensorData.temp1, sensorData.temp2, sensorData.v_bat, sensorData.humidity);
ESP_LOGI(TAG, "S1: %.2f | S2: %.2f | Num: %.2f | T_DH: %.2f", 
         sensorData.s1, sensorData.s2, sensorData.num, sensorData.t_dh);
ESP_LOGI(TAG, "Lux: %.2f | AHT_Hum: %.2f | AHT_Temp: %.2f", 
         sensorData.lux, sensorData.aht_hum, sensorData.aht_temp);
ESP_LOGI(TAG, "Moisture: %.2f | BME280_Pressure: %.2f", 
         sensorData.moisture_capacitive, sensorData.bme280_pressure);
ESP_LOGI(TAG, "DHT11 Data0: %.2f | DHT11 Data1: %.2f", 
         sensorData.dht11_data0, sensorData.dht11_data1);
ESP_LOGI(TAG, "----------------------------------------");*/
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

void modbus_master_init() {
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, RS485_TX_PIN, RS485_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    gpio_set_direction(RS485_RE_DE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RS485_RE_DE_PIN, 0);

    xTaskCreate(modbus_master_task, "modbus_task", 4096, NULL, 5, NULL);
}
