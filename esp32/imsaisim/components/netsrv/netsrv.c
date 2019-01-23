/**
 * netsrv.c
 *
 * Copyright (C) 2018 by David McNaughton
 * 
 * History:
 * 12-JUL-18    1.0     Initial Release
 */

/**
 * This web server module provides...
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include "sim.h"
#include "simglb.h"
#include "frontpanel.h"
#include "memory.h"
// #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "log.h"
// #include "civetweb.h"
#include "libesphttpd/esp.h"
#include "libesphttpd/httpd.h"
#include "libesphttpd/httpd-freertos.h"
#include "libesphttpd/cgiredirect.h"
#include "libesphttpd/cgiwebsocket.h"
#include "libesphttpd/cgiflash.h"

#include "esp32_hardware.h"
#include "netsrv.h"

#define HAS_NETSERVER
#ifdef HAS_NETSERVER

#define DOCUMENT_ROOT "/sdcard/www"
#define PORT "80"

#define MAX_WS_CLIENTS (7)
static const char *TAG = "netsrv";

static msgbuf_t msg;

struct {
    QueueHandle_t rxQueue;
    QueueHandle_t txQueue;
    TaskHandle_t txTask;
    ws_client_t ws_client;
} dev[MAX_WS_CLIENTS];

char *dev_name[] = {
	"SIO1",
	"LPT",
	"VIO",
	"CPA",
	"DZLR",
    "ACC"
};

#ifdef HAS_DISKMANAGER
extern int LibraryHandler(HttpdConnection_t *, void *);
extern int DiskHandler(HttpdConnection_t *, void *);
#endif

#define MAX_CONNECTIONS 15
static char connectionMemory[sizeof(RtosConnType) * MAX_CONNECTIONS];
static HttpdFreertosInstance httpI;
static HttpdInitStatus httpd;

extern CgiStatus cgiEspVfsHook(HttpdConnData *);
extern CgiStatus cgiEspVfsUpload(HttpdConnData *);
extern CgiStatus cgiEspVfsDir(HttpdConnData *);
extern CgiStatus cgiSystem(HttpdConnData *);
extern CgiStatus cgiTasks(HttpdConnData *);

/**
 * Check if a queue is provisioned
 */
int net_device_alive(net_device_t device) {
	return (dev[device].rxQueue != NULL);
}

/**
 * Assumes the data is:
 * 		TEXT	if only a single byte
 * 		BINARY	if there are multiple bytes
 */
void net_device_send(net_device_t device, char* msg, int len) {
    int fl = WEBSOCK_FLAG_NONE;
    int next;
    int rem;
    int chunk;
    int cont;

    if(device != DEV_CPA && len > 1) fl = WEBSOCK_FLAG_BIN;

 	if(dev[device].rxQueue != NULL) {
#ifndef ESP_PLATFORM
		mg_websocket_write(dev[device].ws_client.conn,
			(len==1)?MG_WEBSOCKET_OPCODE_TEXT:MG_WEBSOCKET_OPCODE_BINARY,
			msg, len);
#else 
        LOGD(TAG, "WS SEND: device: %d data: [%s]", device, msg);
        if (dev[device].txQueue != NULL) {
            xQueueSend(dev[device].txQueue, msg, portMAX_DELAY);
        } else {
            httpdConnSendStart(&httpI.httpdInstance, ((Websock *)dev[device].ws_client.ws)->conn);
            next = 0;
            rem = len;
            cont = 0;
#define CHUNK_SIZE 1024
            while(rem > 0) {
                if (rem > CHUNK_SIZE) { 
                    fl |= WEBSOCK_FLAG_MORE;
                    chunk = CHUNK_SIZE;
                    cont = 1;
                    ESP_LOGI(TAG, "WebSock chunking %d of %d", chunk, len);
                } else {
                    fl &= ~WEBSOCK_FLAG_MORE;
                    chunk = rem;
                }
                cgiWebsocketSend(&httpI.httpdInstance, (Websock *)dev[device].ws_client.ws, msg+next, chunk, fl);

                if(cont) fl |= WEBSOCK_FLAG_CONT;
                next += chunk;
                rem -= chunk;
            }
            
            httpdConnSendFinish(&httpI.httpdInstance, ((Websock *)dev[device].ws_client.ws)->conn);
        }
#endif
	}
}

