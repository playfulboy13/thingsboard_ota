#include "relay_driver.h"


// Lưu trạng thái 6 relay (bit 0 → relay1, bit 5 → relay6)
static uint8_t relay_state_bitmask = 0x00;

// Ghi bit ra 74HC595
static void shiftOutCustom(uint8_t data) {
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(RELAY_DS_PIN, (data >> i) & 0x01);
        gpio_set_level(RELAY_SHCP_PIN, 1);
        gpio_set_level(RELAY_SHCP_PIN, 0);
    }
}

// Cập nhật dữ liệu ra 74HC595
void relay_driver_update(uint8_t bitmask) {
    relay_state_bitmask = bitmask & 0x3F;  // Giữ 6 bit thấp (relay1–6)

    gpio_set_level(RELAY_STCP_PIN, 0);
    shiftOutCustom(0x00);                  // Byte đầu tiên (không dùng)
    shiftOutCustom(relay_state_bitmask);   // Byte điều khiển relay
    gpio_set_level(RELAY_STCP_PIN, 1);
}

// Điều khiển từng relay 1–6
void relay_driver_control(uint8_t relay_id, bool state) {
    if (relay_id < 1 || relay_id > 6) return;

    uint8_t mask = (1 << (relay_id - 1));
    if (state)
        relay_state_bitmask |= mask;   // Bật relay
    else
        relay_state_bitmask &= ~mask;  // Tắt relay

    relay_driver_update(relay_state_bitmask);
}

// Khởi tạo GPIO
void relay_driver_init(void) {
    gpio_reset_pin(RELAY_DS_PIN);
    gpio_reset_pin(RELAY_SHCP_PIN);
    gpio_reset_pin(RELAY_STCP_PIN);

    gpio_set_direction(RELAY_DS_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY_SHCP_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY_STCP_PIN, GPIO_MODE_OUTPUT);

    relay_driver_update(0x00);  // Tắt hết relay ban đầu
}
