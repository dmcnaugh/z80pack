#include <unistd.h>
#include "esp_log.h"
#include "esp_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "tcpip_adapter.h"
// #include "esp_wifi.h"
#include "esp_event_loop.h"
#include "mdns.h"
#include "apps/sntp/sntp.h"
#include "libesphttpd/esp.h"
#include "libesphttpd/httpd.h"
#include "libesphttpd/httpd-freertos.h"
#include "libesphttpd/cgiredirect.h"
#include "libesphttpd/cgiwebsocket.h"
#include "libesphttpd/cgiflash.h"

#include "esp32_hardware.h"

#include "esp_ota_ops.h"

#include "sim.h"
#include "simglb.h"

static const char* TAG = "esp32_netsrv";

EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

extern CgiStatus cgiEspVfsHook(HttpdConnData *connData);
extern CgiStatus cgiEspVfsUpload(HttpdConnData *connData);
extern CgiStatus cgiEspVfsDir(HttpdConnData *connData);
extern CgiStatus cgiDisks(HttpdConnData *connData);
extern CgiStatus cgiLibrary(HttpdConnData *connData);
static CgiStatus cgiSystem(HttpdConnData *connData);
static CgiStatus cgiTasks(HttpdConnData *connData);

static TaskHandle_t wsSend = NULL;
static TaskHandle_t wsPrint = NULL;
static TaskHandle_t wsRefresh = NULL;

extern void wsRefreshTask();

QueueHandle_t sendQueue = NULL;
QueueHandle_t printQueue = NULL;
QueueHandle_t recvQueue = NULL;
QueueHandle_t VIOQueue = NULL;
static Websock *xtermWs = NULL;
static Websock *printerWs = NULL;

static void wsConnSIO(Websock *ws);
static void wsConnLPT(Websock *ws);
static void wsConnVIO(Websock *ws);
static void wsConnCPA(Websock *ws);

#define MAX_CONNECTIONS 14
static char connectionMemory[sizeof(RtosConnType) * MAX_CONNECTIONS];
static HttpdFreertosInstance httpI;
static HttpdInitStatus httpd;

static const CgiUploadFlashDef flashDef;

const HttpdBuiltInUrl builtInUrls[]={
    {"/", cgiRedirect, "/console/", NULL},
    {"/sio1", cgiWebsocket, wsConnSIO, NULL},
    {"/lpt", cgiWebsocket, wsConnLPT, NULL},
    {"/vio", cgiWebsocket, wsConnVIO, NULL},
    {"/cpa", cgiWebsocket, wsConnCPA, NULL},
    {"/disks", cgiDisks, NULL, NULL},
    {"/cfg", cgiEspVfsUpload, "/sdcard/imsai/conf", NULL},
    {"/console", cgiEspVfsUpload, "/sdcard/www/console", NULL},
    {"/printer", cgiEspVfsUpload, "/sdcard/www/printer", NULL},
    {"/libbox", cgiEspVfsUpload, DISKSDIR, NULL},
    {"/library", cgiLibrary, NULL, NULL},
    {"/system", cgiSystem, NULL, NULL},
    {"/tasks", cgiTasks, NULL, NULL},
    {"/flash", cgiUploadFirmware, &flashDef, NULL},
    {"/conf", cgiEspVfsDir, "/sdcard/imsai", NULL},
    {"/imsai/*", cgiEspVfsHook, "/sdcard", NULL},
    {"*", cgiEspVfsHook, "/sdcard/www", NULL},
	{NULL, NULL, NULL, NULL}
};

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

    char *port_str;
    int http_port = 3000;
    if((port_str=getenv("PORT")) != NULL) {
        http_port = atoi(port_str);
    }
    ESP_LOGI(TAG, "Using HTTP Port: %d [%s]", http_port, port_str);

    httpd = httpdFreertosInit(&httpI,
	                  builtInUrls,
	                  http_port,
	                  connectionMemory,
	                  MAX_CONNECTIONS,
	                  HTTPD_FLAG_NONE);
    if(httpd != InitializationSuccess) {
        ESP_LOGE(TAG, "Failed to start httpd [err: %d]", httpd);
    } else {
        update_status(0x08, STATUS_SET);
    }
    
}

static void stop_services()
{
    mdns_free();

    if(sntp_enabled()) sntp_stop();

    /*todo: there is no way to stop/close esphttpd */
    // if(httpd == InitializationSuccess)  httpdShutdown(&httpI.httpdInstance);

}

