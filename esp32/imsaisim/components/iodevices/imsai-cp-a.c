/*
 * IMSAI CP-A reproduction simulator
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#ifndef ESP_PLATFORM
#include <termios.h>
#include <fcntl.h>
#endif //ESP_PLATFORM

#include "sim.h"
#include "simglb.h"
#include "frontpanel.h"

#ifdef ESP_PLATFORM
#include "esp32_hardware.h"
#include "esp_timer.h"
#endif //ESP_PLATFORM

#define PANEL_ENV "PANEL"

#ifndef ESP_PLATFORM
static int extPanel = -1;
static struct termios oldtio, newtio;
static char *extPanelDevice;
static char extBuffer[255];
static ssize_t extIn;
static char hexstr[10];
static char *ptr;
#endif //ESP_PLATFORM
static WORD key_switch, run_switch;
static BYTE control_switch;

static int dirty = 0;
int sampdiv = 0;

extern BYTE fp_led_wait;
extern int cpu_switch;
extern int reset;
extern int power;
extern void power_clicked(int, int);
extern void run_clicked(int, int);
extern void examine_clicked(int, int);
extern void deposit_clicked(int, int);
extern void step_clicked(int, int);
extern void reset_clicked(int, int);

static void draw_callback(void*);
extern void sample_callback(void);

extern void cpaSend(char *);

#ifdef ESP_PLATFORM
WORD keySW, keySHOT;

static uint8_t leds[6];
static uint8_t sws[4];

static char cpa_status[6];
static char last_cpa_status[6];

void update_cpa(bool force) {
    cpa_status[0] = cpa_attached?(power?'U':'D'):'E';
    cpa_status[1] = IFF?'I':' ';
    cpa_status[2] = cpu_state?'R':' ';
    cpa_status[3] = fp_led_wait?'W':' ';
    cpa_status[4] = bus_request?'H':' ';
    cpa_status[5] = 0;
	if(force || strncmp(cpa_status, last_cpa_status, 6)) {
		cpaSend(cpa_status);
		strncpy(last_cpa_status, cpa_status, 6);
	};
};

const esp_timer_create_args_t cpa_timer_args = {
            .callback = &draw_callback,
            .name = "cpa_timer"
    };
static esp_timer_handle_t cpa_timer = NULL;

void init_cpa_timer(int timer_period_us) {
    ESP_ERROR_CHECK(esp_timer_create(&cpa_timer_args, &cpa_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(cpa_timer, timer_period_us));
}

#endif //ESP_PLATFORM

void imsai_cpa_init(void) {

	strcpy(last_cpa_status, "     ");

#ifndef ESP_PLATFORM
	extPanelDevice = getenv(PANEL_ENV);

	if (!extPanelDevice) {
		return;
	} else {
		extPanel = open(extPanelDevice, O_RDWR | O_NOCTTY | O_NONBLOCK);
		if(extPanel != -1) {
			printf("Connected to external panel at %s\n", extPanelDevice);

			fp_addDrawCallback(draw_callback);
			fp_addSampleCallback(sample_callback);
			fp_bindSwitch8("SW_PWR", &control_switch, &control_switch, 1);

			fcntl(extPanel, F_SETFL, 0);
			tcgetattr(extPanel, &oldtio);

			bzero(&newtio, sizeof(newtio));
			newtio.c_cflag = /* B115200 |*/ CRTSCTS | CS8 | CLOCAL | CREAD;
			newtio.c_iflag = IGNPAR;
			newtio.c_oflag = 0;
			
			// set input mode (non-canonical, no echo,...)
			newtio.c_lflag = 0;
			// cfmakeraw(&newtio);

			newtio.c_cc[VTIME]    = 0;   // inter-character timer unused
			newtio.c_cc[VMIN]     = 1;   // blocking read until 1 chars received
			
			tcflush(extPanel, TCIFLUSH);
			tcsetattr(extPanel,TCSANOW,&newtio);

			write(extPanel, "I", 1);
			extIn = read(extPanel, extBuffer, 255);
			extBuffer[extIn] = 0;
			printf("%s\n", extBuffer);

		}
	}