/**
 * Always removes something from the queue is data waiting
 * returns:
 * 		char	if data is waiting in the queue
 * 		-1		if the queue is not open, is empty
 */
int net_device_get(net_device_t device) {
	ssize_t res;
	msgbuf_t msg;
    char data;

	if (dev[device].rxQueue != NULL) {
#ifndef ESP_PLATFORM
		res = msgrcv(dev[device].rxQueue, &msg, 2, 1L, IPC_NOWAIT);
		LOGD(TAG, "GET: device[%d] res[%d] msg[%ld, %s]\r\n", device, res, msg.mtype, msg.mtext);
		if (res == 2) {
			return msg.mtext[0];
		}
#else
        UNUSED(res);
        UNUSED(msg);
		if(uxQueueMessagesWaiting(dev[device].rxQueue) > 0) {
			xQueueReceive(dev[device].rxQueue, &data, portMAX_DELAY);	
			LOGD(TAG, "GET: device[%d] data[%d, %c]\r\n", device, data, data);
            return data;
		} 
#endif
	}

	return -1;
}

int net_device_get_data(net_device_t device, char *dst, int len) {
	ssize_t res;
	msgbuf_t msg;

	if (dev[device].rxQueue != NULL) {
#ifndef ESP_PLATFORM
		res = msgrcv(queue[device], &msg, len, 1L, MSG_NOERROR);
		// if (device == DEV_88ACC)
		// 	LOGI(TAG, "GET: device[%d] res[%ld] msg[tyep: %ld]\r\n", device, res, msg.mtype);
		memcpy((void *)dst, (void *)msg.mtext, res);
		return res;
#else
        // UNUSED(res);
        // UNUSED(msg);
		// if(uxQueueMessagesWaiting(dev[device].rxQueue) > 0) {
			xQueueReceive(dev[device].rxQueue, &msg, portMAX_DELAY);
            res = msg.len;	
			// LOGI(TAG, "GET: device[%d] res[%d] msg[type: %ld]\r\n", device, res, msg.mtype);
            memcpy((void *)dst, (void *)msg.mtext, res);
            return res;
		// } 
#endif
	}
	return -1;
}

/**
 * Doesn't remove data from the queue
 * returns:
 * 		1	if data is waiting in the queue
 * 		0	if the queue is not open or is empty
 */
int net_device_poll(net_device_t device) {
	ssize_t res;
	msgbuf_t msg;

	if (dev[device].rxQueue != NULL) {
#ifndef ESP_PLATFORM
		res = msgrcv(dev[device].rxQueue, &msg, 1, 1L, IPC_NOWAIT);
		LOGV(TAG, "POLL: device[%d] res[%d] errno[%d]", device, res, errno);
		if (res == -1 && errno == E2BIG) {
			LOGD(TAG, "CHARACTERS WAITING");
			return 1;
		}
#else
        UNUSED(res);
        UNUSED(msg);
        if(uxQueueMessagesWaiting(dev[device].rxQueue) > 0) {
            return 1;
        }
#endif
	}
	return 0;
}

int get_body(HttpdConnection_t *conn, char *buf, int maxlen) {
#ifndef ESP_PLATFORM
        return mg_read(conn, buf, maxlen);
#else
        if (conn->post.received < conn->post.len) 
            LOGW(TAG, "Body too long, did not get all chunks");
        strncpy(buf, conn->post.buff, maxlen);
        return strlen(buf);
#endif
}

