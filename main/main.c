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
#include "config.h"
#include "drv_led.h"

static const char *TAG = "WiFi softAP";
static const char *TELEM_TAG = "Telemetry server";
static const char *CONFIG_TAG = "Config server";

const uint8_t myIp[4] = IP_ADDR_MY;
const uint8_t gwIp[4] = IP_ADDR_GW;
const uint8_t netMask[4] = NET_MASK;

xFifo_t smartPortDownlinkFifo;      // R9M -> UART -> UDP -> Ground Station
//xFifo_t smartPortUplinkFifo;        // Ground Station -> UDP -> UART -> R9M (not used at the moment)
//
//xFifo_t configDownlinkFifo;         // AAT -> UART -> UDP -> Configurator
//xFifo_t configUplinkFifo;           // Configurator -> UDP -> UART -> AAT

int aatConfigMode;
int aatConfigModeTimer;
int telemetryTimeoutTimer;

// LED indication types
typedef enum {
    LedIndic_Off,
    LedIndic_On,
    LedIndic_Blink,
} LedIndication;

#define LED_INDIC_FSM_CALL_PERIOD       2      // [ms]
#define TELEMETRY_TIMEOUT               1000    // [s]

static struct ldi_s {
    struct {
        LedIndication indicationType;
        uint8_t numRepeats;
        uint16_t timeOn;
        uint16_t timeOff;
        uint8_t state;
        uint16_t tmr;
    } leds[LedCount];
} ldi, altLdi;
static bool isAltLdi;



/**
    @brief  Provide indication by LEDs
    @param[in]  led Led to use
    @param[in]  indicationType Desired indication type
    @param[in]  timeOn Time [ms] for ON state (blink indication type only). May be 0 for ON or OFF indication
    @param[in]  timeOff Time [ms] for OFF state (blink indication type only). May be 0 for ON or OFF indication
    @param[in]  numRepeats Number of blinks. If set to 0, LED will blink until other indication type is set.
                Otherwise LED will blink numRepeats times and then switch to OFF state
    @return None
*/
void putLedIndication(Leds led, LedIndication indicationType, uint16_t timeOn, uint16_t timeOff, uint8_t numRepeats)
{
    ldi.leds[led].indicationType = indicationType;
    ldi.leds[led].numRepeats = numRepeats;
    ldi.leds[led].timeOn = timeOn / LED_INDIC_FSM_CALL_PERIOD;
    ldi.leds[led].timeOff = timeOff / LED_INDIC_FSM_CALL_PERIOD;
    ldi.leds[led].state = 0;
    ldi.leds[led].tmr = 1;      // Update state on next FSM call

    if (indicationType == LedIndic_On)
    {
        ldi.leds[led].state = 1;
        if (!isAltLdi)
            drvLed_Set(led, On);
    }
    else if (indicationType == LedIndic_Off)
    {
        ldi.leds[led].state = 0;
        if (!isAltLdi)
            drvLed_Set(led, Off);
    }
    else
    {
        // Blink will be processed by FSM
    }
}


/**
    @brief  Provide alternative indication by LEDs
    @param[in]  led Led to use
    @param[in]  indicationType Desired indication type
    @param[in]  timeOn Time [ms] for ON state (blink indication type only). May be 0 for ON or OFF indication
    @param[in]  timeOff Time [ms] for OFF state (blink indication type only). May be 0 for ON or OFF indication
    @param[in]  numRepeats Number of blinks. If set to 0, LED will blink until other indication type is set.
                Otherwise LED will blink numRepeats times and then switch to main indication
    @return None
*/
void putAltLedIndication(Leds led, LedIndication indicationType, uint16_t timeOn, uint16_t timeOff, uint8_t numRepeats)
{
    altLdi.leds[led].indicationType = indicationType;
    altLdi.leds[led].numRepeats = numRepeats;
    altLdi.leds[led].timeOn = timeOn / LED_INDIC_FSM_CALL_PERIOD;
    altLdi.leds[led].timeOff = timeOff / LED_INDIC_FSM_CALL_PERIOD;
    altLdi.leds[led].state = 0;
    altLdi.leds[led].tmr = 1;       // Update state on next FSM call
    if (indicationType == LedIndic_On)
    {
        drvLed_Set(led, On);
        isAltLdi = true;                // Switch to alternative structure
    }
    else if (indicationType == LedIndic_Off)
    {
        drvLed_Set(led, (ldi.leds[led].state) ? On : Off);
        isAltLdi = false;               // Switch to main structure
    }
    else
    {
        isAltLdi = true;                // Switch to alternative structure
        // Blink will be processed by FSM
    }
}


