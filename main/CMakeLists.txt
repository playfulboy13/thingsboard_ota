idf_component_register(SRCS "relay_uart.c" "relay_driver.c" "gateway_doicao.c" "user_task.c" "modbus_master.c" "relay_driver.c"
"relay_uart.c" "oled_ssd1306.c" "mqtt.c" "ota.c"
INCLUDE_DIRS "."
REQUIRES driver freertos mqtt esp_wifi nvs_flash json ssd1306  esp_http_server app_update esp_https_ota spi_flash
EMBED_TXTFILES "hivemq_root_ca.pem" )