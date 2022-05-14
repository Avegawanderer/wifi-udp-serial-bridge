#include <string.h>
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"

#include <lwip/netdb.h>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#include "xfifo.h"

static const char *TAG = "wifi softAP";
static const char *TELEM_TAG = "Telemetry server";
static const char *CONFIG_TAG = "Config server";

#define ESP_WIFI_SSID      "esp32_wifi"
#define ESP_WIFI_PASS      "11112222"
#define ESP_WIFI_CHANNEL   1
#define MAX_STA_CONN       5

const uint8_t myIp[4] =     {192, 168, 1, 10};
const uint8_t gwIp[4] =     {192, 168, 1, 1};
const uint8_t netMask[4] =  {255, 255, 255, 0};

#define TELEMETRY_PORT      3151
#define CONFIG_PORT         3140

#define TELEMETRY_UART      UART_NUM_2
#define TELEMETRY_RX_PIN    16
#define TELEMETRY_TX_PIN    17

#define AAT_UART            UART_NUM_1
#define AAT_RX_PIN          18
#define AAT_TX_PIN          19

xFifo_t smartPortDownlinkFifo;      // R9M -> UART -> UDP -> Ground Station
//xFifo_t smartPortUplinkFifo;        // Ground Station -> UDP -> UART -> R9M (not used at the moment)
//
//xFifo_t configDownlinkFifo;         // AAT -> UART -> UDP -> Configurator
//xFifo_t configUplinkFifo;           // Configurator -> UDP -> UART -> AAT

#define AAT_CONFIG_TIMEOUT  2000    // When configuration becomes active, telemetry stream is disabled for this time [ms]

int aatConfigMode;
int aatConfigModeTimer;



static void setupTelemetryUart(void)
{
    const uart_port_t uart_num = TELEMETRY_UART;
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, TELEMETRY_TX_PIN, TELEMETRY_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Setup UART buffered IO with event queue
    const int uart_buffer_size = (1024 * 2);
    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size, uart_buffer_size, 10, 0, 0));
}


static void setupAatUart(void)
{
    const uart_port_t uart_num = AAT_UART;
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, AAT_TX_PIN, AAT_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Setup UART buffered IO with event queue
    const int uart_buffer_size = (1024 * 2);
    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size, uart_buffer_size, 10, 0, 0));
}


static void activity_indication_task(void *pvParameters)
{
    // Indication:
    //  1. Telemetry input
    //  2. AAT UART mode: telemetry / configuration
    //  2. AAT UART TX
    //  3. AAT UART RX
    while(1)
    {
        vTaskDelay(3 / portTICK_PERIOD_MS);

    }
}


static void indicate_activity(int source)
{

}


static void set_static_indication(int source, int state)
{

}


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}


static void telemetry_server_task(void *pvParameters)
{
    int err;
    int len;
    const int bufSize = 256;
    char tmpBuffer[bufSize];
    char addrStr[128];

//    struct sockaddr_in bindAddr;
//    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
//    bindAddr.sin_family = AF_INET;
//    bindAddr.sin_port = htons(PORT);

    struct sockaddr_in bindAddr;
    bindAddr.sin_addr.s_addr = htonl(LWIP_MAKEU32(myIp[0], myIp[1], myIp[2], myIp[3]));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(TELEMETRY_PORT);

    struct sockaddr_in bcastAddr;
    bcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    bcastAddr.sin_family = AF_INET;
    bcastAddr.sin_port = htons(TELEMETRY_PORT);

//    struct sockaddr_in bcastAddr;
//    bcastAddr.sin_addr.s_addr = htonl(LWIP_MAKEU32(192,168,4,200));
//    bcastAddr.sin_family = AF_INET;
//    bcastAddr.sin_port = htons(PORT);

    struct sockaddr_storage sourceAddr;        // Large enough for both IPv4 or IPv6
    socklen_t socklen = sizeof(sourceAddr);

    while(1)
    {
        // Create
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0)
        {
            ESP_LOGE(TELEM_TAG, "Unable to create socket: errno %d", errno);
            continue;
        }

        // Set options
        int bcastEnabled = 1;
        err = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bcastEnabled, sizeof(bcastEnabled));
        if (err < 0)
        {
            ESP_LOGI(TELEM_TAG, "Broadcast permission failed");
            continue;
        }

        // Bind
        err = bind(sock, (struct sockaddr*) &bindAddr, sizeof(bindAddr));
        if (err < 0)
        {
            ESP_LOGE(TELEM_TAG, "Socket unable to bind: errno %d", errno);
            continue;
        }

        // Ready
        ESP_LOGI(TELEM_TAG, "Socket created and bound, port %d", TELEMETRY_PORT);
        while (1)
        {
            // Downink (to PC)
            uint32_t availCnt = xFifo_DataAvaliable(&smartPortDownlinkFifo);
            //int availCnt = 0;
            //uart_get_buffered_data_len(TELEMETRY_UART, (size_t*)&availCnt);
            if (availCnt > 0)
            {
                len = (availCnt > bufSize) ? bufSize : availCnt;
                xFifo_Get(&smartPortDownlinkFifo, tmpBuffer, len);
                //uart_read_bytes(TELEMETRY_UART, tmpBuffer, len, 0);
                err = sendto(sock, tmpBuffer, len, 0, (struct sockaddr*) &bcastAddr, sizeof(bcastAddr));
                if (err < 0)
                {
                    ESP_LOGE(TELEM_TAG, "Error occurred during sending: errno %d", errno);
                }
                else
                {
                    ESP_LOGI(TELEM_TAG, "downlink %u bytes", len);
                }
            }

            // Uplink (from PC)
            len = recvfrom(sock, tmpBuffer, sizeof(tmpBuffer) - 1, MSG_DONTWAIT, (struct sockaddr*) &sourceAddr, &socklen);
            if (len > 0)
            {
                // Data received
                // Get the sender's ip address as string
                if (sourceAddr.ss_family != PF_INET)
                {
                    ESP_LOGE(TELEM_TAG, "IPv6 is not supported");
                    continue;
                }
                inet_ntoa_r(((struct sockaddr_in* )&sourceAddr)->sin_addr, addrStr, sizeof(addrStr) - 1);

                tmpBuffer[len] = 0; // Null-terminate whatever we received and treat like a string...
                ESP_LOGI(TELEM_TAG, "Received %d bytes from %s:", len, addrStr);
                ESP_LOGI(TELEM_TAG, "%s", tmpBuffer);

                // Echo for now
                err = sendto(sock, tmpBuffer, len, 0, (struct sockaddr*) &sourceAddr, sizeof(sourceAddr));
                if (err < 0)
                {
                    ESP_LOGE(TELEM_TAG, "Error occurred during sending: errno %d", errno);
                }
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
    }
    vTaskDelete(NULL);
}


