#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"

extern EventGroupHandle_t wifi_event_group;
extern esp_err_t wifi_event_handler(void *ctx, system_event_t *event);

extern void start_wifi_services(void);

extern QueueHandle_t sendQueue;
extern QueueHandle_t printQueue;
extern QueueHandle_t recvQueue;
extern QueueHandle_t VIOQueue;

extern void vioSend(char *msg , int len);