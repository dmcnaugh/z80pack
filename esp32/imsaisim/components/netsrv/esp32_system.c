#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "tcpip_adapter.h"
#include "libesphttpd/esp.h"
#include "libesphttpd/httpd.h"
#include "libesphttpd/httpd-freertos.h"
#include "netsrv.h"
#include "log.h"

#include "esp32_hardware.h"

#include "esp_ota_ops.h"

#include "sim.h"
#include "simglb.h"


static const char* TAG = "esp32_system";

extern char **environ;
extern int reset;
extern int power;
extern int last_error;

// static char output[1024];	//Temporary buffer for HTML output
static char buf[256];

extern void taskGetRunTimeStats(HttpdConnection_t *, char *);
extern void reboot(int);

CgiStatus   cgiSystem(HttpdConnection_t *conn) {
    uint8_t mac[6];
    tcpip_adapter_ip_info_t ip_info;
    char *res;
// #define httpdPrintf(conn, args...)   sprintf(output, args); httpdSend(conn, output, -1);
    
    if (conn->isConnectionClosed) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

    switch(conn->requestType) {
    case HTTPD_METHOD_GET:
        httpdStartResponse(conn, 200); 
        httpdHeader(conn, "Content-Type", "application/json");
        httpdEndHeaders(conn);

        httpdPrintf(conn, "{");

            httpdPrintf(conn, "\"platform\": \"esp32\", ");
            
            httpdPrintf(conn, "\"network\": { ");
                httpdPrintf(conn, "\"interface\": \"%s\", ", (netIface==ESP_IF_WIFI_AP)?"AP":"STA");

                ESP_ERROR_CHECK( esp_wifi_get_mac(netIface, mac) );
                httpdPrintf(conn, "\"mac_address\": \"%x:%x:%x:%x:%x:%x\", ", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

                ESP_ERROR_CHECK( tcpip_adapter_get_ip_info(netIface, &ip_info) );
                httpdPrintf(conn, "\"ip_address\": \"%s\", ", ip4addr_ntoa(&ip_info.ip));

                ESP_ERROR_CHECK(tcpip_adapter_get_hostname(netIface, (const char **) &res));
                httpdPrintf(conn, "\"hostname\": \"%s\" ", res);
            httpdPrintf(conn, "}, ");
                
            int i=0;
            char *t1, *t2;
            httpdPrintf(conn, "\"env\": { ");
                while(environ[i] != NULL) {
                    strcpy(buf, environ[i]);
                    t1 = strtok(buf, "=");
                    t2 = strtok(NULL, "\0");
                    httpdPrintf(conn, "%s \"%s\": \"%s\" ", i==0?"":",", t1, t2);
                    i++;
                }
            httpdPrintf(conn, "}, ");
 
            httpdPrintf(conn, "\"paths\": { ");
                httpdPrintf(conn, "\"%s\": \"%s\", ", "CONFDIR", CONFDIR);
                httpdPrintf(conn, "\"%s\": \"%s\", ", "DISKSDIR", DISKSDIR);
                httpdPrintf(conn, "\"%s\": \"%s\" ", "BOOTROM", BOOTROM);
            httpdPrintf(conn, "}, ");

            httpdPrintf(conn, "\"system\": { ");
                httpdPrintf(conn, "\"%s\": \"%s\",", "IDF_VER", esp_get_idf_version());
                httpdPrintf(conn, "\"free_mem\": %d, ", heap_caps_get_free_size(MALLOC_CAP_8BIT )); //| MALLOC_CAP_INTERNAL));
                httpdPrintf(conn, "\"time\": %ld, ", time(NULL));
                httpdPrintf(conn, "\"uptime\": %lld ", esp_timer_get_time());
            httpdPrintf(conn, "}, ");

            httpdPrintf(conn, "\"state\": { ");
                httpdPrintf(conn, "\"last_error\": %d, ", last_error);
                httpdPrintf(conn, "\"cpu_error\": %d, ", cpu_error);
                httpdPrintf(conn, "\"reset\": %d, ", reset);
                httpdPrintf(conn, "\"power\": %d ", power);
            httpdPrintf(conn, "}, ");

            httpdPrintf(conn, "\"about\": { ");
                const esp_app_desc_t* desc = esp_ota_get_app_description();

                httpdPrintf(conn, "\"%s\": \"%s\", ", "APP_VER", desc->version);

                httpdPrintf(conn, "\"%s\": \"%s\", ", "USR_COM", USR_COM);
                httpdPrintf(conn, "\"%s\": \"%s\", ", "USR_REL", USR_REL);
                httpdPrintf(conn, "\"%s\": \"%s\", ", "USR_CPR", USR_CPR);
                httpdPrintf(conn, "\"%s\": \"%s\", ", "cpu", cpu==Z80?"Z80":"I8080");
                if(x_flag) {
                    httpdPrintf(conn, "\"%s\": \"%s\", ", "bootrom", xfn);
                }
                if(cpa_attached) {
                    httpdPrintf(conn, "\"%s\": %d, ", "cpa", nvs_settings);
                }
                httpdPrintf(conn, "\"%s\": %d ", "clock", f_flag);
            httpdPrintf(conn, "}, ");

            const esp_partition_t* part;

            httpdPrintf(conn, "\"partitions\": { ");
                part = esp_ota_get_running_partition();
                httpdPrintf(conn, "\"run\": ");
                httpdPrintf(conn, "{ \"label\":\"%s\", \"type\": %d,  \"subtype\": %d }", part->label, part->type, part->subtype);

                part = esp_ota_get_boot_partition();
                if (part != NULL) {
                    httpdPrintf(conn, ", \"boot\": ");
                    httpdPrintf(conn, "{ \"label\":\"%s\", \"type\": %d,  \"subtype\": %d }", part->label, part->type, part->subtype);
                }
                part = esp_ota_get_next_update_partition(NULL);
                if (part != NULL) {
                    httpdPrintf(conn, ", \"next\": ");
                    httpdPrintf(conn, "{ \"label\":\"%s\", \"type\": %d,  \"subtype\": %d }", part->label, part->type, part->subtype);
                }
            httpdPrintf(conn, "} ");

        httpdPrintf(conn, "}");
        break;
    case HTTPD_METHOD_DELETE:
        httpdStartResponse(conn, 205);
		httpdEndHeaders(conn);
        ESP_LOGW(TAG, "Reboot requested. Shutdown in 2 seconds");
        reboot(2000000);
        break;
    default:
		httpdStartResponse(conn, 405);  //http error code 'Method Not Allowed'
		httpdEndHeaders(conn);
    }

    return HTTPD_CGI_DONE;

}

CgiStatus   cgiTasks(HttpdConnection_t *conn) {
    
    if (conn->isConnectionClosed) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

    if (conn->requestType!=HTTPD_METHOD_GET) {
		httpdStartResponse(conn, 405);  //http error code 'Method Not Allowed'
		httpdEndHeaders(conn);
	} else {
        httpdStartResponse(conn, 200); 
        httpdHeader(conn, "Content-Type", "application/json");
        httpdEndHeaders(conn);

        httpdPrintf(conn, "{ ");
            httpdPrintf(conn, "\"free_mem\": %d, ", esp_get_free_heap_size());

            httpdPrintf(conn, "\"tasks\": [ ");
            taskGetRunTimeStats(conn, NULL);
            httpdPrintf(conn, " ] ");
        
        httpdPrintf(conn, "}");
    }

    return HTTPD_CGI_DONE;
}
