#include <unistd.h>
#include <string.h>
#include "esp_log.h"
#include "esp_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "tcpip_adapter.h"
// #include "esp_wifi.h"
#include "esp_event_loop.h"
#include "mdns.h"
#include "apps/sntp/sntp.h"

#include "esp32_hardware.h"

#include "esp_ota_ops.h"

#include "sim.h"
#include "simglb.h"

static const char* TAG = "esp32_netsrv";

EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

extern void start_net_services(void);
extern void stop_net_services(void);

static void start_services(void)
{
    update_status(0x08, STATUS_FLASH);
    sleep(1);

    char *mdns_instance;
    if((mdns_instance=getenv("HOSTNAME")) != NULL) {
        ESP_LOGI(TAG, "Initializing hostname to: %s", mdns_instance);
        ESP_ERROR_CHECK(tcpip_adapter_set_hostname(netIface, mdns_instance));

        //initialize mDNS
        ESP_LOGI(TAG, "Initializing MDNS");
        ESP_ERROR_CHECK( mdns_init() );
        //set hostname
        ESP_ERROR_CHECK( mdns_hostname_set(mdns_instance) );
        //set default instance
        ESP_ERROR_CHECK( mdns_instance_name_set("IMSAI 8080 Replica") );

        ESP_ERROR_CHECK( mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0) );
        ESP_ERROR_CHECK( mdns_service_instance_name_set("_http", "_tcp", "IMSAI 8080 Console") );
    }

    char *ntp_server;
    if((ntp_server=getenv("NTP_SERVER")) != NULL) {
        ESP_LOGI(TAG, "Initializing SNTP");
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, ntp_server);
        sntp_init();
    }

    start_net_services();
}

static void stop_services()
{
    mdns_free();

    if(sntp_enabled()) sntp_stop();

    stop_net_services();
    /*todo: there is no way to stop/close esphttpd */
    // if(httpd == InitializationSuccess)  httpdShutdown(&httpI.httpdInstance);

}

esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    ESP_LOGD(TAG, "EVENT: %d", event->event_id);
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_AP_START:
        ESP_LOGI(TAG, "soft-AP Started");
        start_services();
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        start_services();
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        stop_services();
        // wsStopVIO(NULL);
        // wsStopLPT(NULL);
        // wsStopSIO(NULL);
        // wsStopCPA(NULL);
        // esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    mdns_handle_system_event(ctx, event);
    return ESP_OK;
}

void start_wifi_services(void) {

    int timeout_ms = 10000;
    char *ssid_str;

    wifi_config_t wifi_config;
    wifi_config_t wifi_config_sta = {
        .sta = {
       },
    };
    wifi_config_t wifi_config_ap = {
        .ap = {
            .ssid = "imsai",
            .password = "password",
            .ssid_len = 0,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .max_connection = 2
        }
    };
  
    if(netIface == ESP_IF_WIFI_AP) {
        wifi_config = wifi_config_ap;
        if((ssid_str=getenv("HOSTNAME")) != NULL) {
            strncpy((char *) wifi_config.ap.ssid, ssid_str, 31);
        }
        ESP_LOGI(TAG, "AP Mode - Using SSID: %s (HOSTNAME)", wifi_config.ap.ssid);

    } else {
        wifi_config = wifi_config_sta;
        if((ssid_str=getenv("SSID")) != NULL) {
            strncpy((char *)  wifi_config.sta.ssid, ssid_str, 31);
        }
        ESP_LOGI(TAG, "STA Mode - Using SSID: %s (SSID)", wifi_config.sta.ssid);
        if((ssid_str=getenv("PASSWORD")) != NULL) {
            strncpy((char *) wifi_config.sta.password, ssid_str, 63);
        }
    }
  
    ESP_ERROR_CHECK( esp_wifi_set_config(netIface, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
 
    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
            0, 1, timeout_ms / portTICK_PERIOD_MS);

    ESP_LOGD(TAG, "WiFi result is %d", bits);

    if(bits & CONNECTED_BIT) {
        update_status(0x20, STATUS_SET);
    } else {
        ESP_LOGE(TAG, "WiFi failed to connect - timeout");
        
        sleep(2);
        esp_deep_sleep(500000);
    }

 }