#include "mqtt.h"
#include "esp_crt_bundle.h"

// ==== HTTP ====
httpd_handle_t server = NULL;

// ==== WIFI / EVENT ====
EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT      = BIT1;

int s_retry_num = 0;

QueueHandle_t mqtt_msg_queue=NULL;
esp_mqtt_client_handle_t global_client=NULL;

bool wifi_connected=false;
bool mqtt_connected=false;

static esp_netif_t *wifi_sta_netif = NULL;
static esp_netif_t *wifi_ap_netif  = NULL;

net_mode_t g_current_mode;

static bool mqtt_task_started=false;

bool relay1_state = false;
bool relay2_state = false;
bool relay3_state = false;
bool relay4_state = false;
bool relay5_state = false;
bool relay6_state = false;
bool relay7_state = false;
bool relay8_state = false;

// ====== NVS: Lưu/đọc SSID/PASS ======
void save_wifi_config(const char *ssid, const char *pass) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        ESP_ERROR_CHECK(nvs_set_str(nvs, "ssid", ssid));
        ESP_ERROR_CHECK(nvs_set_str(nvs, "pass", pass ? pass : ""));
        ESP_ERROR_CHECK(nvs_commit(nvs));
        nvs_close(nvs);
        ESP_LOGI(TAG, "Saved SSID:'%s' PASS:'%s'", ssid, pass);
    } else {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
    }
}

bool read_wifi_config(char *ssid, size_t ssid_len, char *pass, size_t pass_len) {
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READONLY, &nvs) != ESP_OK) return false;

    size_t need_ssid = ssid_len;
    size_t need_pass = pass_len;
    esp_err_t e1 = nvs_get_str(nvs, "ssid", ssid, &need_ssid);
    esp_err_t e2 = nvs_get_str(nvs, "pass", pass, &need_pass);
    nvs_close(nvs);
    if (e1 == ESP_OK && e2 == ESP_OK && ssid[0] != '\0') return true;
    return false;
}

void clear_wifi_config(void) {
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, "ssid");
        nvs_erase_key(nvs, "pass");
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGW(TAG, "Cleared WiFi credentials from NVS");
    }
}

// ====== URL decode ======
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            a = src[1]; b = src[2];
            a = (a >= 'a') ? a - 'a' + 10 : (a >= 'A') ? a - 'A' + 10 : a - '0';
            b = (b >= 'a') ? b - 'a' + 10 : (b >= 'A') ? b - 'A' + 10 : b - '0';
            *dst++ = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void stop_ap_if_connected(void) {
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    if (mode == WIFI_MODE_APSTA) {
        ESP_LOGI(TAG, "STA đã kết nối thành công -> Tắt AP");
        esp_wifi_set_mode(WIFI_MODE_STA);   // chỉ giữ STA
    }
}

static void wifi_event_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WIFI STA START");
                esp_wifi_connect();
            
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                wifi_connected = false;
                ESP_LOGW(TAG, "WIFI DISCONNECTED");

                if (global_client) {
                    esp_mqtt_client_stop(global_client);   // 🔥 ngắt MQTT khi mất Wi-Fi
                }

                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

                vTaskDelay(pdMS_TO_TICKS(1000));           // tránh reconnect quá nhanh
                esp_wifi_connect();                        // 🔥 luôn reconnect
                break;

            default:
                break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi IP: " IPSTR, IP2STR(&event->ip_info.ip));

        wifi_connected = true;
        s_retry_num = 0;

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        //xTaskCreate(rtc_task,"rtc_task",4096,NULL,1,NULL);

        stop_ap_if_connected();    // nếu có AP portal → tắt

        // 🔥 start MQTT khi có mạng
        if (global_client) {
            esp_mqtt_client_start(global_client);
        } else {
            mqtt_app_start();
           
        }
    }
}




// ====== STA connect (blocking wait with timeout) ======
bool wifi_sta_connect_blocking(const char *ssid, const char *pass, uint32_t timeout_ms) {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid)-1);
    strncpy((char*)wifi_config.sta.password, pass ? pass : "", sizeof(wifi_config.sta.password)-1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // vẫn kết nối được mạng open (pass rỗng)
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "STA connected to '%s'", ssid);
        return true;
    }
    ESP_LOGE(TAG, "STA connect timeout/fail");
    esp_wifi_stop();
    esp_wifi_deinit();
    return false;
}

// ====== Start SoftAP + Web server ======
void start_softap(void) {
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(AP_PASS) == 0) ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    esp_wifi_start();

    ESP_LOGI(TAG, "SoftAP started. SSID:%s PASS:%s", AP_SSID, AP_PASS);
}