#else	

	// Run test display pattern
	// long t1, t2;
	// for(t1=1; t1<2; t1=t1<<1) {
	// 	for(t2=1; t2<0x8001; t2=t2<<1) {
	// 		// leds[5] = t2 >> 8;
	// 		// leds[4] = t2 >> 8;
	// 		// leds[3] = t2 >> 8;
	// 		// leds[2] = t2 & 0xff;
	// 		// leds[1] = t2 & 0xff;
	// 		// leds[0] = t1;
	// 		// tx_leds(ledspi, leds);
	// 		// LATCH_DSK_LEDS;
	// 		update_status(t2 >> 8, STATUS_FORCE);
	// 	    usleep(30000);
	// 	}
	// 	for(t2=0x8000; t2>0; t2=t2>>1) {
	// 		// leds[5] = t2 >> 8;
	// 		// leds[4] = t2 >> 8;
	// 		// leds[3] = t2 >> 8;
	// 		// leds[2] = t2 & 0xff;
	// 		// leds[1] = t2 & 0xff;
	// 		// leds[0] = t1;
	// 		// tx_leds(ledspi, leds);
	// 		// LATCH_DSK_LEDS;
	// 		update_status(t2 >> 8, STATUS_FORCE);
	// 		usleep(30000);
	// 	}
	// }

	// Clear display
	// leds[0] = 0;
	// leds[1] = 0;
	// leds[2] = 0;
	// leds[3] = 0;
	// leds[4] = 0;
	// leds[5] = 0; // runSWBITS; // turn-on all keySW bits
	// tx_leds(ledspi, leds);
	// LATCH_DSK_LEDS;
	// update_status(0, STATUS_FORCE);

	keySHOT = 0x0FFF;

	// if(cpa_attached) { 
		init_cpa_timer(40000); // 10000Hz / 400 = 25Hz
	// } else {
		//Autorun if no CP-A
		// power_clicked(FP_SW_UP, 0);
		// run_clicked(FP_SW_UP, 0);
	// }
#endif //ESP_PLATFORM
}

void imsai_cpa_quit(void) {

#ifndef ESP_PLATFORM
	if(extPanel != -1) {
		tcsetattr(extPanel,TCSANOW,&oldtio);
		close(extPanel);
	}
#endif //ESP_PLATFORM

    if(cpa_timer != NULL) {
		ESP_ERROR_CHECK(esp_timer_stop(cpa_timer));
		ESP_ERROR_CHECK(esp_timer_delete(cpa_timer));
	}
}

/*
 * Callback for panel->draw (graphics window update)
 */
static void draw_callback(void *arg)
{
	dirty++;
}

void poll_switches(void) {

	if(!dirty) return;
	dirty=0;

#ifndef ESP_PLATFORM
	unsigned int len = sprintf(extBuffer, "S");
	write(extPanel, extBuffer, len);

	extIn = read(extPanel, extBuffer, 255);
	extBuffer[extIn] = 0;

	memcpy(hexstr, &extBuffer[4], 4);
	hexstr[4]=0;
	address_switch = strtoul(hexstr, &ptr, 16);

	memcpy(hexstr, &extBuffer[2], 2);
	hexstr[2]=0;
	key_switch = strtoul(hexstr, &ptr, 16);

	memcpy(hexstr, &extBuffer[0], 2);
	hexstr[2]=0;
	run_switch = strtoul(hexstr, &ptr, 16);

#else
	LATCH_SWITCHES;
	gpio_set_level(PIN_NUM_SW_OE, LOW);
	rx_switches(swspi, sws);
	gpio_set_level(PIN_NUM_SW_OE, HIGH);

	//ESP_LOGI(__func__, "Switches %02X %02X %02X %02X", sws[3], sws[2], sws[1], sws[0]);

	address_switch = sws[0] << 8 | sws[1];
	keySW = (sws[2] << 8 | sws[3]); // & keySHOT;
	key_switch = keySW & keySHOT;
	keySHOT = ~ (keySW & 0b0000111100111100) ; // Don't one-shot the RESET/RST.CLR. switch

	// if(key_switch) ESP_LOGI(__func__, "Key Switch %X", key_switch);

#endif //ESP_PLATFORM

	if(key_switch & 0x04) step_clicked(FP_SW_UP, 0);
	if(key_switch & 0x08) step_clicked(FP_SW_DOWN, 0);
	if(key_switch & 0x40) reset_clicked(FP_SW_UP, 0);
	if(reset && !(key_switch & 0x40)) reset_clicked(FP_SW_CENTER, 0); // but its a one-shot
	if(key_switch & 0x80) reset_clicked(FP_SW_DOWN, 0);
	if(key_switch & 0x100) deposit_clicked(FP_SW_UP, 0);
	if(key_switch & 0x200) deposit_clicked(FP_SW_DOWN, 0);
	if(key_switch & 0x400) examine_clicked(FP_SW_UP, 0);
	if(key_switch & 0x800) examine_clicked(FP_SW_DOWN, 0);

	if(!power && (key_switch & 0x01)) { // PWR UP (ON)
		control_switch |= 0x01;
		power_clicked(FP_SW_UP, 0);	
	}
	else if(power && (key_switch & 0x02)) { // PWR DOWN (OFF)
		control_switch &= 0xFE;
		power_clicked(FP_SW_DOWN, 0);
	}

	if(key_switch & 0x10) run_clicked(FP_SW_UP, 0);
	if(key_switch & 0x20) run_clicked(FP_SW_DOWN, 0);

	update_cpa(false);

	// fp_sampleSwitches();

}

