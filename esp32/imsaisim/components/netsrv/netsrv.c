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
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "log.h"
// #include "civetweb.h"
#include "libesphttpd/esp.h"
#include "libesphttpd/httpd.h"
#include "libesphttpd/httpd-freertos.h"
#include "libesphttpd/cgiredirect.h"
#include "libesphttpd/cgiwebsocket.h"
#include "libesphttpd/cgiflash.h"

#include "netsrv.h"

#define HAS_NETSERVER
#ifdef HAS_NETSERVER

#define DOCUMENT_ROOT "/sdcard/www"
#define PORT "80"

#define MAX_WS_CLIENTS (6)
static const char *TAG = "netsrv";

static QueueHandle_t queue[MAX_WS_CLIENTS];

// static msgbuf_t msg;

static ws_client_t ws_clients[MAX_WS_CLIENTS];

char *dev_name[] = {
	"SIO1",
	"LPT",
	"VIO",
	"CPA",
	"DZLR"
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
	return (queue[device] != NULL);
}

/**
 * Assumes the data is:
 * 		TEXT	if only a single byte
 * 		BINARY	if there are multiple bytes
 */
void net_device_send(net_device_t device, char* msg, int len) {
 	if(queue[device] != NULL) {
#ifndef ESP_PLATFORM
		mg_websocket_write(ws_clients[device].conn,
			(len==1)?MG_WEBSOCKET_OPCODE_TEXT:MG_WEBSOCKET_OPCODE_BINARY,
			msg, len);
#else 
        LOGD(TAG, "WS SEND: device: %d data: [%s]", device, msg);
        // cgiWebsockBroadcast(&httpI.httpdInstance, "/sio1", msg, 1, (len==1)?WEBSOCK_FLAG_NONE:WEBSOCK_FLAG_BIN);
        cgiWebsocketSend(&httpI.httpdInstance, (Websock *)ws_clients[device].ws, msg, len, (len==1)?WEBSOCK_FLAG_NONE:WEBSOCK_FLAG_BIN);
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

	if (queue[device] != NULL) {
#ifndef ESP_PLATFORM
		res = msgrcv(queue[device], &msg, 2, 1L, IPC_NOWAIT);
		LOGD(TAG, "GET: device[%d] res[%d] msg[%ld, %s]\r\n", device, res, msg.mtype, msg.mtext);
		if (res == 2) {
			return msg.mtext[0];
		}
#else
        UNUSED(res);
        UNUSED(msg);
		if(uxQueueMessagesWaiting(queue[device]) > 0) {
			xQueueReceive(queue[device], &data, portMAX_DELAY);	
			LOGD(TAG, "GET: device[%d] data[%d, %c]\r\n", device, data, data);
            return data;
		} 
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

	if (queue[device] != NULL) {
#ifndef ESP_PLATFORM
		res = msgrcv(queue[device], &msg, 1, 1L, IPC_NOWAIT);
		LOGV(TAG, "POLL: device[%d] res[%d] errno[%d]", device, res, errno);
		if (res == -1 && errno == E2BIG) {
			LOGD(TAG, "CHARACTERS WAITING");
			return 1;
		}
#else
        UNUSED(res);
        UNUSED(msg);
        if(uxQueueMessagesWaiting(queue[device]) > 0) {
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

// int log_message(const HttpdConnection_t *conn, const char *message)
// {
// 	UNUSED(conn);

// 	puts(message);
// 	return 1;
// }

// void InformWebsockets(struct mg_context *ctx)
// {
// 	static unsigned long cnt = 0;
// 	char text[32];
// 	int i;

// 	UNUSED(cnt);

// 	// sprintf(text, "%lu", ++cnt);
// 	sprintf(text, "%c", 0);

// 	mg_lock_context(ctx);
// 	for (i = 0; i < MAX_WS_CLIENTS; i++) {
// 		if (ws_clients[i].state == 2) {
// 			mg_websocket_write(ws_clients[i].conn,
// 			                   MG_WEBSOCKET_OPCODE_TEXT,
// 			                   text,
// 			                   strlen(text));
// 		}
// 	}
// 	mg_unlock_context(ctx);
// }

// int DirectoryHandler(HttpdConnection_t *conn, void *path) {
//     request_t *req = get_request(conn);
//     struct dirent *pDirent;
//     DIR *pDir;
// 	int i = 0;

//     switch(req->method) {
//     case HTTP_GET:
//         pDir = opendir ((char *)path);
//         if (pDir == NULL) {
//             httpdStartResponse(conn, 404);  //http error code 'Not Found'
//             httpdEndHeaders(conn);
//         } else {
//             httpdStartResponse(conn, 200); 
//             httpdHeader(conn, "Content-Type", "application/json");
//             httpdEndHeaders(conn);
    
//             httpdPrintf(conn, "[");

//             while ((pDirent = readdir(pDir)) != NULL) {
//                 LOGD(TAG, "GET directory: %s type: %d", pDirent->d_name, pDirent->d_type);
//                 if (pDirent->d_type==DT_REG) {
// 					httpdPrintf(conn, "%c\"%s\"", (i++ > 0)?',':' ', pDirent->d_name);
//                  }
//             }
//             closedir (pDir);
//             httpdPrintf(conn, "]");
//         }
// 		break;
// 	default:
//         httpdStartResponse(conn, 405);  //http error code 'Method Not Allowed'
//         httpdEndHeaders(conn);
// 		break;
//     }
// 	return 1;
// }

// int UploadHandler(HttpdConnection_t *conn, void *path) {
//     request_t *req = get_request(conn);
// 	int filelen;
// 	char output[MAX_LFN];

//     switch (req->method) {
//     case HTTP_PUT:
// 		strncpy(output, path, MAX_LFN);

// 		if (output[strlen(output)-1] != '/')
// 			strncat(output, "/", MAX_LFN - strlen(output));

// 		strncat(output, req->args[0], MAX_LFN - strlen(output));

// 		filelen = 0;
// 		filelen = mg_store_body(conn, output);

//         LOGI(TAG, "%d bytes written to %s, received %d", filelen, output, (int) req->len);
//         httpdStartResponse(conn, 200); 
//         httpdHeader(conn, "Content-Type", "application/json");
//         httpdEndHeaders(conn);

//         httpdPrintf(conn, "{");
//         httpdPrintf(conn, "\"filename\": \"%s\", ", output);
//         httpdPrintf(conn, "\"size\": \"%d\" ", filelen);
//         httpdPrintf(conn, "}");
// 		break;
// 	default:
//         httpdStartResponse(conn, 405);  //http error code 'Method Not Allowed'
//         httpdEndHeaders(conn);
// 		break;
//     }
// 	return 1;
// }

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

static TaskHandle_t vioRefresh = NULL;
extern void wsRefreshTask();

static void wsStopDEV(Websock *ws) {
    net_device_t device = (net_device_t)ws->conn->cgiArg2;

    // if (wsSend != NULL) { vTaskDelete(wsSend); wsSend = NULL; };
    // if (sendQueue != NULL) { vQueueDelete(sendQueue); sendQueue = NULL; };
    if(device == DEV_VIO) {
        if (vioRefresh != NULL) { 
            vTaskDelete(vioRefresh); 
            vioRefresh = NULL; 
        };
    };

    if (queue[device] != NULL) { 
        vQueueDelete(queue[device]); 
        queue[device] = NULL; 
    };
    ws_clients[device].ws = NULL;
    ESP_LOGI(__func__, "WS CLIENT CLOSED %s", dev_name[device]);

}

static void wsRecvDEV(Websock *ws, char *data, int len, int flags) {
    net_device_t device = (net_device_t)ws->conn->cgiArg2;

    int i=0;
    while (i < len) {
        char item = (char) data[i++];
        ESP_LOGD(TAG, "WS RECV: [%d] %d", item, len);
        if (queue[device] != NULL) {
            xQueueSend(queue[device], (void *) &item, portMAX_DELAY);
        }
    }
}

static void wsConnDEV(Websock *ws) {
    net_device_t device = (net_device_t)ws->conn->cgiArg2;
    
    ESP_LOGI(__func__, "WS CLIENT CONNECTED to %s", dev_name[device]);
    // ESP_LOGI(__func__, "WS CLIENT CONNECTED to %d", device);
    
    ws_clients[device].ws = (void *)ws;

    ws->recvCb=wsRecvDEV;
    ws->closeCb=wsStopDEV;
    
    switch (device) {
        case DEV_SIO1:
        case DEV_VIO:
        case DEV_LPT:
        case DEV_DZLR:
            queue[device] = xQueueCreate(30, sizeof(char));
            if (queue[device] == NULL) {
                ESP_LOGE(__func__, "WS RECV QUEUE CREATE FAILED");
            }
            break;
        default:
            break;
        }

    if(device == DEV_SIO1) {
        cgiWebsocketSend(&httpI.httpdInstance, ws, "Connected to ESP32 IMSAI 8080 Simulation\n\r", 42, WEBSOCK_FLAG_NONE);
    };

    if (device == DEV_VIO) {
        xTaskCreatePinnedToCore(wsRefreshTask, "vioRefreshTask", 2000, NULL, ESP_TASK_MAIN_PRIO + 5, &vioRefresh, 0);
        if (vioRefresh == NULL) 
            ESP_LOGE(TAG, "WS VIO REFREASH TASK CREATE FAILED");

	// 	BYTE mode = peek(0xf7ff);
	// 	poke(0xf7ff, 0x00);
	// 	SLEEP_MS(100);
	// 	poke(0xf7ff, mode);
	}
    // sendQueue = xQueueCreate(96 , sizeof(char));
    // if (sendQueue == NULL) ESP_LOGE(__func__, "WS SEND QUEUE CREATE FAILED");
    // xTaskCreatePinnedToCore(wsSendTask, "wsSendTask", 2000, NULL, ESP_TASK_MAIN_PRIO + 5, &wsSend, 0);
    // if (wsSend == NULL) ESP_LOGE(__func__, "WS SEND TASK CREATE FAILED");
}

// static struct mg_context *ctx = NULL;

// void stop_net_services (void) {

// 	if (ctx != NULL) {
// 		InformWebsockets(ctx);

// 		/* Stop the server */
// 		mg_stop(ctx);
// 		LOGI(TAG, "Server stopped.");
// 		LOGI(TAG, "Bye!");

// 		ctx = NULL;
// 	}
// }

// int start_net_services (void) {

// 	//TODO: add config for DOCUMENT_ROOT, PORT

// 	const char *options[] = {
// 	    "document_root",
// 	    DOCUMENT_ROOT,
// 	    "listening_ports",
// 	    PORT,
//         "num_threads",
//         "2",
//         "max_request_size",
//         "4096",
// 	    "request_timeout_ms",
// 	    "10000",
// 	    "error_log_file",
// 	    "error.log",
// 	    "websocket_timeout_ms",
// 	    "3600000",
// 	    "enable_auth_domain_check",
// 	    "no",
// 		"url_rewrite_patterns",
// 		"/imsai/disks/=./disks/, /imsai/conf/=./conf/, /imsai/printer.txt=./printer.txt",
// 	    0};

// 	struct mg_callbacks callbacks;

// #ifdef DEBUG
// 	struct mg_server_ports ports[32];
// 	int port_cnt, n;
// 	int err = 0;
//     const struct mg_option *opts;
// #endif

// 	atexit(stop_net_services);

// 	memset(queue, 0, sizeof(queue));

//     /* Start CivetWeb web server */
// 	memset(&callbacks, 0, sizeof(callbacks));
// 	callbacks.log_message = log_message;

//     LOGI(TAG, "Starting CivetWeb - mg_start.");

// 	ctx = mg_start(&callbacks, 0, options);

// 	/* Check return value: */
// 	if (ctx == NULL) {
// 		LOGW(TAG, "Cannot start CivetWeb - mg_start failed.");
// 		return EXIT_FAILURE;
// 	}

//     LOGI(TAG, "Started CivetWeb - mg_start succeeded.");

// 	//TODO: sort out all the paths for the handlers
//     // mg_set_request_handler(ctx, "/system", 		SystemHandler, 		0);
//     mg_set_request_handler(ctx, "/tasks", 		taskGetRunTimeStats, 		0);
//     // mg_set_request_handler(ctx, "/conf", 		ConfigHandler, 		"conf");
// #ifdef HAS_DISKMANAGER
//     mg_set_request_handler(ctx, "/library", 	LibraryHandler, 	0);
//     mg_set_request_handler(ctx, "/disks", 		DiskHandler, 		0);
// #endif

// 	// mg_set_websocket_handler(ctx, "/sio1",
// 	//                          WebSocketConnectHandler,
// 	//                          WebSocketReadyHandler,
// 	//                          WebsocketDataHandler,
// 	//                          WebSocketCloseHandler,
// 	//                          (void *) DEV_SIO1);
// 	// mg_set_websocket_handler(ctx, "/lpt",
// 	//                          WebSocketConnectHandler,
// 	//                          WebSocketReadyHandler,
// 	//                          WebsocketDataHandler,
// 	//                          WebSocketCloseHandler,
// 	//                          (void *) DEV_LPT);
	
// 	// mg_set_websocket_handler(ctx, "/vio",
// 	//                          WebSocketConnectHandler,
// 	//                          WebSocketReadyHandler,
// 	//                          WebsocketDataHandler,
// 	//                          WebSocketCloseHandler,
// 	//                          (void *) DEV_VIO);
	
// 	// mg_set_websocket_handler(ctx, "/dazzler",
// 	//                          WebSocketConnectHandler,
// 	//                          WebSocketReadyHandler,
// 	//                          WebsocketDataHandler,
// 	//                          WebSocketCloseHandler,
// 	//                          (void *) DEV_DZLR);
	
// 	// mg_set_websocket_handler(ctx, "/cpa",
// 	//                          WebSocketConnectHandler,
// 	//                          WebSocketReadyHandler,
// 	//                          WebsocketDataHandler,
// 	//                          WebSocketCloseHandler,
// 	//                          (void *) DEV_CPA);

// #ifdef DEBUG
// 	/* List all listening ports */
// 	memset(ports, 0, sizeof(ports));
// 	port_cnt = mg_get_server_ports(ctx, 32, ports);
// 	printf("\n%i listening ports:\n", port_cnt);
// #endif

// 	return EXIT_SUCCESS;
// }

static const CgiUploadFlashDef flashDef;

const HttpdBuiltInUrl builtInUrls[]={
    {"/", cgiRedirect, "/console/", NULL},
    {"/sio1", cgiWebsocket, wsConnDEV, (void *)DEV_SIO1},
    {"/lpt", cgiWebsocket, wsConnDEV, (void *)DEV_LPT},
    {"/vio", cgiWebsocket, wsConnDEV, (void *)DEV_VIO},
    {"/dazzler", cgiWebsocket, wsConnDEV, (void *)DEV_DZLR},
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
        // update_status(0x08, STATUS_SET);
    }
}

void stop_net_services(void) {

}
#endif