/**
    @brief  Process indication by LEDs
            Must be called periodically
    @param doApply if set to True, LED driver is set to LED state
    @return None
*/
static void ProcessLedIndication(bool doApply)
{
    uint8_t i;
    struct ldi_s *pLdi = (isAltLdi) ? &altLdi : &ldi;

    for (i = 0; i < LedCount; i++)
    {
        if ((pLdi->leds[i].indicationType == LedIndic_Blink))
        {
            if (--pLdi->leds[i].tmr == 0)
            {
                while (1)
                {
                    if (pLdi->leds[i].state == 0)
                    {
                        drvLed_Set((Leds)i, On);
                        pLdi->leds[i].state = 1;
                        pLdi->leds[i].tmr = pLdi->leds[i].timeOn;
                        break;
                    }
                    else if (pLdi->leds[i].state == 1)
                    {
                        drvLed_Set((Leds)i, Off);
                        pLdi->leds[i].state = 2;
                        pLdi->leds[i].tmr = pLdi->leds[i].timeOff;
                        break;
                    }
                    else // (pLdi->leds[i].state == 2)
                    {
                        if ((pLdi->leds[i].numRepeats > 0) && (--pLdi->leds[i].numRepeats == 0))
                        {
                            pLdi->leds[i].indicationType = LedIndic_Off;
                            drvLed_Set((Leds)i, Off);
                            pLdi->leds[i].state = 0;
                            if (isAltLdi)
                            {
                                isAltLdi = false;
                                ProcessLedIndication(true);
                            }
                            break;
                        }
                        else
                        {
                            pLdi->leds[i].state = 0;
                        }
                    }
                }
            }
        }
        if (doApply)
        {
            drvLed_Set((Leds)i, (pLdi->leds[i].state) ? On : Off);
        }
    }
}


static void activity_indication_task(void *pvParameters)
{
    drvLed_Init();
    while(1)
    {
        ProcessLedIndication(false);
        vTaskDelay(LED_INDIC_FSM_CALL_PERIOD / portTICK_PERIOD_MS);
    }
}


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
                    if (aatConfigMode == 0)
                    {
                        aatConfigMode = 1;
                        putLedIndication(AatModeTelemLed, LedIndic_Off, 0, 0, 0);
                        putLedIndication(AatModeConfigLed, LedIndic_On, 0, 0, 0);
                    }
                    putAltLedIndication(AatModeConfigLed, LedIndic_Blink, 10, 40, 1);
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
                    if (aatConfigMode == 0)
                    {
                        aatConfigMode = 1;
                        putLedIndication(AatModeTelemLed, LedIndic_Off, 0, 0, 0);
                        putLedIndication(AatModeConfigLed, LedIndic_On, 0, 0, 0);
                    }
                    putAltLedIndication(AatModeConfigLed, LedIndic_Blink, 10, 40, 1);
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
                    putLedIndication(AatModeTelemLed, LedIndic_On, 0, 0, 0);
                    putLedIndication(AatModeConfigLed, LedIndic_Off, 0, 0, 0);
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

            // Indicate
            telemetryTimeoutTimer = 0;
            putAltLedIndication(TelemLed, LedIndic_Blink, 10, 40, 1);

            // Output to PC
            xFifo_Put(&smartPortDownlinkFifo, tmpBuffer, len);

            // Output to AAT UART (disabled during configuration of AAT)
            if (aatConfigMode == 0)
            {
                putAltLedIndication(AatModeTelemLed, LedIndic_Blink, 10, 40, 1);
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
    telemetryTimeoutTimer = 0;

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
#if ENA_DHCP == 1
    ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));              // If sending socket is bound to INADDR_ANY and DHCP is disabled,
                                                                // sending broadcast packets to 255.255.255.255 fails with EHOSTUNREACH (errno 118)
#endif

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
    xTaskCreate(activity_indication_task, "indication", 4096, 0, 2, NULL);

    //-----------------------------------------------------------------------//

    vTaskDelay(100 / portTICK_PERIOD_MS);
    putLedIndication(AatModeTelemLed, LedIndic_On, 0, 0, 0);
    putLedIndication(TelemLed, LedIndic_Blink, 1000, 1000, 0);

    //uint8_t i = 0;
    //char tmpBuffer[20];

    while(1)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        if (telemetryTimeoutTimer < TELEMETRY_TIMEOUT)
        {
            telemetryTimeoutTimer += 100;
            if (telemetryTimeoutTimer >= TELEMETRY_TIMEOUT)
            {
                //putLedIndication(TelemLed, LedIndic_Blink, 1000, 1000, 0);
            }
        }


        //gpio_set_level(GPIO_NUM_2, 1);
        //vTaskDelay(500 / portTICK_PERIOD_MS);
        //gpio_set_level(GPIO_NUM_2, 0);

        // Test output to PC
        //sprintf(tmpBuffer, "Test data %d", i++);
        //xFifo_Put(&smartPortDownlinkFifo, tmpBuffer, strlen(tmpBuffer));
    }
}