request_t *get_request(const HttpdConnection_t *conn) {
	static request_t req;

#ifndef ESP_PLATFORM
    req.mg = (void *)mg_get_request_info(conn);

    if (!strcmp(req.mg->request_method, "GET")) {
		req.method = HTTP_GET;
    } else if (!strcmp(req.mg->request_method, "POST")) {
		req.method = HTTP_POST;
    } else if (!strcmp(req.mg->request_method, "PUT")) {
		req.method = HTTP_PUT;
    } else if (!strcmp(req.mg->request_method, "DELETE")) {
		req.method = HTTP_DELETE;
    } else {
		req.method = UNKNOWN;
	}

	//TODO: split query_string on '&' into args[] - for now its all jammed into args[0]
	req.args[0] = req.mg->query_string;
	req.len = req.mg->content_length;
#else
    switch (conn->requestType) {
        case HTTPD_METHOD_GET:
            req.method = HTTP_GET;
            break;
        case HTTPD_METHOD_POST:
            req.method = HTTP_POST;
            break;
        case HTTPD_METHOD_PUT:
            req.method = HTTP_PUT;
            break;
        case HTTPD_METHOD_DELETE:
            req.method = HTTP_DELETE;
            break;
        default:
            req.method = UNKNOWN;
            break;
    }

	//TODO: split query_string on '&' into args[] - for now its all jammed into args[0]
    req.args[0] = conn->getArgs;
    req.len = conn->post.len;
#endif

	return &req;
}

void httpdPrintf(HttpdConnection_t *conn, const char* format, ...) {
    char output[256];
    va_list(args);
    va_start(args, format);
    vsprintf(output, format, args); 
    httpdSend(conn, output, -1);
}

int DirectoryHandler(HttpdConnection_t *conn, void *path) {

    conn->cgiArg = path;
    return cgiEspVfsDir(conn);
}

int UploadHandler(HttpdConnection_t *conn, void *path) {

    conn->cgiArg = path;
    return cgiEspVfsUpload(conn);
}

int configHandler(HttpdConnection_t *conn, void *path) {
    request_t *req = get_request(conn);

    switch (req->method) {
    case HTTP_GET:
        return DirectoryHandler(conn, path);
		break;
    case HTTP_PUT:
	    return UploadHandler(conn, path);
		break;
	default:
        httpdStartResponse(conn, 405);  //http error code 'Method Not Allowed'
        httpdEndHeaders(conn);
		break;
    }
	return 1;
}

CgiStatus   cgiDisks(HttpdConnection_t *conn) {

    return DiskHandler(conn, NULL);
}
CgiStatus   cgiLibrary(HttpdConnection_t *conn) {
    
    return LibraryHandler(conn, NULL);
}
CgiStatus   cgiConfig(HttpdConnection_t *conn) {
    
    return configHandler(conn, (char *)conn->cgiArg);
}

extern void wsRefreshTask();

static void wsStopDEV(Websock *ws) {
    net_device_t device = (net_device_t)ws->conn->cgiArg2;

    if (dev[device].txTask != NULL) {
        vTaskDelete(dev[device].txTask); 
        dev[device].txTask = NULL; 
    }
    if (dev[device].rxQueue != NULL) { 
        vQueueDelete(dev[device].rxQueue); 
        dev[device].rxQueue = NULL; 
    };
    if (dev[device].txQueue != NULL) { 
        vQueueDelete(dev[device].txQueue); 
        dev[device].txQueue = NULL; 
    };
    dev[device].ws_client.ws = NULL;
    dev[device].ws_client.state = 0;
    ESP_LOGI(__func__, "WS CLIENT CLOSED %s", dev_name[device]);

}

