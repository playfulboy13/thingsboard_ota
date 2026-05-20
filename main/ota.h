#ifndef _OTA_H
#define _OTA_H

#include "main.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "mbedtls/sha256.h"

#define OTA_BUFFSIZE      1024
#define OTA_NAMESPACE     "fw"
#define OTA_KEY_VERSION   "version"

extern char g_current_fw_version[32];
extern char g_target_fw_version[32];
extern char g_fw_title[64];

void handle_ota_json(char *data);
void ota_task(void *param);
void ota_start(const char *url,
               const char *new_version,
               const char *expected_checksum,
               const char *checksum_algo);

void ota_save_version(const char *ver);
void ota_get_version(char *ver_out, size_t len);

#endif
