#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "tcpip_adapter.h"
// #include "esp_wifi.h"

#include "esp32_netsrv.h"
#include "esp32_hardware.h"

#define PIN_NUM_MISO GPIO_NUM_19
#define PIN_NUM_MOSI GPIO_NUM_23
#define PIN_NUM_CLK  GPIO_NUM_18
#define PIN_NUM_CS   -1

#define PIN_NUM_DSK_LAT	GPIO_NUM_22

#define LATCH_DSK_LEDS	gpio_set_level(PIN_NUM_DSK_LAT, HIGH); gpio_set_level(PIN_NUM_DSK_LAT, LOW);

static const char* TAG = "esp32_hardware";

esp_interface_t netIface = ESP_IF_WIFI_STA;
int cpa_attached;
uint8_t dip_settings = 0;

spi_device_handle_t ledspi;
spi_device_handle_t swspi;
static spi_bus_config_t buscfg={
	.miso_io_num=PIN_NUM_MISO,
	.mosi_io_num=PIN_NUM_MOSI,
	.sclk_io_num=PIN_NUM_CLK,
	.quadwp_io_num=-1,
	.quadhd_io_num=-1
};
static spi_device_interface_config_t ledscfg={
	.command_bits=0,
	.address_bits=0,
	.clock_speed_hz=40*1000*1000,           //Clock out at 40 MHz
	.mode=0,                                //SPI mode 0
	.spics_io_num=PIN_NUM_CS,               //CS pin
	.queue_size=7,                          //We want to be able to queue 7 transactions at a time
	// .pre_cb=lcd_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
};
static spi_device_interface_config_t swcfg={
	.command_bits=0,
	.address_bits=0,
	.clock_speed_hz=10*1000*1000,           //Clock out at 10 MHz
	.mode=0,                                //SPI mode 0
	.spics_io_num=PIN_NUM_CS,               //CS pin
	.queue_size=7,                          //We want to be able to queue 7 transactions at a time
	// .pre_cb=lcd_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
};

void tx_leds(spi_device_handle_t spi, uint8_t* data) 
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=48;                    //Command is 48 bits
    t.tx_buffer=data;               //The data itself
	ESP_ERROR_CHECK( spi_device_transmit(spi, &t) );  //Transmit!
}

static void tx_status_leds(spi_device_handle_t spi, uint8_t* data) 
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=8;                    //Command is 48 bits
    t.tx_buffer=data;               //The data itself
	ESP_ERROR_CHECK( spi_device_transmit(spi, &t) );  //Transmit!
}

void rx_switches(spi_device_handle_t spi, uint8_t* data)
{
	spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
	t.length=32;					//Command is 32 bits
    t.rx_buffer=data;               //The data itself
	ESP_ERROR_CHECK( spi_device_transmit(spi, &t) );  //Transmit!
}

static uint8_t status_leds = 0;
static uint8_t status_bits = 0;
static uint8_t status_pulse_bits = 0;

void update_status(int data, int mode)
{
    if(mode & STATUS_SET) { status_bits |= data; status_pulse_bits &= ~data; }
    if(mode & STATUS_FLASH) status_pulse_bits = data;
    if(mode & STATUS_FORCE) { status_bits = data; status_leds = data; }
    
    status_leds = (status_leds & status_pulse_bits) | status_bits;
	tx_status_leds(ledspi, &status_leds);
	LATCH_DSK_LEDS;
}

void post_flash(void *arg) {
        status_leds = (status_leds ^ status_pulse_bits) | status_bits;
	    tx_status_leds(ledspi, &status_leds);
	    LATCH_DSK_LEDS;
}

const esp_timer_create_args_t post_timer_args = {
            .callback = &post_flash,
            .name = "post_timer"
    };
static esp_timer_handle_t post_timer = NULL;

