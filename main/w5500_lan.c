#include "w5500_lan.h"

/* ================= HANDLE ================= */

static esp_eth_handle_t eth_handle = NULL;
static esp_netif_t *eth_netif = NULL;

/* ================= STATE FLAG ================= */

static volatile bool eth_link_up = false;
static volatile bool eth_has_ip = false;

/* tránh tạo nhiều lần */
static bool mqtt_started = false;
static bool rtc_started = false;


/* ============================================================
   EVENT HANDLER
   ============================================================ */

static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {

    case ETHERNET_EVENT_CONNECTED:

        eth_link_up = true;

        ESP_LOGI(TAG, "Ethernet Link Up");

        eth_speed_t speed;
        eth_duplex_t duplex;

        if (esp_eth_ioctl(eth_handle, ETH_CMD_G_SPEED, &speed) == ESP_OK)
        {
            ESP_LOGI(TAG, "Speed: %s", speed == ETH_SPEED_100M ? "100 Mbps" : "10 Mbps");
        }

        if (esp_eth_ioctl(eth_handle, ETH_CMD_G_DUPLEX_MODE, &duplex) == ESP_OK)
        {
            ESP_LOGI(TAG, "Duplex: %s", duplex == ETH_DUPLEX_FULL ? "FULL" : "HALF");
        }

        break;


    case ETHERNET_EVENT_DISCONNECTED:

        ESP_LOGW(TAG, "Ethernet Link Down");

        eth_link_up = false;
        eth_has_ip = false;

        ESP_LOGW(TAG, "Restarting Ethernet...");

        esp_eth_stop(eth_handle);

        vTaskDelay(pdMS_TO_TICKS(1000));

        esp_eth_start(eth_handle);

        break;


    case ETHERNET_EVENT_START:

        ESP_LOGI(TAG, "Ethernet Started");

        break;


    case ETHERNET_EVENT_STOP:

        ESP_LOGI(TAG, "Ethernet Stopped");

        eth_link_up = false;
        eth_has_ip = false;

        break;


    default:
        break;
    }
}


/* ============================================================
   GOT IP EVENT
   ============================================================ */

static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

    if (event->esp_netif != eth_netif) return;

    eth_has_ip = true;

    ESP_LOGI(TAG, "ETH IP: " IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "MASK: " IPSTR, IP2STR(&event->ip_info.netmask));
    ESP_LOGI(TAG, "GW: " IPSTR, IP2STR(&event->ip_info.gw));


    /* ===== MQTT START ONCE ===== */

    if (!mqtt_started)
    {
        ESP_LOGI(TAG, "Starting MQTT...");
        mqtt_app_start();
        mqtt_started = true;
    }


    /* ===== RTC TASK START ONCE ===== */

    if (!rtc_started)
    {
        ESP_LOGI(TAG, "Starting RTC task...");
        xTaskCreate(rtc_task, "rtc_task", 4096, NULL, 5, NULL);
        rtc_started = true;
    }

}



/* ============================================================
   INIT
   ============================================================ */

void w5500_init(void)
{

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    gpio_install_isr_service(0);



/* ================= RESET CHIP ================= */

    gpio_config_t rst_cfg =
    {
        .pin_bit_mask = 1ULL << W5500_RST,
        .mode = GPIO_MODE_OUTPUT,
    };

    gpio_config(&rst_cfg);

    gpio_set_level(W5500_RST, 0);

    vTaskDelay(pdMS_TO_TICKS(100));

    gpio_set_level(W5500_RST, 1);

    vTaskDelay(pdMS_TO_TICKS(300));



/* ================= SPI BUS ================= */

    spi_bus_config_t buscfg =
    {
        .miso_io_num = MISO_GPIO,
        .mosi_io_num = MOSI_GPIO,
        .sclk_io_num = SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };


    ESP_ERROR_CHECK(spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));



/* ================= SPI DEVICE ================= */

    spi_device_interface_config_t devcfg =
    {
        .command_bits = 16,
        .address_bits = 8,
        .mode = 0,

        .clock_speed_hz = (SPI_CLOCK_MHZ > 15 ? 15 : SPI_CLOCK_MHZ) * 1000 * 1000,

        .spics_io_num = CS_GPIO,

        .queue_size = 10,

    };



/* ================= MAC + PHY ================= */

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    phy_config.reset_gpio_num = -1;



    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &devcfg);

    w5500_config.int_gpio_num = INT_GPIO;



    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);

    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);



    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);



    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));



/* ================= SET MAC ================= */

    uint8_t mac_addr[6] = {0x02,0x12,0x34,0x56,0x78,0x9A};

    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr));



/* ================= NETIF ================= */

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();

    eth_netif = esp_netif_new(&cfg);

    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));



/* ================= EVENT ================= */

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_event_handler, NULL));



/* ================= START ================= */

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));



    ESP_LOGI(TAG,"W5500 Ethernet Ready (DHCP)");
}



/* ============================================================
   API
   ============================================================ */

bool ethernet_is_connected(void)
{
    return (eth_link_up && eth_has_ip);
}