static void wsRecvCallback(Websock *ws, char *data, int len, int flags) {
    int i=0;
    while(i<len) {
        char item = (char) data[i++];
        ESP_LOGD(__func__, "WS RECV: [%d] %d", item, len);
        if(recvQueue != NULL) xQueueSend(recvQueue, (void *) &item, portMAX_DELAY);   
    }
}

static void wsVIOCallback(Websock *ws, char *data, int len, int flags) {
    int i=0;
    while(i<len) {
        char item = (char) data[i++];
        ESP_LOGD(__func__, "WS RECV: [%d] %d", item, len);
        if(VIOQueue != NULL) xQueueSend(VIOQueue, (void *) &item, portMAX_DELAY);   
    }
}

extern void net_switches(char *);
extern void update_cpa(bool);

void cpaSend(char *data) {
    cgiWebsockBroadcast(&httpI.httpdInstance, "/cpa", data, 6, WEBSOCK_FLAG_NONE);
}

static void wsCPACallback(Websock *ws, char *data, int len, int flags) {
    if(len == 1 && data[0] == 'P') {
        ESP_LOGD(__func__, "CPA WS message [%d:%c]", len, data[0]);
        update_cpa(true);
        return;
    }
    if(len != 2) {
        ESP_LOGE(__func__, "CPA WS message length error [%d]", len);
        return;
    }
    net_switches(data);

}

void vioSend(char* msg, int len) {
    cgiWebsockBroadcast(&httpI.httpdInstance, "/vio", msg, len, WEBSOCK_FLAG_BIN);
}

static void wsSendTask() {
    while(true) {
        char data;
        xQueueReceive(sendQueue, &data, portMAX_DELAY);
        ESP_LOGD(__func__, "WS SEND: [%d]", data);
        cgiWebsockBroadcast(&httpI.httpdInstance, "/sio1", (char *) &data, 1, WEBSOCK_FLAG_NONE);
    };
}

static void wsPrintTask() {
    while(true) {
        char data;
        xQueueReceive(printQueue, &data, portMAX_DELAY);
        ESP_LOGD(__func__, "WS PRINT SEND: [%d]", data);
        cgiWebsockBroadcast(&httpI.httpdInstance, "/lpt", (char *) &data, 1, WEBSOCK_FLAG_NONE);
    };
}

static void wsStopSIO(Websock *ws) {
    if (wsSend != NULL) { vTaskDelete(wsSend); wsSend = NULL; };
    if (sendQueue != NULL) { vQueueDelete(sendQueue); sendQueue = NULL; };
    if (recvQueue != NULL) { vQueueDelete(recvQueue); recvQueue = NULL; };
    ESP_LOGI(__func__, "WS CLIENT CLOSED SIO");
}

static void wsConnSIO(Websock *ws) {
    ESP_LOGI(__func__, "WS CLIENT CONNECTED to SIO");
    ws->recvCb=wsRecvCallback;
    ws->closeCb=wsStopSIO;
    xtermWs = ws;
    cgiWebsocketSend(&httpI.httpdInstance, ws, "Connected to ESP32 IMSAI 8080 Simulation\n\r", 42, WEBSOCK_FLAG_NONE);
    sendQueue = xQueueCreate(96 , sizeof(char));
    if (sendQueue == NULL) ESP_LOGE(__func__, "WS SEND QUEUE CREATE FAILED");
    recvQueue = xQueueCreate(10, sizeof(char));
    if (recvQueue == NULL) ESP_LOGE(__func__, "WS RECV QUEUE CREATE FAILED");
    xTaskCreatePinnedToCore(wsSendTask, "wsSendTask", 2000, NULL, ESP_TASK_MAIN_PRIO + 5, &wsSend, 0);
    if (wsSend == NULL) ESP_LOGE(__func__, "WS SEND TASK CREATE FAILED");
}

static void wsStopLPT(Websock *ws) {
    if (wsPrint != NULL) { vTaskDelete(wsPrint); wsPrint = NULL; };
    if (printQueue != NULL) { vQueueDelete(printQueue); printQueue = NULL; };
    ESP_LOGI(__func__, "WS CLIENT CLOSED LPT");
}

