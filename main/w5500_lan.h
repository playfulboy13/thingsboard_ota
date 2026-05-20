#ifndef _W5500_H
#define _W5500_H

#include "main.h"

#include "driver/spi_master.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_netif_ip_addr.h"
#include "esp_mac.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#include "mqtt.h"

/* PIN */
#define MISO_GPIO   19
#define MOSI_GPIO   23
#define SCK_GPIO    18
#define CS_GPIO      5
#define INT_GPIO    33
#define W5500_RST   14

#define ETH_SPI_HOST SPI3_HOST
#define SPI_CLOCK_MHZ 10

typedef enum {
    NET_NONE = 0,
    NET_ETH,
    NET_WIFI
} net_type_t;

extern net_type_t active_net;




bool ethernet_is_connected(void);
void w5500_init(void);
void network_set_active(net_type_t new_net);

#endif
