
#include "esp_log.h"

#define LOG( tag, format, ... )    { esp_log_write(0, tag, format, ##__VA_ARGS__); }
#define LOGV    ESP_LOGV
#define LOGD    ESP_LOGD
#define LOGI    ESP_LOGI
#define LOGW    ESP_LOGW
#define LOGE    ESP_LOGE
