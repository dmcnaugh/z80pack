#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_system.h"
#include "esp_log.h"

#include "sim.h"
#include "esp32_hardware.h"

static const char* TAG = "esp32_boot";

extern int cpu_error;
extern int main(int argc, char *argv[]);
char *sim_args[32];
int sim_arg_count = 0;

#define BUFSIZE CONFIG_FATFS_MAX_LFN

void get_boot_env(void) {
    FILE *fp;
    char buf[BUFSIZE];
    char *s, *t1, *t2;
    int i = 0;
    int line=0;

    ESP_LOGI(TAG, "OPENING BOOT.CONF");
    
    strcpy(buf, CONFIG_CONFDIR);
    strcat(buf, "/boot.conf");
    if ((fp = fopen(buf, "r")) != NULL) {
        ESP_LOGI(TAG, "OPENED: %s", buf);
        buf[0] = '\0';
		s = &buf[0];
		while (fgets(s, BUFSIZE, fp) != NULL) {
            line++;
            ESP_LOGD(TAG, "BOOT.CONF LINE[%d]: %s", line, s);
            if ((*s == '\n') || (*s == '\r') || (*s == '#'))
                continue;
            t1 = strtok(s, "=");
            t2 = strtok(NULL, "\n\r#");
            setenv(t1, t2, 1);
		}
		fclose(fp);
    }

    extern char **environ;
    i = 0;
    while(environ[i] != NULL) {
        ESP_LOGI(TAG, "ENV: %s", environ[i++]);        
    }
   
    if((s = getenv("SIM_ARGS")) != NULL) { 
        strcpy(buf, s);
        t1 = strtok(buf, " ");
        do {
            ESP_LOGD(TAG, "SIM_ARGS TOK: %s", t1);
            sim_args[sim_arg_count++] = strdup(t1);
        } while((t1 = strtok(NULL, " ")) != NULL);
        for(i=0; i<sim_arg_count; i++) {
            ESP_LOGI(TAG, "SIM_ARG[%d] = %s",i, sim_args[i]);
        }
    }
}

void set_time(void) {
    time_t now;
    struct tm timeinfo;
    int retry = 0;
    const int retry_count = 10;
    char strftime_buf[64];

    update_status(0x04, STATUS_FLASH);

    tzset(); // Set timezone from TZ environment variable
    time(&now);
    localtime_r(&now, &timeinfo);
    while(timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        usleep(2000);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s [%ld]", strftime_buf, now);
    
    update_status(0x04, STATUS_SET); 
}

extern void initialise_hardware(void);
extern void initialise_network(void);
extern void start_wifi_services(void);
extern void stop_post_flash_timer(void);
void reboot(int);

void app_main()
{

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("tcpip_adapter", ESP_LOG_INFO);
    esp_log_level_set("nvs", ESP_LOG_INFO);
    esp_log_level_set("heap_init", ESP_LOG_WARN);
    esp_log_level_set("intr_alloc", ESP_LOG_WARN);
 
    initialise_hardware();

    sim_args[sim_arg_count++] = strdup("imsai");
    if(dip_settings & 0x04) {
        sim_args[sim_arg_count++] = strdup("-z");
    } else {
        sim_args[sim_arg_count++] = strdup("-8");
    } 
    get_boot_env();

    if(dip_settings & 0x08) {
        sim_args[sim_arg_count++] = strdup("-x /sdcard/imsai/mpu-a-vio-rom.hex");
    }
    if(dip_settings & 0x02) {
        sim_args[sim_arg_count++] = strdup("-f 0");
    }

    initialise_network();
    start_wifi_services();
    set_time();

    sleep(2);
    stop_post_flash_timer();

    do {
        main(sim_arg_count, sim_args);
    } while (cpu_error == POWEROFF);

    ESP_LOGI(__func__,"CPU_ERROR: %d", cpu_error);
#ifdef CONFIG_IMSAI_AUTOSTART
    reboot(2000000);
#else
    ESP_LOGI(__func__,"Harware Reset required to Restart");
#endif //CONFIG_IMSAI_AUTOSTART
}

static void timeout(void* arg)
{
    esp_restart();
}

const esp_timer_create_args_t reboot_timer_args = {
		.callback = &timeout,
		.name = "reboot"
};
esp_timer_handle_t reboot_timer = NULL;

void reboot(int timer_period_us)
{
    ESP_LOGI(__func__,"Reboot in %5.3f sec.", (float) timer_period_us / 1000000.0);

    ESP_ERROR_CHECK(esp_timer_create(&reboot_timer_args, &reboot_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(reboot_timer, timer_period_us));
};