#include "ota.h"

#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

#define TB_HOST            "tb.iotnamban.cloud"
#define DEVICE_TOKEN       "dpf14WLAUPBKrzXIAvhV"

char g_current_fw_version[32] = "1.0";
char g_target_fw_version[32]  = "";
char g_fw_title[64]           = "";

typedef struct
{
    char version[32];
    char title[64];
    char url[256];

    char checksum[80];
    char checksum_algo[16];

} ota_param_t;


/* =========================================================
   HEX TO STRING
========================================================= */
static void sha256_to_string(uint8_t *hash,
                             char *output)
{
    for (int i = 0; i < 32; i++)
    {
        sprintf(output + (i * 2),
                "%02x",
                hash[i]);
    }

    output[64] = '\0';
}

/* =========================================
   MQTT OTA STATUS
========================================= */
/* =========================================
   MQTT OTA STATUS
========================================= */
void ota_publish_state(const char *state,
                       int progress,
                       const char *current_version,
                       const char *target_version,
                       const char *title)
{
    if (global_client == NULL)
        return;

    char payload[512];

    snprintf(payload,
             sizeof(payload),
             "{"
             "\"fw_state\":\"%s\","
             "\"fw_progress\":%d,"
             "\"current_fw_version\":\"%s\","
             "\"current_fw_title\":\"%s\","
             "\"target_fw_version\":\"%s\","
             "\"target_fw_title\":\"%s\""
             "}",
             state,
             progress,
             current_version ? current_version : "",
             title ? title : "",
             target_version ? target_version : "",
             title ? title : "");

    esp_mqtt_client_publish(global_client,
                            "v1/devices/me/telemetry",
                            payload,
                            0,
                            1,
                            0);

    ESP_LOGI(TAG,
             "OTA STATUS TX: %s",
             payload);
}

/* =========================================
   SAVE VERSION TO NVS
========================================= */
void ota_save_version(const char *ver)
{
    nvs_handle_t nvs;

    if (nvs_open(OTA_NAMESPACE,
                 NVS_READWRITE,
                 &nvs) == ESP_OK)
    {
        nvs_set_str(nvs,
                    OTA_KEY_VERSION,
                    ver);

        nvs_commit(nvs);

        nvs_close(nvs);

        ESP_LOGI(TAG,
                 "Saved FW version: %s",
                 ver);
    }
    else
    {
        ESP_LOGE(TAG,
                 "NVS open failed");
    }
}

/* =========================================
   READ VERSION FROM NVS
========================================= */
void ota_get_version(char *ver_out,
                     size_t len)
{
    nvs_handle_t nvs;

    strncpy(ver_out,
            "0.0",
            len);

    if (nvs_open(OTA_NAMESPACE,
                 NVS_READONLY,
                 &nvs) == ESP_OK)
    {
        size_t required = len;

        if (nvs_get_str(nvs,
                        OTA_KEY_VERSION,
                        ver_out,
                        &required) != ESP_OK)
        {
            strncpy(ver_out,
                    "0.0",
                    len);
        }

        nvs_close(nvs);
    }
}

