/**
 * netsrv.h
 *
 * Copyright (C) 2018 by David McNaughton
 * 
 * History:
 * 12-JUL-18    1.0     Initial Release
 */

/**
 * This web server module provides...
 */
#include <stdbool.h>
#ifndef ESP_PLATFORM
#include <sys/ipc.h>
#include <sys/msg.h>
#endif
#include <string.h>

#define UNUSED(x) (void)(x)

enum net_device {
	DEV_SIO1,
	DEV_LPT,
	DEV_VIO,
	DEV_CPA,
	DEV_DZLR
};

typedef enum net_device net_device_t;

struct msgbuf {
	long	mtype;
	char	mtext[64];
};

typedef struct msgbuf msgbuf_t;

struct ws_client {
	// struct mg_connection *conn;
	void *ws;
	int state;
};

typedef struct ws_client ws_client_t;

extern int net_device_alive(net_device_t);
extern void net_device_send(net_device_t, char*, int);
extern int net_device_get(net_device_t);
extern int net_device_poll(net_device_t);

/*
* convenience macros for http output
*/
typedef struct HttpdConnData HttpdConnection_t;

#ifndef ESP_PLATFORM
#define httpdPrintf(conn, args...)			mg_printf(conn, args)
#define httpdStartResponse(conn, code)		mg_printf(conn, "HTTP/1.1 %d OK\r\n", code)
#define httpdHeader(conn, key, val)			mg_printf(conn, "%s: %s\r\n", key, val)
#define httpdEndHeaders(conn)				mg_printf(conn, "\r\n")
#else
extern void httpdPrintf(HttpdConnection_t *conn, const char* format, ...);
#endif

#define _HTTP_MAX_ARGS	8

enum http_method {
	HTTP_GET,
	HTTP_POST,
	HTTP_PUT,
	HTTP_DELETE,
	UNKNOWN
};

typedef enum http_method http_method_t;

struct request {
	void *mg; // struct mg_request_info *
	http_method_t method;
	const char *args[_HTTP_MAX_ARGS];
	long long len;
};

typedef struct request request_t;

extern request_t *get_request(const HttpdConnection_t *);
int get_body(HttpdConnection_t *, char *, int);