void net_switches(char *data) {
	    switch (data[0]) {
        case 's':
            if(data[1] == 'u') step_clicked(FP_SW_UP, 0);
            if(data[1] == 'd') step_clicked(FP_SW_UP, 0); 
            break;          
        case 'c':
            if(data[1] == 'u') reset_clicked(FP_SW_UP, 0);
            if(data[1] == 'd') reset_clicked(FP_SW_DOWN, 0);
            break;          
        case 'd':
            if(data[1] == 'u') deposit_clicked(FP_SW_UP, 0);
            if(data[1] == 'd') deposit_clicked(FP_SW_DOWN, 0); 
            break;          
        case 'e':
            if(data[1] == 'u') examine_clicked(FP_SW_UP, 0);
            if(data[1] == 'd') examine_clicked(FP_SW_DOWN, 0); 
            break;          
        case 'r':
            if(data[1] == 'u') run_clicked(FP_SW_UP, 0);
            if(data[1] == 'd') run_clicked(FP_SW_DOWN, 0); 
            break;   
		case 'p':
			if(!cpa_attached) {
				if(data[1] == 'u') power_clicked(FP_SW_UP, 0);
				if(data[1] == 'd') power_clicked(FP_SW_DOWN, 0); 
			}
			break;
        default:
            ESP_LOGE(__func__, "CPA WS message error [%c%c]", data[0],data[1]);
    }

	// if(reset && !(key_switch & 0x40)) reset_clicked(FP_SW_CENTER, 0); // but its a one-shot
}

void sample_callback(void)
{	
#ifndef ESP_PLATFORM
	if((cpu_state == CONTIN_RUN) && (sampdiv++ < 2000)) return;
	sampdiv = 0;

	unsigned int len = sprintf(extBuffer, "L%04X%02X%02X%02X%02X",
		(unsigned int)fp_led_address, 
		(unsigned int)fp_led_data, 
		(unsigned int)cpu_bus, 
		(unsigned int)fp_led_output,
		(unsigned int)(( IFF << 3 | (cpu_state & 1) << 2 | fp_led_wait << 1 | bus_request ) & 0x0F)
	);
	write(extPanel, extBuffer, len);
#else
	if(cpa_attached) {
		leds[5] = ~fp_led_output;
		leds[4] = cpu_bus;
		leds[3] = fp_led_address >> 8;
		leds[2] = fp_led_data;
		leds[1] = fp_led_address & 0xff;
		leds[0] = (( IFF << 3 | (cpu_state & 1) << 2 | fp_led_wait << 1 | bus_request ) & 0x0F); // | runSWBITS; // turn-on keySW bits
		tx_leds(ledspi, leds);
		LATCH_LEDS;
	}
#endif //ESP_PLATFORM

	poll_switches();

}

void tx_switches(void)
{
	dirty = 1;
	poll_switches();
	printf("%02X%02X%02X%02X", run_switch, key_switch & keySHOT, address_switch & 0xFF, address_switch >> 8);
}

unsigned long getFromHex (char *str, int len) {
    char hexstr[20];
    char *ptr;
  
    memcpy(hexstr, str, len);
    hexstr[len]=0;
  
    return strtoul(hexstr, &ptr, 16);
}

void rx_leds(void)
{
	char buff[20];
    BYTE control_byte;

	fgets(buff, 12, stdin);
	ESP_LOGI(__func__, "BUFF: [%s]", buff);
	
	fp_led_address = getFromHex(&buff[0], 4);
	fp_led_data = getFromHex(&buff[4], 2);
	cpu_bus = getFromHex(&buff[6], 2);
	fp_led_output = getFromHex(&buff[8], 2);
	control_byte = getFromHex(&buff[10], 2);

	IFF = control_byte >> 3;
	cpu_state = (control_byte >>2) & 1;
	fp_led_wait = (control_byte >>1) & 1;
	bus_request = (control_byte) & 1;
	
	dirty = 0;
	sample_callback();
}