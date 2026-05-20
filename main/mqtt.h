#ifndef _MQTT_H
#define _MQTT_H
#include "main.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "ds3231.h"
#include "w5500_lan.h"
#include "ota.h"
#include "edge_controller.h"

#define MAX_MQTT_MSG_LEN 128
typedef struct
{
    char topic[64];
    char data[MAX_MQTT_MSG_LEN];
}mqtt_msg_t;

extern QueueHandle_t mqtt_msg_queue;

extern bool wifi_connected;
extern bool mqtt_connected;


#define RESET_BTN_GPIO 34

// ==== CẤU HÌNH AP PORTAL ====
#define AP_SSID        "HQPRC_GWC"
#define AP_PASS        "12345678"
#define AP_CHANNEL     1
#define AP_MAX_CONN    4

// ==== THỜI GIAN CHỜ KẾT NỐI STA (ms) ====
#define STA_CONNECT_TIMEOUT_MS 15000

// ==== HTTP ====
extern httpd_handle_t server;

// ==== WIFI / EVENT ====
extern EventGroupHandle_t s_wifi_event_group;
extern const int WIFI_CONNECTED_BIT;
extern const int WIFI_FAIL_BIT;

extern int s_retry_num;
#define MAX_RETRY 10

extern QueueHandle_t mqtt_msg_queue;

extern bool wifi_connected;
extern bool mqtt_connected;

#define NVS_NAMESPACE   "net_cfg"
#define NVS_KEY_MODE    "net_mode"

typedef enum
{
    NET_MODE_WIFI = 1,
    NET_MODE_ETHERNET = 2
} net_mode_t;

extern net_mode_t g_current_mode;

extern esp_mqtt_client_handle_t global_client;

void check_clear_wifi_config(void);
void start_webserver(void);
void start_softap(void);
void clear_wifi_config(void);
bool read_wifi_config(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
void save_wifi_config(const char *ssid, const char *pass);
bool wifi_sta_connect_blocking(const char *ssid, const char *pass, uint32_t timeout_ms);
void wifi_init(void);
net_mode_t load_network_mode_from_nvs(void);
void save_network_mode_to_nvs(net_mode_t mode);
void mqtt_app_start(void);
void TaskPublish(void *pvParameters);
void TaskSubScribe(void *pvParameters);

#endif