/* =========================================
   JSON HANDLE
========================================= */
/* =========================================
   JSON HANDLE
========================================= */
void handle_ota_json(char *data)
{
    cJSON *root = cJSON_Parse(data);

    if (!root)
        return;

    cJSON *title = cJSON_GetObjectItem(root,
                                       "fw_title");

    cJSON *ver = cJSON_GetObjectItem(root,
                                     "fw_version");

    cJSON *checksum = cJSON_GetObjectItem(root,
                                          "fw_checksum");

    cJSON *algo = cJSON_GetObjectItem(root,
                                      "fw_checksum_algorithm");

    if (!cJSON_IsString(title) ||
        !cJSON_IsString(ver) ||
        !cJSON_IsString(checksum) ||
        !cJSON_IsString(algo))
    {
        ESP_LOGE(TAG,
                 "OTA JSON invalid");

        cJSON_Delete(root);

        return;
    }

    char current_ver[32];

    ota_get_version(current_ver,
                    sizeof(current_ver));

    ESP_LOGI(TAG,
             "Current FW: %s",
             current_ver);

    ESP_LOGI(TAG,
             "New FW: %s",
             ver->valuestring);

    /* VERSION CHECK */
    if (strcmp(current_ver,
               ver->valuestring) == 0)
    {
        ESP_LOGI(TAG,
                 "Firmware already latest");

        cJSON_Delete(root);

        return;
    }

    /* SAVE GLOBAL INFO */
    strncpy(g_target_fw_version,
            ver->valuestring,
            sizeof(g_target_fw_version) - 1);

    strncpy(g_fw_title,
            title->valuestring,
            sizeof(g_fw_title) - 1);

    ota_param_t *param =
        malloc(sizeof(ota_param_t));

    if (!param)
    {
        cJSON_Delete(root);
        return;
    }

    memset(param,
           0,
           sizeof(ota_param_t));

    strncpy(param->version,
            ver->valuestring,
            sizeof(param->version) - 1);

    strncpy(param->title,
            title->valuestring,
            sizeof(param->title) - 1);

    strncpy(param->checksum,
            checksum->valuestring,
            sizeof(param->checksum) - 1);

    strncpy(param->checksum_algo,
            algo->valuestring,
            sizeof(param->checksum_algo) - 1);

    /* BUILD INTERNAL TB OTA URL */
    snprintf(param->url,
             sizeof(param->url),
             "https://%s/api/v1/%s/firmware?title=%s&version=%s",
             TB_HOST,
             DEVICE_TOKEN,
             title->valuestring,
             ver->valuestring);

    ESP_LOGI(TAG,
             "OTA URL: %s",
             param->url);

    ESP_LOGI(TAG,
             "Checksum: %s",
             param->checksum);

    ESP_LOGI(TAG,
             "Algorithm: %s",
             param->checksum_algo);

    xTaskCreate(ota_task,
                "ota_task",
                12288,
                param,
                5,
                NULL);

    cJSON_Delete(root);
}