// Trạng thái kết nối hiện tại (hiển thị ra WebUI)
static char g_status[64] = "Đang chờ chọn Wi-Fi…";


// ===== HTML =====
static const char *HTML_HEAD =
"<!DOCTYPE html><html lang='vi'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>ESP32 Wi-Fi Config</title>"
"<style>"
"body{font-family:system-ui,Arial;background:#f5f7fb;margin:0;padding:16px;}"
".card{max-width:720px;margin:0 auto;background:#fff;border-radius:12px;box-shadow:0 4px 16px rgba(0,0,0,.1);padding:20px;}"
"h1{font-size:22px;margin:0 0 12px;color:#222}"
"#status{padding:10px;margin:10px 0;border-radius:8px;font-weight:500;}"
"#status.ok{background:#e6f7e9;color:#0a7d24}"
"#status.err{background:#fdecea;color:#a30000}"
"#status.wait{background:#fff8e5;color:#8a6d00}"
".row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}"
"button,a.btn{padding:8px 12px;border:0;border-radius:8px;cursor:pointer;background:#1976d2;color:#fff;text-decoration:none;display:inline-block;}"
"button:active,a.btn:active{opacity:.8}"
"table{width:100%;border-collapse:collapse;margin-top:12px;overflow-x:auto;display:block}"
"th,td{padding:8px;border-bottom:1px solid #eee;text-align:left;white-space:nowrap}"
".ssid{font-weight:600}"
".muted{color:#666;font-size:12px}"
"form.inline{display:inline}"
"input[type=password]{padding:6px;border:1px solid #ddd;border-radius:8px;}"
"@media(max-width:600px){button,a.btn{width:100%;margin-top:6px}td,th{font-size:14px}}"
"</style></head><body><div class='card'>";

static const char *HTML_TAIL =
"<p class='muted'>HQ PRC IoT</p></div></body></html>";


// ===== Handler scan WiFi và render =====
static void render_ap_list(httpd_req_t *req) {
    wifi_scan_config_t scanConf = {0};
    esp_wifi_scan_start(&scanConf, true);

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num > 20) ap_num = 20;

    wifi_ap_record_t *ap_records = calloc(ap_num ? ap_num : 1, sizeof(wifi_ap_record_t));
    if (!ap_records) {
        httpd_resp_sendstr_chunk(req, "<p>Lỗi bộ nhớ khi quét.</p>");
        return;
    }
    esp_wifi_scan_get_ap_records(&ap_num, ap_records);

    httpd_resp_sendstr_chunk(req, "<div class='row'><h1 style='margin-right:auto'>Chọn Wi-Fi</h1>"
                                       "<a class='btn' href='/'>Quét lại</a></div>");

    // Hiển thị trạng thái kết nối
    char status_html[128];
    snprintf(status_html, sizeof(status_html),
             "<div id='status' class='%s'>%s</div>",
             strstr(g_status, "✅") ? "ok" :
             strstr(g_status, "❌") ? "err" : "wait",
             g_status);
    httpd_resp_sendstr_chunk(req, status_html);

    httpd_resp_sendstr_chunk(req, "<table><thead><tr>"
                                   "<th>SSID</th><th>RSSI</th><th>Mã hoá</th><th>Kết nối</th>"
                                   "</tr></thead><tbody>");

    for (int i = 0; i < ap_num; i++) {
        char row[1024];
        const char *enc =
            (ap_records[i].authmode == WIFI_AUTH_OPEN) ? "Open" :
            (ap_records[i].authmode == WIFI_AUTH_WEP) ? "WEP" :
            (ap_records[i].authmode == WIFI_AUTH_WPA_PSK) ? "WPA" :
            (ap_records[i].authmode == WIFI_AUTH_WPA2_PSK) ? "WPA2" :
            (ap_records[i].authmode == WIFI_AUTH_WPA_WPA2_PSK) ? "WPA/WPA2" :
            (ap_records[i].authmode == WIFI_AUTH_WPA2_ENTERPRISE) ? "WPA2-Ent" :
            (ap_records[i].authmode == WIFI_AUTH_WPA3_PSK) ? "WPA3" :
            (ap_records[i].authmode == WIFI_AUTH_WPA2_WPA3_PSK) ? "WPA2/WPA3" : "Other";

        snprintf(row, sizeof(row),
            "<tr>"
            "<td class='ssid'>%s</td>"
            "<td>%d dBm</td>"
            "<td>%s</td>"
            "<td>"
              "<form class='inline' action='/wifi' method='post'>"
                "<input type='hidden' name='ssid' value='%s'>"
                "<input type='password' name='pass' placeholder='Mật khẩu%s'> "
                "<button type='submit'>Kết nối</button>"
              "</form>"
            "</td>"
            "</tr>",
            (char*)ap_records[i].ssid,
            ap_records[i].rssi,
            enc,
            (char*)ap_records[i].ssid,
            (ap_records[i].authmode == WIFI_AUTH_OPEN ? " (mạng mở, để trống)" : "")
        );
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</tbody></table>");
    free(ap_records);
}