static void config_server_task(void *pvParameters)
{
    int err;
    int len;
    const int bufSize = 256;
    char tmpBuffer[bufSize];
    char addrStr[128];
    int isClientAddrKnown = 0;

    struct sockaddr_in bindAddr;
    bindAddr.sin_addr.s_addr = htonl(LWIP_MAKEU32(myIp[0], myIp[1], myIp[2], myIp[3]));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(CONFIG_PORT);

    struct sockaddr_storage clientAddr;        // Large enough for both IPv4 or IPv6
    socklen_t socklen = sizeof(clientAddr);

    while(1)
    {
        // Create
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0)
        {
            ESP_LOGE(CONFIG_TAG, "Unable to create socket: errno %d", errno);
            continue;
        }

        // Set options
//        int bcastEnabled = 1;
//        err = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bcastEnabled, sizeof(bcastEnabled));
//        if (err < 0)
//        {
//            ESP_LOGI(TAG, "Broadcast permission failed");
//            continue;
//        }

        // Bind
        err = bind(sock, (struct sockaddr*) &bindAddr, sizeof(bindAddr));
        if (err < 0)
        {
            ESP_LOGE(CONFIG_TAG, "Socket unable to bind: errno %d", errno);
            continue;
        }

        // Ready
        ESP_LOGI(CONFIG_TAG, "Socket created and bound, port %d", CONFIG_PORT);
        while (1)
        {
            // Downlink (to PC)
            if (isClientAddrKnown)
            {
                //uint32_t availCnt = xFifo_DataAvaliable(&configDownlinkFifo);
                int availCnt = 0;
                uart_get_buffered_data_len(AAT_UART, (size_t*)&availCnt);
                if (availCnt > 0)
                {
                    len = (availCnt > bufSize) ? bufSize : availCnt;
                    uart_read_bytes(AAT_UART, tmpBuffer, len, 0);
                    err = sendto(sock, tmpBuffer, len, 0, (struct sockaddr*) &clientAddr, sizeof(clientAddr));
                    if (err < 0)
                    {
                        ESP_LOGE(CONFIG_TAG, "Error occurred during sending: errno %d", errno);
                    }
                    else
                    {
                        ESP_LOGI(CONFIG_TAG, "downlink %u bytes", len);
                    }
                    // Disable telemetry UART sending to AAT
                    aatConfigMode = 1;
                    aatConfigModeTimer = AAT_CONFIG_TIMEOUT;
                }
            }
            else
            {
                //xFifo_Clear(&configDownlinkFifo);
                uart_flush_input(AAT_UART);
            }

            // Uplink (from PC)
            //if (xFifo_FreeSpace(&configUplinkFifo) >= bufSize)
            if (1)
            {
                len = recvfrom(sock, tmpBuffer, sizeof(tmpBuffer) - 1, MSG_DONTWAIT, (struct sockaddr*) &clientAddr, &socklen);
                if (len > 0)
                {
                    // Data received
                    // Get the sender's ip address as string
                    if (clientAddr.ss_family != PF_INET)
                    {
                        ESP_LOGE(CONFIG_TAG, "IPv6 is not supported");
                        continue;
                    }
                    ESP_LOGI(CONFIG_TAG, "uplink %u bytes", len);
                    if (!isClientAddrKnown)
                    {
                        isClientAddrKnown = 1;
                        inet_ntoa_r(((struct sockaddr_in* )&clientAddr)->sin_addr, addrStr, sizeof(addrStr) - 1);
                        ESP_LOGI(CONFIG_TAG, "Client address: %s", addrStr);
                    }
                    // Disable telemetry UART sending to AAT
                    aatConfigMode = 1;
                    aatConfigModeTimer = AAT_CONFIG_TIMEOUT;

                    //xFifo_Put(&configUplinkFifo, tmpBuffer, len);
                    uart_write_bytes(AAT_UART, tmpBuffer, len);


                }
            }

            // Process configuration timeout
            if (aatConfigMode)
            {
                aatConfigModeTimer -= 5;
                if (aatConfigModeTimer <= 0)
                {
                    aatConfigMode = 0;
                }
            }

            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
    }
    vTaskDelete(NULL);
}