/* =========================================
   OTA START
========================================= */
void ota_start(const char *url,
               const char *new_version,
               const char *expected_checksum,
               const char *checksum_algo)
{
    ESP_LOGI(TAG,
             "===== OTA START =====");

    ESP_LOGI(TAG,
             "OTA URL: %s",
             url);

    char current_ver[32];

    ota_get_version(current_ver,
                    sizeof(current_ver));

    /* OTA START */
    ota_publish_state("DOWNLOADING",
                      0,
                      current_ver,
                      new_version,
                      "firmware");

    esp_http_client_config_t config =
    {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (!client)
    {
        ESP_LOGE(TAG,
                 "HTTP init failed");

        ota_publish_state("FAILED",
                          0,
                          current_ver,
                          new_version,
                          "firmware");

        return;
    }

    /* OPEN CONNECTION */
    esp_err_t err =
        esp_http_client_open(client, 0);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "HTTP open failed: %s",
                 esp_err_to_name(err));

        ota_publish_state("FAILED",
                          0,
                          current_ver,
                          new_version,
                          "firmware");

        esp_http_client_cleanup(client);

        return;
    }

    /* FETCH HEADERS */
    esp_http_client_fetch_headers(client);

    int status_code =
        esp_http_client_get_status_code(client);

    ESP_LOGI(TAG,
             "HTTP STATUS: %d",
             status_code);

    /* CHECK HTTP STATUS */
    if (status_code != 200)
    {
        ESP_LOGE(TAG,
                 "Firmware URL invalid");

        ota_publish_state("FAILED",
                          0,
                          current_ver,
                          new_version,
                          "firmware");

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return;
    }

    /* GET CONTENT LENGTH */
    int content_length =
        esp_http_client_get_content_length(client);

    ESP_LOGI(TAG,
             "Firmware size: %d bytes",
             content_length);

    if (content_length <= 0)
    {
        ESP_LOGE(TAG,
                 "Invalid firmware size");

        ota_publish_state("FAILED",
                          0,
                          current_ver,
                          new_version,
                          "firmware");

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return;
    }

    /* GET OTA PARTITION */
    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);

    if (!update_partition)
    {
        ESP_LOGE(TAG,
                 "No OTA partition");

        ota_publish_state("FAILED",
                          0,
                          current_ver,
                          new_version,
                          "firmware");

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return;
    }

    ESP_LOGI(TAG,
             "Writing to partition: %s",
             update_partition->label);

    /* OTA BEGIN */
    esp_ota_handle_t ota_handle;

    err = esp_ota_begin(update_partition,
                        OTA_SIZE_UNKNOWN,
                        &ota_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "OTA begin failed: %s",
                 esp_err_to_name(err));

        ota_publish_state("FAILED",
                          0,
                          current_ver,
                          new_version,
                          "firmware");

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return;
    }

    uint8_t buffer[OTA_BUFFSIZE];

    int data_read;
    int total = 0;
    int last_percent = -1;

    /* SHA256 INIT */
    mbedtls_sha256_context sha_ctx;

    mbedtls_sha256_init(&sha_ctx);

    mbedtls_sha256_starts(&sha_ctx,
                          0);

    /* DOWNLOAD LOOP */
    while ((data_read =
            esp_http_client_read(client,
                                 (char *)buffer,
                                 OTA_BUFFSIZE)) > 0)
    {
        /* UPDATE SHA256 */
        mbedtls_sha256_update(&sha_ctx,
                              buffer,
                              data_read);

        /* WRITE OTA */
        err = esp_ota_write(ota_handle,
                            buffer,
                            data_read);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG,
                     "OTA write failed: %s",
                     esp_err_to_name(err));

            ota_publish_state("FAILED",
                              last_percent,
                              current_ver,
                              new_version,
                              "firmware");

            esp_ota_abort(ota_handle);

            esp_http_client_close(client);
            esp_http_client_cleanup(client);

            return;
        }

        total += data_read;

        int percent =
            (total * 100) / content_length;

        /* SEND ONLY WHEN CHANGED */
        if (percent != last_percent)
        {
            last_percent = percent;

            ota_publish_state("DOWNLOADING",
                              percent,
                              current_ver,
                              new_version,
                              "firmware");

            ESP_LOGI(TAG,
                     "Downloaded: %d / %d (%d%%)",
                     total,
                     content_length,
                     percent);
        }
    }

    /* READ ERROR */
    if (data_read < 0)
    {
        ESP_LOGE(TAG,
                 "HTTP read error");

        ota_publish_state("FAILED",
                          last_percent,
                          current_ver,
                          new_version,
                          "firmware");

        esp_ota_abort(ota_handle);

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return;
    }

    ota_publish_state("DOWNLOADED",
                      100,
                      current_ver,
                      new_version,
                      "firmware");

    /* CHECKSUM VERIFY */
    uint8_t sha_result[32];

    char sha_string[65];

    mbedtls_sha256_finish(&sha_ctx,
                          sha_result);

    mbedtls_sha256_free(&sha_ctx);

    sha256_to_string(sha_result,
                     sha_string);

    ESP_LOGI(TAG,
             "Calculated SHA256: %s",
             sha_string);

    ESP_LOGI(TAG,
             "Expected SHA256  : %s",
             expected_checksum);

    /* CHECK ALGORITHM */
    if (strcasecmp(checksum_algo,
                   "SHA256") != 0)
    {
        ESP_LOGE(TAG,
                 "Unsupported checksum algorithm: %s",
                 checksum_algo);

        ota_publish_state("FAILED",
                          100,
                          current_ver,
                          new_version,
                          "firmware");

        esp_ota_abort(ota_handle);

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return;
    }

    /* COMPARE CHECKSUM */
    if (strcasecmp(sha_string,
                   expected_checksum) != 0)
    {
        ESP_LOGE(TAG,
                 "Checksum mismatch!");

        ota_publish_state("FAILED",
                          100,
                          current_ver,
                          new_version,
                          "firmware");

        esp_ota_abort(ota_handle);

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return;
    }

    ESP_LOGI(TAG,
             "Checksum OK");

    /* OTA END */
    err = esp_ota_end(ota_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "OTA end failed: %s",
                 esp_err_to_name(err));

        ota_publish_state("FAILED",
                          100,
                          current_ver,
                          new_version,
                          "firmware");

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return;
    }

    ota_publish_state("VERIFIED",
                      100,
                      current_ver,
                      new_version,
                      "firmware");

    /* SET BOOT PARTITION */
    err = esp_ota_set_boot_partition(update_partition);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "Set boot partition failed: %s",
                 esp_err_to_name(err));

        ota_publish_state("FAILED",
                          100,
                          current_ver,
                          new_version,
                          "firmware");

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        return;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    /* SAVE NEW VERSION */
    ota_save_version(new_version);

    ota_publish_state("UPDATED",
                      100,
                      new_version,
                      new_version,
                      "firmware");

    ESP_LOGI(TAG,
             "===== OTA SUCCESS =====");

    ESP_LOGI(TAG,
             "Rebooting...");

    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_restart();
}

/* =========================================
   OTA TASK
========================================= */
void ota_task(void *param)
{
    ota_param_t *p =
        (ota_param_t *)param;

    ota_start(p->url,
              p->version,
              p->checksum,
              p->checksum_algo);

    free(p);

    vTaskDelete(NULL);
}