// ====== HTTP Handlers ======
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, HTML_HEAD);
    render_ap_list(req);
    httpd_resp_sendstr_chunk(req, HTML_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t favicon_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t select_get_handler(httpd_req_t *req) {
    char buf[128]; char ssid[33] = {0};
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, HTML_HEAD);
    char page[1024];
    snprintf(page, sizeof(page),
        "<h1>Nhập mật khẩu cho: <span class='ssid'>%s</span></h1>"
        "<form action='/wifi' method='post'>"
        "<input type='hidden' name='ssid' value='%s'>"
        "<p><input type='password' name='pass' placeholder='Mật khẩu (mạng mở để trống)'></p>"
        "<p><button type='submit'>Lưu & Reboot</button></p>"
        "</form>"
        "<p><a class='btn' href='/'>Quay lại</a></p>",
        ssid, ssid);
    httpd_resp_sendstr_chunk(req, page);
    httpd_resp_sendstr_chunk(req, HTML_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

#define MAX_POST_LEN 256
static esp_err_t wifi_post_handler(httpd_req_t *req) {
    if (req->content_len >= MAX_POST_LEN) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Post data too large");
        return ESP_FAIL;
    }
    char buf[MAX_POST_LEN+1];
    int cur = 0;
    while (cur < req->content_len) {
        int r = httpd_req_recv(req, buf + cur, req->content_len - cur);
        if (r <= 0) return ESP_FAIL;
        cur += r;
    }
    buf[cur] = '\0';

    // Parse "ssid=...&pass=..."
    char raw_ssid[64] = {0}, raw_pass[128] = {0};
    // Rộng tay để chịu các ký tự '=' trong pass: tách thủ công
    char *p_ssid = strstr(buf, "ssid=");
    char *p_pass = strstr(buf, "&pass=");
    if (!p_ssid) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid"); return ESP_FAIL; }

    if (p_pass) {
        size_t len_ssid = (size_t)(p_pass - (p_ssid + 5));
        if (len_ssid >= sizeof(raw_ssid)) len_ssid = sizeof(raw_ssid) - 1;
        memcpy(raw_ssid, p_ssid + 5, len_ssid);
        strncpy(raw_pass, p_pass + 6, sizeof(raw_pass)-1);
    } else {
        strncpy(raw_ssid, p_ssid + 5, sizeof(raw_ssid)-1);
        raw_pass[0] = '\0';
    }

    char ssid[33], pass[65];
    url_decode(ssid, raw_ssid);
    url_decode(pass, raw_pass);

    save_wifi_config(ssid, pass);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<html><body><h1>Đã lưu Wi-Fi. Thiết bị sẽ khởi động lại…</h1>"
        "<p>Nếu không tự kết nối được, ESP32 sẽ quay lại chế độ AP để cấu hình.</p>"
        "</body></html>");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// ====== Webserver start ======
void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri    = { .uri = "/",          .method = HTTP_GET,  .handler = root_get_handler    };
        httpd_uri_t select_uri  = { .uri = "/select",    .method = HTTP_GET,  .handler = select_get_handler  };
        httpd_uri_t wifi_uri    = { .uri = "/wifi",      .method = HTTP_POST, .handler = wifi_post_handler    };
        httpd_uri_t favicon_uri = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get_handler };

        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &select_uri);
        httpd_register_uri_handler(server, &wifi_uri);
        httpd_register_uri_handler(server, &favicon_uri);
        ESP_LOGI(TAG, "HTTP server started");
    } else {
        ESP_LOGE(TAG, "HTTP server start failed");
    }
}

net_mode_t load_network_mode_from_nvs(void)
{
    nvs_handle_t handle;
    uint8_t mode = NET_MODE_WIFI; // default

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK)
    {
        nvs_get_u8(handle, NVS_KEY_MODE, &mode);
        nvs_close(handle);
    }

    return (net_mode_t)mode;
}

