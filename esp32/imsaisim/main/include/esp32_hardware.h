#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_wifi.h"

#define STATUS_SET 1
#define STATUS_FLASH 2
#define STATUS_ALL 3
#define STATUS_FORCE 4

#define PIN_NUM_LED_LAT	GPIO_NUM_21
#define PIN_NUM_SW_LAT	GPIO_NUM_25
#define PIN_NUM_SW_OE	GPIO_NUM_26

#define LOW 0
#define HIGH 1

#define LATCH_LEDS	    gpio_set_level(PIN_NUM_LED_LAT, HIGH); gpio_set_level(PIN_NUM_LED_LAT, LOW);
#define LATCH_SWITCHES	gpio_set_level(PIN_NUM_SW_LAT, LOW); gpio_set_level(PIN_NUM_SW_LAT, HIGH);

extern spi_device_handle_t ledspi;
extern spi_device_handle_t swspi;
extern void tx_leds(spi_device_handle_t spi, uint8_t* data) ;
extern void rx_switches(spi_device_handle_t spi, uint8_t* data);

extern void update_status(int data, int mode);

extern int cpa_attached;
extern uint8_t dip_settings;

extern esp_interface_t netIface;