/*
 *  Non-Volatile Storage (NVS) 
 * 
 */
#include "nvs_flash.h"
#include "nvs.h"
#include "log.h"

static const char *TAG = "nvs";
static nvs_handle my_handle = 0;

static struct nvs_settings_t {
    uint16_t settings;
    char *ssid;
    char *password;
} nvs_settings;

uint16_t get_nvs_settings(bool RW)
{
    size_t required_size;

    nvs_settings.settings = 0; // value will default to 0, if not set yet in NVS
    nvs_settings.ssid = NULL;
    nvs_settings.password = NULL;

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_LOGW(TAG, "NVS Partition needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    // Open
    ESP_LOGD(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    err = nvs_open("imsai", RW ? NVS_READWRITE : NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Done");

        // Read
        ESP_LOGI(TAG, "Reading settings from NVS ");
        err = nvs_get_u16(my_handle, "settings", &nvs_settings.settings);
        switch (err) {
            case ESP_OK:
                ESP_LOGD(TAG, "Done");
                ESP_LOGI(TAG, "settings = %08X", nvs_settings.settings);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The value is not initialized yet!");
                // Write
                ESP_LOGD(TAG, "Updating settings in NVS ... ");
                err = nvs_set_u16(my_handle, "settings", nvs_settings.settings);
                ESP_LOGD(TAG, "%s", (err != ESP_OK) ? "Failed!" : "Done");
                // Commit written value.
                // After setting any values, nvs_commit() must be called to ensure changes are written
                // to flash storage. Implementations may write to storage at other times,
                // but this is not guaranteed.
                ESP_LOGD(TAG, "Committing updates in NVS ... ");
                err = nvs_commit(my_handle);
                ESP_LOGD(TAG, "%s", (err != ESP_OK) ? "Failed!" : "Done");
                break;
            default :
                ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
        }

        err = nvs_get_str(my_handle, "ssid", NULL, &required_size);
        if (err == ESP_OK && required_size > 0) {
            nvs_settings.ssid = malloc(required_size);
            nvs_get_str(my_handle, "ssid", nvs_settings.ssid, &required_size);
        }
        // ESP_LOGI(TAG, "ssid = %s", nvs_settings.ssid==NULL?"<empty>":nvs_settings.ssid);
        err = nvs_get_str(my_handle, "password", NULL, &required_size);
        if (err == ESP_OK && required_size > 0) {
            nvs_settings.password = malloc(required_size);
            nvs_get_str(my_handle, "password", nvs_settings.password, &required_size);
        }
        // ESP_LOGI(TAG, "password = %s", nvs_settings.password==NULL?"<empty>":nvs_settings.password);

        // Close
        if (!RW) { 
            nvs_close(my_handle);
            my_handle = 0;
        }
    }

    return nvs_settings.settings;
}

void set_nvs_settings(uint16_t settings) {

    if (my_handle == 0) {
        ESP_LOGE(TAG, "Error no NVS handle");
        return;
    }

    nvs_settings.settings = settings;

    ESP_LOGI(TAG, "Updating settings in NVS ... ");
    esp_err_t err = nvs_set_u16(my_handle, "settings", nvs_settings.settings);
    ESP_LOGI(TAG, "%s", (err != ESP_OK) ? "Failed!" : "Done");

}

void set_nvs_ssid(char *ssid) {
    size_t required_size;

    if (my_handle == 0) {
        ESP_LOGE(TAG, "Error no NVS handle");
        return;
    }

    ESP_LOGI(TAG, "Updating ssid in NVS to %s", ssid);
    esp_err_t err = nvs_set_str(my_handle, "ssid", ssid);
    ESP_LOGI(TAG, "%s", (err != ESP_OK) ? "Failed!" : "Done");

    if (nvs_settings.ssid != NULL) free(nvs_settings.ssid);

    err = nvs_get_str(my_handle, "ssid", NULL, &required_size);
    if (err == ESP_OK && required_size > 0) {
        nvs_settings.ssid = malloc(required_size);
        nvs_get_str(my_handle, "ssid", nvs_settings.ssid, &required_size);
    }
    ESP_LOGI(TAG, "ssid = %s", nvs_settings.ssid);
}

void commit_nvs_settings(bool close) {
 
    if (my_handle == 0) {
        ESP_LOGE(TAG, "Error no NVS handle");
        return;
    }

    ESP_LOGI(TAG, "Committing updates in NVS ... ");
    esp_err_t err = nvs_commit(my_handle);
    ESP_LOGI(TAG, "%s", (err != ESP_OK) ? "Failed!" : "Done");

    if (close) {
        nvs_close(my_handle);
        my_handle = 0;
    }
}