void start_post_flash_timer(void) {

    ESP_ERROR_CHECK(esp_timer_create(&post_timer_args, &post_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(post_timer, 250000));
    ESP_LOGI(TAG, "IMSAI POST flash timer started");
}

void stop_post_flash_timer(void) {
    if(post_timer != NULL) {
		ESP_ERROR_CHECK(esp_timer_stop(post_timer));
		ESP_ERROR_CHECK(esp_timer_delete(post_timer));
	}

    ESP_LOGI(TAG, "IMSAI POST flash timer stopped");
}

void initialise_hardware(void) {

    ESP_LOGI(TAG, "IMSAI Shell Running on Core#%d",xPortGetCoreID());
    start_post_flash_timer();

    //Initialize the SPI bus
    ESP_ERROR_CHECK( spi_bus_initialize(VSPI_HOST, &buscfg, 1) );
    //Attach the LEDs to the SPI bus
	ESP_ERROR_CHECK( spi_bus_add_device(VSPI_HOST, &ledscfg, &ledspi) );
    //Attach the SWs to the SPI bus
	ESP_ERROR_CHECK( spi_bus_add_device(VSPI_HOST, &swcfg, &swspi) );
	
	//Initialize non-SPI GPIOs
	gpio_set_direction(PIN_NUM_LED_LAT, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_NUM_DSK_LAT, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_NUM_SW_LAT, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_NUM_SW_OE, GPIO_MODE_OUTPUT);

	//Reset the GPIOs
	gpio_set_level(PIN_NUM_LED_LAT, LOW);
	gpio_set_level(PIN_NUM_DSK_LAT, LOW);
	gpio_set_level(PIN_NUM_SW_LAT, HIGH);
	gpio_set_level(PIN_NUM_SW_OE, HIGH);

	// usleep(50000);

	// Run test display pattern
    update_status(0, STATUS_FORCE);

	int t;
    for(t=1; t<0x81; t=t<<1) {
        update_status(t, STATUS_FORCE);
        usleep(30000);
    }
    for(t=0x80; t>0; t=t>>1) {
        update_status(t, STATUS_FORCE);
        usleep(30000);
    }

	// Clear display
	update_status(0, STATUS_FORCE);

    /**
     * Initialise SD card
     *
     * 
     */
    update_status(0x40, STATUS_FLASH);
    ESP_LOGI(TAG, "Initializing SD card");
    
    ESP_LOGI(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // To use 1-line SD mode, uncomment the following line:
    // host.flags = SDMMC_HOST_FLAG_1BIT;

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    
    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 10
    };

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    sleep(1);
    update_status(0, STATUS_FLASH);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%d). "
                "Make sure SD card lines have pull-up resistors in place.", ret);
        }
        return;
    }

    update_status(0x40, STATUS_SET);
    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

	//Check CP-A connected by checking switches for PWR.ON or PWR.OFF
    update_status(0x80, STATUS_FLASH);
    sleep(1);

    uint8_t sws[4];
	LATCH_SWITCHES;
	gpio_set_level(PIN_NUM_SW_OE, LOW);
	rx_switches(swspi, sws);
	gpio_set_level(PIN_NUM_SW_OE, HIGH);

	if(sws[3]) { 
		cpa_attached = 1;
        dip_settings = sws[2] >> 4;
        update_status(0x80, STATUS_SET);
		ESP_LOGI(TAG, "CP-A Found - Power Switch %X", sws[3]);
	} else {
		cpa_attached = 0;
        update_status(0, STATUS_FLASH);
		ESP_LOGI(TAG, "CP-A NOT Found - Power Switch %X", sws[3]);
	}

    return;
}

void initialise_network(void) {
    /**
     * Initialise WIFI
     *
     * 
     */
        
    esp_log_level_set("wifi", ESP_LOG_WARN);
    // static bool wifi_initialized = false;
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK( nvs_flash_init() );
    tcpip_adapter_init();
    
    update_status(0x20, STATUS_FLASH);

    if((dip_settings & 0x01) || esp_sleep_get_wakeup_cause())  {
        netIface = ESP_IF_WIFI_AP;
    } else {
        if(getenv("SSID") != NULL) {
            netIface = ESP_IF_WIFI_STA;
        } else {
            netIface = ESP_IF_WIFI_AP;
        }
    }
    
    ESP_ERROR_CHECK( esp_event_loop_init(wifi_event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    // ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode((netIface==ESP_IF_WIFI_AP)?WIFI_MODE_AP:WIFI_MODE_STA));
 
    uint8_t mac[6];
    ESP_ERROR_CHECK( esp_wifi_get_mac(netIface, mac) );
    ESP_LOGI(TAG, "MAC ADDR: %x:%x:%x:%x:%x:%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    update_status(0x10, STATUS_SET);
    // wifi_initialized = true;

    return;
}

void stop_spi() {
	//Detach the LEDs from the SPI bus
	if(ledspi)
    	ESP_ERROR_CHECK( spi_bus_remove_device(ledspi) );
	//Detach the SWs from the SPI bus
	if(swspi)
    	ESP_ERROR_CHECK( spi_bus_remove_device(swspi) );
	//Free the SPI bus
    ESP_ERROR_CHECK( spi_bus_free(VSPI_HOST) );
}