static void wsRecvDEV(Websock *ws, char *data, int len, int flags) {
    net_device_t device = (net_device_t)ws->conn->cgiArg2;
    int i=0;
    // msgbuf_t msg;

    if (device == DEV_88ACC) {
        msg.mtype = 1L;
        msg.len = len;
        // if(len == 128) {
        if(len != 128) ESP_LOGI(__func__, "%2X - Continue? %d", flags, len);
            memcpy(msg.mtext, data, len);
            if (dev[device].rxQueue != NULL) {
                xQueueSend(dev[device].rxQueue, (void *) &msg, portMAX_DELAY);
            }
        // }
        // if (msgsnd(queue[(net_device_t)device], &msg, len, 0)) {
        //     perror("msgsnd()");
        // };

    } else {

    while (i < len) {
        char item = (char) data[i++];
        ESP_LOGD(TAG, "WS RECV: [%d] %d", item, len);
        if (dev[device].rxQueue != NULL) {
            xQueueSend(dev[device].rxQueue, (void *) &item, portMAX_DELAY);
        }
    }

    }
}

static void wsSendTask(void *devID) {
    net_device_t device = (net_device_t) devID;
    ESP_LOGI(__func__, "WS DEV==(%d)", device);

    while(true) {
        char data;
        ESP_LOGD(__func__, "WS (%d)", device);
        xQueueReceive(dev[device].txQueue, &data, portMAX_DELAY);
        ESP_LOGD(__func__, "WS %s SEND: [%d]", dev_name[device], data);
        httpdConnSendStart(&httpI.httpdInstance, ((Websock *)dev[device].ws_client.ws)->conn);

        cgiWebsocketSend(&httpI.httpdInstance, (Websock *)dev[device].ws_client.ws, (char *) &data, 1, WEBSOCK_FLAG_NONE);

        httpdConnSendFinish(&httpI.httpdInstance, ((Websock *)dev[device].ws_client.ws)->conn);
    };
}

static void wsConnDEV(Websock *ws) {
    net_device_t device = (net_device_t)ws->conn->cgiArg2;
    
    ESP_LOGI(__func__, "WS CLIENT CONNECTED to %s", dev_name[device]);
    
    dev[device].ws_client.ws = (void *)ws;
    dev[device].ws_client.state = 2;

    ws->recvCb=wsRecvDEV;
    ws->closeCb=wsStopDEV;
    
    switch (device) {
        case DEV_SIO1:
        case DEV_VIO:
        case DEV_LPT:
        case DEV_CPA:
        case DEV_DZLR:
            dev[device].rxQueue = xQueueCreate(10, sizeof(char));
            if (dev[device].rxQueue == NULL) {
                ESP_LOGE(__func__, "WS RECV QUEUE CREATE FAILED for %s", dev_name[device]);
            } else {
                ESP_LOGD(__func__, "WS RECV QUEUE CREATE SUCCEED for %s", dev_name[device]);
            }
            break;
        case DEV_88ACC:
            dev[device].rxQueue = xQueueCreate(2, sizeof(msg));
            if (dev[device].rxQueue == NULL) {
                ESP_LOGE(__func__, "WS RECV QUEUE CREATE FAILED for %s", dev_name[device]);
            } else {
                ESP_LOGI(__func__, "Special WS RECV QUEUE CREATE SUCCEED for %s", dev_name[device]);
            }
            break;
        default:
            break;
    }

    switch (device) {
        case DEV_SIO1:
        case DEV_LPT:
            dev[device].txQueue = xQueueCreate(85, sizeof(char));
            if (dev[device].txQueue == NULL) {
                ESP_LOGE(__func__, "WS SEND QUEUE CREATE FAILED for %s", dev_name[device]);
            } else {
                ESP_LOGD(__func__, "WS SEND QUEUE CREATE SUCCEED for %s", dev_name[device]);
            }
            // xTaskCreatePinnedToCore(wsSendTask, "wsSendTask", 2000, ws->conn->cgiArg2, ESP_TASK_MAIN_PRIO + 6 + (int) device, &dev[device].txTask, 0);
            xTaskCreatePinnedToCore(wsSendTask, dev_name[device], 2000, ws->conn->cgiArg2, ESP_TASK_MAIN_PRIO + 6 + (int) device, &dev[device].txTask, 0);
            if (dev[device].txTask == NULL) {
                ESP_LOGE(__func__, "WS SEND TASK CREATE FAILED");
            } else {
                ESP_LOGD(__func__, "WS SEND TASK CREATE SUCCEED");
            }
            break;
        default:
            break;
    }

    if(device == DEV_SIO1) {
        // cgiWebsocketSend(&httpI.httpdInstance, ws, "Connected to ESP32 IMSAI 8080 Simulation\n\r", 42, WEBSOCK_FLAG_NONE);
    };

    if (device == DEV_VIO) {
        /* start IMSAI VIO task if firmware is loaded */
        if (!strncmp((char *) mem_base() + 0xfffd, "VI0", 3)) {
            xTaskCreatePinnedToCore(wsRefreshTask, "vioRefreshTask", 2000, NULL, ESP_TASK_MAIN_PRIO + 12, &dev[device].txTask, 0);
            if (dev[device].txTask == NULL) 
                ESP_LOGE(TAG, "WS VIO REFRESH TASK CREATE FAILED");
        }

	// 	BYTE mode = peek(0xf7ff);
	// 	poke(0xf7ff, 0x00);
	// 	SLEEP_MS(100);
	// 	poke(0xf7ff, mode);
	}
}

