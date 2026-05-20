idf_component_register(SRCS "edge_controller.c" "w5500_lan.c" "main.c" "mqtt.c" "w5500_lan.h" "lora_uart.c" "ds3231.c" "user_task.c" "ota.c"
"edge_controller.c"
INCLUDE_DIRS "."
REQUIRES driver freertos esp_wifi nvs_flash json mqtt  esp_eth 
esp_netif
esp_event
esp_http_server
driver
spi_flash
esp_http_server
esp_https_ota 
app_update
EMBED_TXTFILES "hivemq_root_ca.pem" "ca.crt" "isrgrootx1.pem") 