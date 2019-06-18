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

#define DIP_1   0x80
#define DIP_2   0x40
#define DIP_3   0x20
#define DIP_4   0x10

#define T_EXAMINE_NEXT    0x08
#define T_EXAMINE         0x04
#define T_DEPOSIT_NEXT    0x02
#define T_DEPOSIT         0x01
#define T_EXT_RESET       0x80
#define T_RESET           0x40
#define T_STOP            0x20
#define T_RUN             0x10
#define T_STEP_DOWN       0x08
#define T_STEP_UP         0x04
#define T_POWER_OFF       0x02
#define T_POWER_ON        0x01

#define EXAMINE_NEXT(s)    (s[1] & T_EXAMINE_NEXT)
#define EXAMINE(s)         (s[1] & T_EXAMINE)
#define DEPOSIT_NEXT(s)    (s[1] & T_DEPOSIT_NEXT)
#define DEPOSIT(s)         (s[1] & T_DEPOSIT)
#define EXT_RESET(s)       (s[0] & T_EXT_RESET)
#define SW_RESET(s)        (s[0] & T_RESET)
#define STOP(s)            (s[0] & T_STOP)
#define RUN(s)             (s[0] & T_RUN)
#define STEP_DOWN(s)       (s[0] & T_STEP_DOWN)
#define STEP_UP(s)         (s[0] & T_STEP_UP)
#define POWER_OFF(s)       (s[0] & T_POWER_OFF)
#define POWER_ON(s)        (s[0] & T_POWER_ON)

#define NVS_POST            (nvs_settings & 0x01)
#define NVS_NO_POST         (!NVS_POST)
#define NVS_LOG_LEVEL       ((nvs_settings & 0x06) >> 1)
#define NVS_IF_STA          ((nvs_settings & 0x08) >> 3)
#define NVS_IF_AP           (!NVS_IF_STA)
#define NVS_Z80             ((nvs_settings & 0x10) >> 4)
#define NVS_I8080           (!NVS_Z80)
#define NVS_NO_UNDOC        ((nvs_settings & 0x20) >> 5)
#define NVS_UNDCO           (!NVS_NO_UNDOC)
#define NVS_4MHZ            ((nvs_settings & 0x40) >> 6)
#define NVS_2MHZ            (!NVS_4MHZ)
#define NVS_UNLIMITED       ((nvs_settings & 0x80) >> 7)
#define NVS_BOOT_ROM        ((nvs_settings & 0x700) >> 8)
#define NVS_BANK_ROM        ((nvs_settings & 0x800) >> 11)

extern spi_device_handle_t ledspi;
extern spi_device_handle_t swspi;
extern void tx_leds(spi_device_handle_t spi, uint8_t* data) ;
extern void rx_switches(spi_device_handle_t spi, uint8_t* data);

extern void update_status(int data, int mode);

extern int cpa_attached;
extern uint16_t nvs_settings;

extern esp_interface_t netIface;