static const CgiUploadFlashDef flashDef;

const HttpdBuiltInUrl builtInUrls[]={
    {"/", cgiRedirect, "/console/", NULL},
    {"/sio1", cgiWebsocket, wsConnDEV, (void *)DEV_SIO1},
    {"/lpt", cgiWebsocket, wsConnDEV, (void *)DEV_LPT},
    {"/vio", cgiWebsocket, wsConnDEV, (void *)DEV_VIO},
    {"/dazzler", cgiWebsocket, wsConnDEV, (void *)DEV_DZLR},
    {"/acc", cgiWebsocket, wsConnDEV, (void *)DEV_88ACC},
    {"/cpa", cgiWebsocket, wsConnDEV, (void *)DEV_CPA},
    {"/disks", cgiDisks, NULL, NULL},
    // {"/cfg", cgiEspVfsUpload, "/sdcard/imsai/conf", NULL},
    {"/console", cgiEspVfsUpload, "/sdcard/www/console", NULL},
    {"/printer", cgiEspVfsUpload, "/sdcard/www/printer", NULL},
    // {"/libbox", cgiEspVfsUpload, DISKSDIR, NULL},
    {"/library", cgiLibrary, NULL, NULL},
    {"/system", cgiSystem, NULL, NULL},
    {"/tasks", cgiTasks, NULL, NULL},
    {"/flash", cgiUploadFirmware, &flashDef, NULL},
    {"/conf", cgiConfig, "/sdcard/imsai/conf", NULL},
    {"/imsai/*", cgiEspVfsHook, "/sdcard", NULL},
    {"*", cgiEspVfsHook, "/sdcard/www", NULL},
	{NULL, NULL, NULL, NULL}
};

void start_net_services(void) {
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

void InformWebsockets(void)
{
	int i;

	for (i = 0; i < MAX_WS_CLIENTS; i++) {
		if (dev[i].ws_client.state == 2) {
            ESP_LOGI(__func__, "Close WS CLIENT %s", dev_name[i]);

            httpdConnSendStart(&httpI.httpdInstance, ((Websock *)dev[i].ws_client.ws)->conn);
            cgiWebsocketClose(&httpI.httpdInstance, (Websock *)dev[i].ws_client.ws, 0x0000);
            httpdConnSendFinish(&httpI.httpdInstance, ((Websock *)dev[i].ws_client.ws)->conn);
		}
	}
}

void stop_net_services(void) {

    if(httpd == InitializationSuccess) {
        InformWebsockets();
    }

}
#endif