static void wsConnLPT(Websock *ws) {
    ESP_LOGI(__func__, "WS CLIENT CONNECTED to LPT");
    printerWs = ws;
    ws->closeCb=wsStopLPT;
    printQueue = xQueueCreate(96 , sizeof(char));
    if (printQueue == NULL) ESP_LOGE(__func__, "WS PRINT QUEUE CREATE FAILED");
    xTaskCreatePinnedToCore(wsPrintTask, "wsPrintTask", 2000, NULL, ESP_TASK_MAIN_PRIO + 4, &wsPrint, 0);
    if (wsPrint == NULL) ESP_LOGE(__func__, "WS PRINT TASK CREATE FAILED");
}

static void wsStopVIO(Websock *ws) {
    if (wsRefresh != NULL) { vTaskDelete(wsRefresh); wsRefresh = NULL; };
    if (VIOQueue != NULL) { vQueueDelete(VIOQueue); VIOQueue = NULL; };
    ESP_LOGI(__func__, "WS CLIENT CLOSED VIO");
}

static void wsConnVIO(Websock *ws) {
    ESP_LOGI(__func__, "WS CLIENT CONNECTED to VIO");
    ws->recvCb=wsVIOCallback;
    ws->closeCb=wsStopVIO;
    VIOQueue = xQueueCreate(10, sizeof(char));
    if (VIOQueue == NULL) ESP_LOGE(__func__, "WS VIO QUEUE CREATE FAILED");
    xTaskCreatePinnedToCore(wsRefreshTask, "wsRefreshTask", 2000, NULL, ESP_TASK_MAIN_PRIO + 5, &wsRefresh, 0);
    if (wsRefresh == NULL) ESP_LOGE(__func__, "WS VIO REFREASH TASK CREATE FAILED");
}

static void wsStopCPA(Websock *ws) {
    ESP_LOGI(__func__, "WS CLIENT CLOSED CPA");
}

static void wsConnCPA(Websock *ws) {
    ESP_LOGI(__func__, "WS CLIENT CONNECTED to CPA");
    ws->recvCb=wsCPACallback;
    ws->closeCb=wsStopCPA;
}

extern char **environ;
extern int reset;
extern int power;
extern int last_error;

static char output[1024];	//Temporary buffer for HTML output
static char buf[256];

extern void taskGetRunTimeStats( HttpdConnData *, char * );
extern void reboot(int);

