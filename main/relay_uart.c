#include "relay_uart.h"

static bool last_state_relay[4] = {false, false, false, false};  // Relay 7–10

// ===== Biến toàn cục lưu trạng thái PIC =====
bool g_relay_state[4] = {false, false, false, false};  // Relay A-D
uint8_t g_triac_level = 0;     // 1..5
float g_vsepic = 0.0f;         // giá trị điện áp tính ra
uint64_t g_last_heartbeat_us = 0; // timestamp lần cuối nhận ALIVE

// ================== INIT ==================
void relay_uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// ================== TX ==================
void relay_uart_send(char command) {
    uart_write_bytes(UART_PORT, &command, 1);
}

// Điều khiển relay 7–10: relay_id = 1..4 (tương ứng A,B,C,D)
void relay_uart_control(uint8_t relay_id, bool state) {
    if (relay_id < 1 || relay_id > 4) return;

    uint8_t idx = relay_id - 1;
    if (state != last_state_relay[idx]) {
        last_state_relay[idx] = state;
        char cmd = 0;

        switch (relay_id) {
            case 1: cmd = state ? 'A' : 'a'; break;
            case 2: cmd = state ? 'B' : 'b'; break;
            case 3: cmd = state ? 'C' : 'c'; break;
            case 4: cmd = state ? 'D' : 'd'; break;
        }
        relay_uart_send(cmd);
    }
}

// Điều khiển TRIAC: giá trị 1–5 (gửi ký tự '1'..'5')
void relay_uart_set_triac(uint8_t level) {
    if (level >= 1 && level <= 5) {
        char cmd = '0' + level;
        relay_uart_send(cmd);
    }
}

// Gửi heartbeat liên tục
void relay_uart_heartbeat_task(void *pvParameters) {
    while (1) {
        relay_uart_send('H');  // Gửi ký tự heartbeat
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ================== RX ==================
static void relay_uart_handle_line(char *line) {
    // loại bỏ \r\n cuối chuỗi
    size_t len = strlen(line);
    while (len && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = 0;
    if (len == 0) return;

    ESP_LOGI(TAG, "PIC -> %s", line);

    // Relay state
    if (strncmp(line, "Relay ", 6) == 0 && len >= 10) {
        char r = line[6]; // 'A'..'D'
        bool on = strstr(line, "ON") != NULL;
        switch (r) {
            case 'A': g_relay_state[0] = on; break;
            case 'B': g_relay_state[1] = on; break;
            case 'C': g_relay_state[2] = on; break;
            case 'D': g_relay_state[3] = on; break;
        }
        return;
    }

    // Triac
    if (strncmp(line, "Triac Power=", 12) == 0) {
        g_triac_level = (uint8_t)atoi(line + 12);
        return;
    }

    // Alive
    if (strcmp(line, "ALIVE") == 0) {
        g_last_heartbeat_us = esp_timer_get_time();
        return;
    }

    // Điện áp
    if (strncmp(line, "V:", 2) == 0) {
        g_vsepic = (float)strtod(line + 2, NULL);
        return;
    }

    // OK hoặc Unknown -> bỏ qua
}

void relay_uart_read_task(void *pvParameters) {
    uint8_t buf[128];
    char linebuf[256];
    size_t line_len = 0;

    while (1) {
        int len = uart_read_bytes(UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(1000));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)buf[i];
                if (c == '\n' || c == '\r') {
                    if (line_len > 0) {
                        linebuf[line_len] = 0;
                        relay_uart_handle_line(linebuf);
                        line_len = 0;
                    }
                } else {
                    if (line_len < sizeof(linebuf) - 1) {
                        linebuf[line_len++] = c;
                    } else {
                        line_len = 0; // tràn -> reset
                    }
                }
            }
        }
    }
}