static void telemetry_mux_task(void *pvParameters)
{
    const int bufSize = 256;
    uint8_t tmpBuffer[bufSize];
    uint8_t i = 0;

    while(1)
    {
        vTaskDelay(3 / portTICK_PERIOD_MS);

        // Provide telemetry to different sinks
        int availCnt = 0;
        uart_get_buffered_data_len(TELEMETRY_UART, (size_t*)&availCnt);
        if (availCnt > 0)
        {
            int len = (availCnt > bufSize) ? bufSize : availCnt;
            uart_read_bytes(TELEMETRY_UART, tmpBuffer, len, 0);

            // Output to PC
            xFifo_Put(&smartPortDownlinkFifo, tmpBuffer, len);

            // Output to AAT UART (disabled during configuration of AAT)
            if (aatConfigMode == 0)
            {
                uart_write_bytes(AAT_UART, tmpBuffer, len);
            }
        }
    }
}


void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    xFifo_Create(&smartPortDownlinkFifo, sizeof(uint8_t), 2048);
    //xFifo_Create(&smartPortUplinkFifo, sizeof(uint8_t), 1024);

    setupTelemetryUart();
    setupAatUart();

    aatConfigMode = 0;
    aatConfigModeTimer = 0;

    //-----------------------------------------------------------------------//

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");

    esp_netif_t *netif = esp_netif_create_default_wifi_ap();

    // Setting static IP
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(netif));
    esp_netif_ip_info_t ip_info;

    IP4_ADDR(&ip_info.ip, myIp[0], myIp[1], myIp[2], myIp[3]);
    IP4_ADDR(&ip_info.gw, gwIp[0], gwIp[1], gwIp[2], gwIp[3]);
    IP4_ADDR(&ip_info.netmask, netMask[0], netMask[1], netMask[2], netMask[3]);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
    //ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));              // If sending socket is bound to INADDR_ANY and DHCP is disabled,
                                                                  // sending broadcast packets to 255.255.255.255 fails with EHOSTUNREACH (errno 118)

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_WIFI_SSID,
            .ssid_len = strlen(ESP_WIFI_SSID),
            .channel = ESP_WIFI_CHANNEL,
            .password = ESP_WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             ESP_WIFI_SSID, ESP_WIFI_PASS, ESP_WIFI_CHANNEL);

    //-----------------------------------------------------------------------//

    // app_main() has priority of ESP_TASK_PRIO_MIN + 1 ( = 1)

    xTaskCreate(telemetry_server_task, "telemetry_server", 4096, 0, 4, NULL);
    xTaskCreate(config_server_task, "config_server", 4096, 0, 5, NULL);
    xTaskCreate(telemetry_mux_task, "telemetry_mux", 4096, 0, 6, NULL);        // Must have priority higher than config_server
    xTaskCreate(activity_indication_task, "indication", 1024, 0, 2, NULL);

    //-----------------------------------------------------------------------//

    //uint8_t i = 0;
    //char tmpBuffer[20];

    //zero-initialize the config structure.
	gpio_config_t io_conf = {};
	//disable interrupt
	io_conf.intr_type = GPIO_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_SEL_2;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);

    while(1)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_2, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_2, 0);

        // Test output to PC
        //sprintf(tmpBuffer, "Test data %d", i++);
        //xFifo_Put(&smartPortDownlinkFifo, tmpBuffer, strlen(tmpBuffer));
    }
}