static CgiStatus   cgiSystem(HttpdConnData *connData) {
    uint8_t mac[6];
    tcpip_adapter_ip_info_t ip_info;
    char *res;
#define HTTPD_PRINTF(args...)   sprintf(output, args); httpdSend(connData, output, -1);
    
    if (connData->isConnectionClosed) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

    switch(connData->requestType) {
    case HTTPD_METHOD_GET:
        httpdStartResponse(connData, 200); 
        httpdHeader(connData, "Content-Type", "application/json");
        httpdEndHeaders(connData);

        HTTPD_PRINTF("{");

            HTTPD_PRINTF("\"network\": { ");
                HTTPD_PRINTF("\"interface\": \"%s\", ", (netIface==ESP_IF_WIFI_AP)?"AP":"STA");

                ESP_ERROR_CHECK( esp_wifi_get_mac(netIface, mac) );
                HTTPD_PRINTF("\"mac_address\": \"%x:%x:%x:%x:%x:%x\", ", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

                ESP_ERROR_CHECK( tcpip_adapter_get_ip_info(netIface, &ip_info) );
                HTTPD_PRINTF("\"ip_address\": \"%s\", ", ip4addr_ntoa(&ip_info.ip));

                ESP_ERROR_CHECK(tcpip_adapter_get_hostname(netIface, (const char **) &res));
                HTTPD_PRINTF("\"hostname\": \"%s\" ", res);
            HTTPD_PRINTF("}, ");
                
            int i=0;
            char *t1, *t2;
            HTTPD_PRINTF("\"env\": { ");
                while(environ[i] != NULL) {
                    strcpy(buf, environ[i]);
                    t1 = strtok(buf, "=");
                    t2 = strtok(NULL, "\0");
                    HTTPD_PRINTF("%s \"%s\": \"%s\" ", i==0?"":",", t1, t2);
                    i++;
                }
            HTTPD_PRINTF("}, ");
 
            HTTPD_PRINTF("\"paths\": { ");
                HTTPD_PRINTF("\"%s\": \"%s\", ", "CONFDIR", CONFDIR);
                HTTPD_PRINTF("\"%s\": \"%s\", ", "DISKSDIR", DISKSDIR);
                HTTPD_PRINTF("\"%s\": \"%s\" ", "BOOTROM", BOOTROM);
            HTTPD_PRINTF("}, ");

            HTTPD_PRINTF("\"system\": { ");
                HTTPD_PRINTF("\"free_mem\": %d, ", esp_get_free_heap_size());
                HTTPD_PRINTF("\"time\": %ld, ", time(NULL));
                HTTPD_PRINTF("\"uptime\": %lld ", esp_timer_get_time());
            HTTPD_PRINTF("}, ");

            HTTPD_PRINTF("\"state\": { ");
                HTTPD_PRINTF("\"last_error\": %d, ", last_error);
                HTTPD_PRINTF("\"cpu_error\": %d, ", cpu_error);
                HTTPD_PRINTF("\"reset\": %d, ", reset);
                HTTPD_PRINTF("\"power\": %d ", power);
            HTTPD_PRINTF("}, ");

            HTTPD_PRINTF("\"about\": { ");
                HTTPD_PRINTF("\"%s\": \"%s\", ", "USR_COM", USR_COM);
                HTTPD_PRINTF("\"%s\": \"%s\", ", "USR_REL", USR_REL);
                HTTPD_PRINTF("\"%s\": \"%s\", ", "USR_CPR", USR_CPR);
                HTTPD_PRINTF("\"%s\": \"%s\", ", "cpu", cpu==Z80?"Z80":"I8080");
                if(x_flag) {
                    HTTPD_PRINTF("\"%s\": \"%s\", ", "bootrom", xfn);
                }
                if(cpa_attached) {
                    HTTPD_PRINTF("\"%s\": %d, ", "cpa", dip_settings);
                }
                HTTPD_PRINTF("\"%s\": %d ", "clock", f_flag);
            HTTPD_PRINTF("}, ");

            const esp_partition_t* part;

            HTTPD_PRINTF("\"partitions\": { ");
                part = esp_ota_get_running_partition();
                HTTPD_PRINTF("\"run\": ");
                HTTPD_PRINTF("{ \"label\":\"%s\", \"type\": %d,  \"subtype\": %d }", part->label, part->type, part->subtype);

                part = esp_ota_get_boot_partition();
                if (part != NULL) {
                    HTTPD_PRINTF(", \"boot\": ");
                    HTTPD_PRINTF("{ \"label\":\"%s\", \"type\": %d,  \"subtype\": %d }", part->label, part->type, part->subtype);
                }
                part = esp_ota_get_next_update_partition(NULL);
                if (part != NULL) {
                    HTTPD_PRINTF(", \"next\": ");
                    HTTPD_PRINTF("{ \"label\":\"%s\", \"type\": %d,  \"subtype\": %d }", part->label, part->type, part->subtype);
                }
            HTTPD_PRINTF("} ");

        HTTPD_PRINTF("}");
        break;
    case HTTPD_METHOD_DELETE:
        httpdStartResponse(connData, 205);
		httpdEndHeaders(connData);
        ESP_LOGW(TAG, "Reboot requested. Shutdown in 2 seconds");
        reboot(2000000);
        break;
    default:
		httpdStartResponse(connData, 405);  //http error code 'Method Not Allowed'
		httpdEndHeaders(connData);
    }

    return HTTPD_CGI_DONE;

}

static CgiStatus   cgiTasks(HttpdConnData *connData) {
    
    if (connData->isConnectionClosed) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

    if (connData->requestType!=HTTPD_METHOD_GET) {
		httpdStartResponse(connData, 405);  //http error code 'Method Not Allowed'
		httpdEndHeaders(connData);
	} else {
        httpdStartResponse(connData, 200); 
        httpdHeader(connData, "Content-Type", "application/json");
        httpdEndHeaders(connData);

        HTTPD_PRINTF("{");

            HTTPD_PRINTF("\"tasks\": [ ");
            taskGetRunTimeStats( connData, output );
            HTTPD_PRINTF(" ] ");
        
        HTTPD_PRINTF("}");
    }

    return HTTPD_CGI_DONE;
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
        wsStopVIO(NULL);
        wsStopLPT(NULL);
        wsStopSIO(NULL);
        wsStopCPA(NULL);
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