void save_network_mode_to_nvs(net_mode_t mode)
{
    nvs_handle_t handle;

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
    {
        nvs_set_u8(handle, NVS_KEY_MODE, mode);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

void check_clear_wifi_config(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << RESET_BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    if (gpio_get_level(RESET_BTN_GPIO) == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(3000));

        if (gpio_get_level(RESET_BTN_GPIO) == 0)
        {
            ESP_LOGW(TAG, "Long press at boot → clearing WiFi config");
            ESP_ERROR_CHECK(nvs_flash_erase());
            ESP_ERROR_CHECK(nvs_flash_init());
        }
    }
}

const char *reset_reason_to_string(esp_reset_reason_t reason)
{
    switch(reason)
    {
        case ESP_RST_POWERON:
            return "POWER_ON";

        case ESP_RST_SW:
            return "SW_RESET";

        case ESP_RST_PANIC:
            return "PANIC";

        case ESP_RST_INT_WDT:
            return "INT_WDT";

        case ESP_RST_TASK_WDT:
            return "TASK_WDT";

        case ESP_RST_WDT:
            return "WDT";

        case ESP_RST_BROWNOUT:
            return "BROWNOUT";

        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";

        case ESP_RST_SDIO:
            return "SDIO";

        default:
            return "UNKNOWN";
    }
}

/* =====================================================
   SEND DEVICE INFO
===================================================== */
void mqtt_send_device_info(void)
{
    if(global_client == NULL)
        return;

    /* =====================================================
       FW VERSION
    ===================================================== */
    char fw_ver[32];

    ota_get_version(fw_ver, sizeof(fw_ver));

    /* =====================================================
       CHIP INFO
    ===================================================== */
    esp_chip_info_t chip_info;

    esp_chip_info(&chip_info);

    /* =====================================================
       MAC ADDRESS
    ===================================================== */
    uint8_t mac[6];

    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char mac_str[32];

    snprintf(mac_str,
             sizeof(mac_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);

    /* =====================================================
       FLASH SIZE
    ===================================================== */
    uint32_t flash_size = 0;

    esp_err_t flash_err =
        esp_flash_get_size(NULL, &flash_size);

    if(flash_err != ESP_OK)
    {
        ESP_LOGW("MQTT",
                 "Get flash size failed");

        flash_size = 0;
    }

    /* =====================================================
       RESET REASON
    ===================================================== */
    esp_reset_reason_t reset_reason =
        esp_reset_reason();

    /* =====================================================
       OTA PARTITION
    ===================================================== */
    const esp_partition_t *running =
        esp_ota_get_running_partition();

    /* =====================================================
       CREATE JSON
    ===================================================== */
    cJSON *root = cJSON_CreateObject();

    if(root == NULL)
        return;

    /* =====================================================
       DEVICE INFO
    ===================================================== */
    cJSON_AddStringToObject(root,
                            "device_name",
                            "HQPRC_HOMEKIT");

    cJSON_AddStringToObject(root,
                            "device_type",
                            "IoT_Node");

    cJSON_AddStringToObject(root,
                            "project",
                            "HOME_AUTOMATION");

    /* =====================================================
       OTA / FW
    ===================================================== */
    cJSON_AddStringToObject(root,
                            "fw_version",
                            fw_ver);

    cJSON_AddStringToObject(root,
                            "idf_version",
                            esp_get_idf_version());

    /* =====================================================
       CHIP INFO
    ===================================================== */
    cJSON_AddStringToObject(root,
                            "chip_model",
                            CONFIG_IDF_TARGET);

    cJSON_AddNumberToObject(root,
                            "cpu_core",
                            chip_info.cores);

    cJSON_AddNumberToObject(root,
                            "chip_revision",
                            chip_info.revision);

    cJSON_AddNumberToObject(root,
                            "flash_size_mb",
                            flash_size / (1024 * 1024));

    /* =====================================================
       MEMORY
    ===================================================== */
    cJSON_AddNumberToObject(root,
                            "free_heap",
                            esp_get_free_heap_size());

    cJSON_AddNumberToObject(root,
                            "min_free_heap",
                            esp_get_minimum_free_heap_size());

    /* =====================================================
       SYSTEM
    ===================================================== */
    cJSON_AddStringToObject(root,
                            "reset_reason",
                            reset_reason_to_string(reset_reason));

    cJSON_AddNumberToObject(root,
                            "uptime_sec",
                            esp_log_timestamp() / 1000);

    /* =====================================================
       OTA PARTITION
    ===================================================== */
    if(running)
    {
        cJSON_AddStringToObject(root,
                                "running_partition",
                                running->label);
    }

    /* =====================================================
       MAC
    ===================================================== */
    cJSON_AddStringToObject(root,
                            "mac",
                            mac_str);

    /* =====================================================
       BUILD INFO
    ===================================================== */
    cJSON_AddStringToObject(root,
                            "build_date",
                            __DATE__);

    cJSON_AddStringToObject(root,
                            "build_time",
                            __TIME__);

    /* =====================================================
       JSON STRING
    ===================================================== */
    char *msg =
        cJSON_PrintUnformatted(root);

    if(msg)
    {
        esp_mqtt_client_publish(global_client,
                                "v1/devices/me/attributes",
                                msg,
                                0,
                                1,
                                0);

        ESP_LOGI("MQTT",
                 "Device info published");

        free(msg);
    }

    cJSON_Delete(root);
}

void wifi_init(void)
{
    esp_err_t ret;

    /* ---------- NVS ---------- */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }
    
    check_clear_wifi_config();

    /* ---------- NETIF & EVENT LOOP ---------- */
    ESP_ERROR_CHECK(esp_netif_init());

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    s_wifi_event_group = xEventGroupCreate();

    /* ---------- CREATE NETIF (CHỈ STA) ---------- */
    wifi_sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_default_netif(wifi_sta_netif); // cho MQTT

    /* ---------- WIFI INIT ---------- */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    /* ---------- LOAD WIFI ---------- */
    char ssid[33] = {0}, pass[65] = {0};
    bool have_cred = read_wifi_config(ssid, sizeof(ssid), pass, sizeof(pass));

    if (have_cred) {
        ESP_LOGI(TAG, "Found saved Wi-Fi: '%s'", ssid);

        wifi_config_t wifi_config = {0};
        strcpy((char*)wifi_config.sta.ssid, ssid);
        strcpy((char*)wifi_config.sta.password, pass);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_start();
    } else {
        ESP_LOGW(TAG, "No saved Wi-Fi → Start AP portal");
        start_softap();     // ✅ để nó tạo AP netif
        start_webserver();
    }

    /* ---------- START WIFI (1 LẦN DUY NHẤT) ---------- */
  
}

static void mqtt_event_handler(void *arg,esp_event_base_t event_base,int32_t event_id,void *event_data)
{
    esp_mqtt_event_handle_t event=event_data;
    esp_mqtt_client_handle_t client=event->client;

    switch(event->event_id)
    {
        case MQTT_EVENT_CONNECTED:
        {
            global_client=client;
            mqtt_connected=true;
            esp_mqtt_client_subscribe(client,"v1/devices/me/rpc/request/+",1);
            esp_mqtt_client_subscribe(client,"v1/devices/me/attributes",1);
            mqtt_send_device_info();
            if(!mqtt_task_started)
            {
                mqtt_task_started=true;
                xTaskCreate(TaskPublish,"TaskPublish",4096,NULL,1,NULL);
                xTaskCreate(TaskSubScribe,"TaskSubscribe",4096,NULL,1,NULL);
                
            }
            break;
        }
        case MQTT_EVENT_DATA:
        {
            mqtt_msg_t msg;
            memset(&msg, 0, sizeof(msg));

            snprintf(msg.topic, sizeof(msg.topic),
                    "%.*s", event->topic_len, event->topic);

            snprintf(msg.data, sizeof(msg.data),
                    "%.*s", event->data_len, event->data);

            if (mqtt_msg_queue != NULL)
            {
                xQueueSendFromISR(mqtt_msg_queue, &msg, 0);
            }

            if (strncmp(event->topic,
                        "v1/devices/me/attributes",
                        event->topic_len) == 0)
            {
                char ota_buf[512];
                int len = event->data_len;

                if (len > sizeof(ota_buf) - 1)
                    len = sizeof(ota_buf) - 1;

                memcpy(ota_buf, event->data, len);
                ota_buf[len] = '\0';

                // FIX: chỉ trigger khi đúng OTA data
                if (strstr(ota_buf, "fw_title") &&
                strstr(ota_buf, "fw_version"))
                {
                    handle_ota_json(ota_buf);
                }
            }

            break;
        }
        case MQTT_EVENT_DISCONNECTED:
        {
            mqtt_connected=false;
            break;
        }
        default: 
        {
            //ESP_LOGI(TAG,"MQTT_ID %d\r\n",event->event_id);
        }
    }
}

void mqtt_app_start(void)
{
    mqtt_msg_queue = xQueueCreate(10, sizeof(mqtt_msg_t));

    /* ===== Create unique client ID from MAC ===== */

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    static char client_id[32];

    snprintf(client_id,
             sizeof(client_id),
             "gateway_%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    /* ===== MQTT CONFIG ===== */

    esp_mqtt_client_config_t mqtt_config = {

        .broker = {
            .address.uri  = "mqtts://tb.iotnamban.cloud",
            .address.port = 8883,
            .verification.crt_bundle_attach = esp_crt_bundle_attach,
        },

        .credentials = {
            .client_id = client_id,
            .username  = "dpf14WLAUPBKrzXIAvhV",   
        },

        .session = {
            .keepalive = 30,
            .disable_clean_session = false,
        },

        .network = {
            .disable_auto_reconnect = false,
            .reconnect_timeout_ms = 5000,
            .timeout_ms = 5000,
        },

        .buffer = {
            .size = 4096,
            .out_size = 4096,
        },
    };

    /* ===== MQTT CLIENT INIT ===== */

    esp_mqtt_client_handle_t client =
        esp_mqtt_client_init(&mqtt_config);

    esp_mqtt_client_register_event(
        client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL);

    esp_mqtt_client_start(client);

    global_client = client;
}

static void trim_new_line(char *str)
{
    int len=strlen(str);
    while(len>0&&(str[len-1]=='\r'||str[len-1]=='\n'))
    {
        str[len-1]='\0';
        len--;
    }
}

void publish_relay_state(void)
{
    if (!mqtt_connected)
        return;

    cJSON *root = cJSON_CreateObject();

    if(root == NULL)
        return;

    cJSON_AddNumberToObject(root,
                            "relay1",
                            relay1_state);

    cJSON_AddNumberToObject(root,
                            "relay2",
                            relay2_state);

    char *json_str =
        cJSON_PrintUnformatted(root);

    if(json_str)
    {
        esp_mqtt_client_publish(
            global_client,
            "v1/devices/me/telemetry",
            json_str,
            0,
            1,
            0);

        free(json_str);
    }

    cJSON_Delete(root);
}

void control_relay(uint8_t relay, uint8_t state)
{
    bool on = (state == 1);

    switch(relay)
    {
        case 1:
            relay_uart_control(4, on);
            relay1_state = on;
            break;

        case 2:
            relay_uart_control(3, on);
            relay2_state = on;
            break;

        case 3:
            relay_uart_control(2, on);
            relay3_state = on;
            break;

        case 4:
            relay_uart_control(1, on);
            relay4_state = on;
            break;

        case 5:
             relay_driver_control(4,on);
            relay5_state = on;
            break;

        case 6:
            relay_driver_control(3,on);
            relay6_state = on;
            break;

        case 7:
            relay_driver_control(2,on);
            relay7_state = on;
            break;

        case 8:
            relay_driver_control(1,on);
            relay8_state = on;
            break;

        default:
            break;
    }

    /* =====================================
       REALTIME STATE UPDATE
    ===================================== */

    // publish_relay_state();
}

extern int16_t humidity;
extern int16_t temperature;

void TaskPublish(void *pvParameters)
{
    wifi_ap_record_t ap_info;

    static float lastRain = 0;

    while (1)
    {
        if (mqtt_connected == true && global_client != NULL)
        {
            cJSON *root = cJSON_CreateObject();

            if (root == NULL)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            /* =====================================================
               WIFI INFO
            ===================================================== */

            int rssi = 0;
            int wifi_quality = 0;

            char wifi_status[32] = "DISCONNECTED";

            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
            {
                rssi = ap_info.rssi;

                /* RSSI -> QUALITY */

                if (rssi >= -50)
                    wifi_quality = 100;
                else if (rssi <= -100)
                    wifi_quality = 0;
                else
                    wifi_quality = 2 * (rssi + 100);

                /* WIFI STATUS */

                if (rssi >= -55)
                    strcpy(wifi_status, "EXCELLENT");
                else if (rssi >= -65)
                    strcpy(wifi_status, "GOOD");
                else if (rssi >= -75)
                    strcpy(wifi_status, "FAIR");
                else if (rssi >= -85)
                    strcpy(wifi_status, "WEAK");
                else
                    strcpy(wifi_status, "VERY_WEAK");

                cJSON_AddNumberToObject(root,
                                        "wifi_rssi",
                                        rssi);

                cJSON_AddNumberToObject(root,
                                        "wifi_quality",
                                        wifi_quality);

                cJSON_AddStringToObject(root,
                                        "wifi_status",
                                        wifi_status);
            }
            else
            {
                cJSON_AddNumberToObject(root,
                                        "wifi_rssi",
                                        0);

                cJSON_AddNumberToObject(root,
                                        "wifi_quality",
                                        0);

                cJSON_AddStringToObject(root,
                                        "wifi_status",
                                        "DISCONNECTED");
            }

            /* =====================================================
               SENSOR DATA
            ===================================================== */

            cJSON_AddNumberToObject(root,
                                    "temp1",
                                    sensorData.temp1);

            cJSON_AddNumberToObject(root,
                                    "temp2",
                                    sensorData.temp2);

            cJSON_AddNumberToObject(root,
                                    "humidity",
                                    sensorData.humidity);

            cJSON_AddNumberToObject(root,
                                    "v_bat",
                                    sensorData.v_bat);

            cJSON_AddNumberToObject(root,
                                    "num",
                                    sensorData.num);

            cJSON_AddNumberToObject(root,
                                    "s1",
                                    sensorData.s1);

            cJSON_AddNumberToObject(root,
                                    "s2",
                                    sensorData.s2);

            cJSON_AddNumberToObject(root,
                                    "t_dh",
                                    sensorData.t_dh);

            cJSON_AddNumberToObject(root,
                                    "lux",
                                    sensorData.lux);

            cJSON_AddNumberToObject(root,
                                    "aht_temp",
                                    sensorData.aht_temp);

            cJSON_AddNumberToObject(root,
                                    "aht_hum",
                                    sensorData.aht_hum);

            cJSON_AddNumberToObject(root,
                                    "moisture_capacitive",
                                    sensorData.moisture_capacitive);

            cJSON_AddNumberToObject(root,
                                    "bme280_pressure",
                                    sensorData.bme280_pressure);

            cJSON_AddNumberToObject(root,
                                    "dht11_data0",
                                    sensorData.dht11_data0);

            cJSON_AddNumberToObject(root,
                                    "dht11_data1",
                                    sensorData.dht11_data1);

            cJSON_AddNumberToObject(root,
                                    "main_voltage",
                                    g_vsepic);

            /* =====================================================
               RELAY STATUS
            ===================================================== */

           /* =====================================================
            RELAY STATUS
            ===================================================== */

            cJSON_AddBoolToObject(root,
                                "relay1",
                                relay1_state);

            cJSON_AddBoolToObject(root,
                                "relay2",
                                relay2_state);

            cJSON_AddBoolToObject(root,
                                "relay3",
                                relay3_state);

            cJSON_AddBoolToObject(root,
                                "relay4",
                                relay4_state);

            cJSON_AddBoolToObject(root,
                                "relay5",
                                relay5_state);

            cJSON_AddBoolToObject(root,
                                "relay6",
                                relay6_state);

            cJSON_AddBoolToObject(root,
                                "relay7",
                                relay7_state);

            cJSON_AddBoolToObject(root,
                                "relay8",
                                relay8_state);

            /* =====================================================
               UART DEBUG
            ===================================================== */

            if (strlen(uart0_buffer) > 0)
            {
                // cJSON_AddStringToObject(root,
                //                         "debug_uart",
                //                         uart0_buffer);
            }

            /* =====================================================
               SYSTEM HEALTH
            ===================================================== */

            uint32_t uptime_sec =
                esp_log_timestamp() / 1000;

            uint32_t days =
                uptime_sec / 86400;

            uint32_t hours =
                (uptime_sec % 86400) / 3600;

            uint32_t minutes =
                (uptime_sec % 3600) / 60;

            uint32_t seconds =
                uptime_sec % 60;

            char uptime_str[64];

            snprintf(uptime_str,
                     sizeof(uptime_str),
                     "%luD %02luH %02luM %02luS",
                     days,
                     hours,
                     minutes,
                     seconds);

            cJSON_AddNumberToObject(root,
                                    "free_heap",
                                    esp_get_free_heap_size());

            cJSON_AddNumberToObject(root,
                                    "min_free_heap",
                                    esp_get_minimum_free_heap_size());

            cJSON_AddNumberToObject(root,
                                    "uptime_sec",
                                    uptime_sec);

            cJSON_AddStringToObject(root,
                                    "uptime_text",
                                    uptime_str);

            /* =====================================================
               MQTT STATUS
            ===================================================== */

            cJSON_AddStringToObject(root,
                                    "mqtt_status",
                                    "CONNECTED");

            /* =====================================================
               JSON SERIALIZE
            ===================================================== */

            char *json_str =
                cJSON_PrintUnformatted(root);

            if (json_str != NULL)
            {
                esp_mqtt_client_publish(global_client,
                                        "v1/devices/me/telemetry",
                                        json_str,
                                        0,
                                        1,
                                        0);

                // printf("MQTT: %s\n", json_str);

                free(json_str);
            }

            cJSON_Delete(root);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void TaskSubScribe(void *pvParameters)
{
    mqtt_msg_t rxBuffer;

    ESP_LOGI(TAG, "TaskSubscribe START");

    while (1)
    {
        if (xQueueReceive(mqtt_msg_queue, &rxBuffer, portMAX_DELAY) != pdTRUE)
        {
            ESP_LOGW(TAG, "Queue receive fail");
            continue;
        }

        ESP_LOGI(TAG, "MQTT RX TOPIC : %s", rxBuffer.topic);
        ESP_LOGI(TAG, "MQTT RX DATA  : %s", rxBuffer.data);

        /* =============================
           CHECK RPC TOPIC
        ============================= */

        if (strncmp(rxBuffer.topic,
                    "v1/devices/me/rpc/request/",
                    26) != 0)
        {
            ESP_LOGW(TAG, "Not RPC topic -> ignore");
            continue;
        }

        /* =============================
           GET REQUEST ID
        ============================= */

        char req_id[16] = {0};

        size_t id_len = strlen(rxBuffer.topic + 26);

        if (id_len >= sizeof(req_id))
            id_len = sizeof(req_id) - 1;

        memcpy(req_id, rxBuffer.topic + 26, id_len);

        ESP_LOGI(TAG, "RPC request id: %s", req_id);

        /* =============================
           PARSE JSON
        ============================= */

        cJSON *root = cJSON_Parse(rxBuffer.data);

        if (!root)
        {
            ESP_LOGE(TAG, "JSON parse FAILED");
            continue;
        }

        cJSON *method = cJSON_GetObjectItem(root, "method");
        cJSON *params = cJSON_GetObjectItem(root, "params");

        if (!method || !cJSON_IsString(method))
        {
            ESP_LOGE(TAG, "Method invalid");
            cJSON_Delete(root);
            continue;
        }

        ESP_LOGI(TAG, "RPC method: %s", method->valuestring);

        /* =====================================================
           LOCAL RELAY CONTROL
        ===================================================== */

        if (strcmp(method->valuestring, "setRelay") == 0)
        {
            ESP_LOGI(TAG, "Handle LOCAL RELAY");

            int relay = 1;
            int state = 0;

            /* ---- CASE 1: params = true/false (switch widget) ---- */

            if (cJSON_IsBool(params))
            {
                relay = 1; // default relay
                state = cJSON_IsTrue(params);

                ESP_LOGI(TAG,
                         "Switch widget relay=%d state=%d",
                         relay,
                         state);
            }

            /* ---- CASE 2: params = number (button widget) ---- */

            else if (cJSON_IsNumber(params))
            {
                relay = params->valueint;
                state = 1;

                ESP_LOGI(TAG,
                         "Button widget relay=%d",
                         relay);
            }

            /* ---- CASE 3: params = object ---- */

            else if (cJSON_IsObject(params))
            {
                cJSON *relay_json = cJSON_GetObjectItem(params, "relay");
                cJSON *state_json = cJSON_GetObjectItem(params, "state");

                if (relay_json && cJSON_IsNumber(relay_json))
                    relay = relay_json->valueint;

                if (state_json && cJSON_IsNumber(state_json))
                    state = state_json->valueint;

                ESP_LOGI(TAG,
                         "Object params relay=%d state=%d",
                         relay,
                         state);
            }

            else
            {
                ESP_LOGW(TAG, "Unsupported params type");
            }

            /* ---- CONTROL RELAY ---- */

            if (relay >= 1 && relay <= 8)
            {
                control_relay(relay,state);

                ESP_LOGI(TAG,
                         "Relay %d -> %s",
                         relay,
                         state ? "ON" : "OFF");
            }
            else
            {
                ESP_LOGW(TAG, "Invalid relay index");
            }
        }

        
        /* =====================================================
        SYNC TIME COMMAND
        ===================================================== */

        else if (strcmp(method->valuestring, "syncTime") == 0)
        {
            ESP_LOGI(TAG, "Handle SYNC TIME");

           // uart_send_time();   // <-- gọi hàm của bạn

            ESP_LOGI(TAG, "Time sync command sent");
        }

        else
        {
            ESP_LOGW(TAG, "Unknown RPC method");
        }

        /* =====================================================
           SEND RPC RESPONSE
        ===================================================== */

        char resp_topic[64];

        snprintf(resp_topic,
                 sizeof(resp_topic),
                 "v1/devices/me/rpc/response/%s",
                 req_id);

        esp_mqtt_client_publish(
            global_client,
            resp_topic,
            "{\"success\":true}",
            0,
            1,
            0);

        cJSON_Delete(root);
    }
}