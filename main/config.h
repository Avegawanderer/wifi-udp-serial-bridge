#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "driver/gpio.h"
#include "driver/uart.h"


#define ESP_WIFI_SSID      "esp32_wifi"
#define ESP_WIFI_PASS      "11112222"
#define ESP_WIFI_CHANNEL   1
#define MAX_STA_CONN       5

#define IP_ADDR_MY                  {192, 168, 1, 10}
#define IP_ADDR_GW                  {192, 168, 1, 1}
#define NET_MASK                    {255, 255, 255, 0}
#define ENA_DHCP                    1

#define TELEMETRY_PORT              3151
#define CONFIG_PORT                 3140

#define TELEMETRY_UART              UART_NUM_2
#define TELEMETRY_RX_PIN            16
#define TELEMETRY_TX_PIN            17

#define AAT_UART                    UART_NUM_1
#define AAT_RX_PIN                  18
#define AAT_TX_PIN                  19

#define TELEM_LED_PIN               GPIO_NUM_2      // Blinks when telemetry data (any) is coming from telemetry UART
#define AAT_TELEM_MODE_LED_PIN      GPIO_NUM_27     // Active when AAT UART is in telemetry mode (receives telemetry)
#define AAT_CONFIG_MODE_LED_PIN     GPIO_NUM_25     // Active when AAT UART is in configurator mode (data exchange with UDP CONFIG_PORT)

#define AAT_CONFIG_TIMEOUT          2000    // When configuration becomes active, telemetry stream is disabled for this time [ms]




#endif  // __CONFIG_H__
