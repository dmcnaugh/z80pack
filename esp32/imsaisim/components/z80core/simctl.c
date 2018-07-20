/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * This module allows operation of the system from an IMSAI 8080 front panel
 *
 * Copyright (C) 2008-2018 by Udo Munk
 *
 * History:
 * 20-OCT-08 first version finished
 * 26-OCT-08 corrected LED status while RESET is hold in upper position
 * 27-JAN-14 set IFF=0 when powered off, so that LED goes off
 * 02-MAR-14 source cleanup and improvements
 * 15-APR-14 added fflush() for teletype
 * 19-APR-14 moved CPU error report into a function
 * 06-JUN-14 forgot to disable timer interrupts when machine switched off
 * 10-JUN-14 increased fp operation timer from 1ms to 100ms
 * 09-MAY-15 added Cromemco DAZZLER to the machine
 * 01-MAR-16 added sleep for threads before switching tty to raw mode
 * 08-MAY-16 frontpanel configuration with path support
 * 06-DEC-16 implemented status display and stepping for all machine cycles
 * 26-JAN-17 bugfix for DATA LED's not always showing correct bus data
 * 13-MAR-17 can't examine/deposit if CPU running HALT instruction
 * 29-JUN-17 system reset overworked
  * 10-APR-18 trap CPU on unsupported bus data during interrupt
 * 17-MAY-18 improved hardware control
 * 08-JUN-18 moved hardware initialisation and reset to iosim
 * 11-JUN-18 fixed reset so that cold and warm start works
 */

#ifndef ESP_PLATFORM
#include <X11/Xlib.h>
#endif //!ESP_PLATFORM
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sim.h"
#include "simglb.h"
#include "config.h"
#include "frontpanel.h"
#include "memory.h"
#ifdef UNIX_TERMINAL
#include "unix_terminal.h"
#endif

#ifdef ESP_PLATFORM
#include "imsai-cp-a.h"
#include "linenoise/linenoise.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task.h"
#include "esp_timer.h"
#include "esp32_hardware.h"
#endif //ESP_PLATFORM

extern void cpu_z80(void), cpu_8080(void);
extern void reset_cpu(void), reset_io(void);

BYTE fp_led_wait;
int cpu_switch;
int reset;
int power;
int last_error;

extern BYTE fp_led_wait;
extern int cpu_switch;
extern int reset;
extern int power;
extern int last_error;

static void run_cpu(void), step_cpu(void);
void run_clicked(int, int), step_clicked(int, int);
void reset_clicked(int, int);
void examine_clicked(int, int), deposit_clicked(int, int);
void power_clicked(int, int);
#ifndef ESP_PLATFORM
static void quit_callback(void);
#endif //ESP_PLATFORM

void do_benchmark(void);

/*
 *	This function initialises the front panel and terminal.
 *	Then the machine waits to be operated from the front panel,
 *	until power switched OFF again.
*/
#ifndef ESP_PLATFORM
void mon(void)
#else
void mon2(void *);

void mon(void) {

	TaskHandle_t thisTask = NULL;
	TaskHandle_t monTask = NULL;
	int	taskRun;

	if(cpu == Z80) update_status(0x02, STATUS_SET);

	last_error = cpu_error;
	cpu_error = NONE;
	cpu_switch = 0;
	reset = 0;
	power = 0;

	thisTask = xTaskGetCurrentTaskHandle();
	taskRun = xTaskCreatePinnedToCore(mon2, "monitor", 4096, thisTask, 16, &monTask, 1);

	if(taskRun == pdPASS) {
		ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
		vTaskDelete(monTask);
		ESP_LOGI(__func__, "IMSAI Mon task deleted");
	} else {
		ESP_LOGE(__func__,"IMSAI Mon task create failed");
	}
}

void mon2(void *parentTask)
#endif //ESP_PLATFORM
{
#ifndef ESP_PLATFORM
	/* initialise front panel */
	XInitThreads();

	if (!fp_init2(&confdir[0], "panel.conf", fp_size)) {
		puts("frontpanel error");
		exit(1);
	}

	fp_addQuitCallback(quit_callback);
	fp_framerate(fp_fps);
	fp_bindSimclock(&fp_clock);
	fp_bindRunFlag(&cpu_state);

	/* bind frontpanel LED's to variables */
	fp_bindLight16("LED_ADDR_{00-15}", &fp_led_address, 1);
	fp_bindLight8("LED_DATA_{00-07}", &fp_led_data, 1);
	fp_bindLight8("LED_STATUS_{00-07}", &cpu_bus, 1);
	fp_bindLight8invert("LED_DATOUT_{00-07}", &fp_led_output, 1, 255);
	fp_bindLight8("LED_RUN", &cpu_state, 1);
	fp_bindLight8("LED_WAIT", &fp_led_wait, 1);
	fp_bindLight8("LED_INTEN", &IFF, 1);
	fp_bindLight8("LED_HOLD", &bus_request, 1);

	/* bind frontpanel switches to variables */
	fp_bindSwitch16("SW_{00-15}", &address_switch, &address_switch, 1);

	/* add callbacks for front panel switches */
	fp_addSwitchCallback("SW_RUN", run_clicked, 0);
	fp_addSwitchCallback("SW_STEP", step_clicked, 0);
	fp_addSwitchCallback("SW_RESET", reset_clicked, 0);
	fp_addSwitchCallback("SW_EXAMINE", examine_clicked, 0);
	fp_addSwitchCallback("SW_DEPOSIT", deposit_clicked, 0);
	fp_addSwitchCallback("SW_PWR", power_clicked, 0);
#else 
	ESP_LOGI(__func__, "IMSAI Mon Running on Core#%d",xPortGetCoreID());
	imsai_cpa_init(); //should be in iosim.c->init_io(), but so should all fp_* calls above
#endif //ESP_PLATFORM
	
#ifdef UNIX_TERMINAL
	/* give threads a bit time and then empty buffer */
	SLEEP_MS(999);
	fflush(stdout);

	/* initialise terminal */
	set_unix_terminal();
#endif

#ifdef HAS_BANKED_ROM
	if(r_flag)
		PC = 0x0000;
#endif

	/* operate machine from front panel */
	while (cpu_error == NONE) {
		if (reset) {
			cpu_bus = 0xff;
			fp_led_address = 0xffff;
			fp_led_data = 0xff;
		} else {
			if (power) {
				fp_led_address = PC;
				if (!(cpu_bus & CPU_INTA))
					fp_led_data = peek(PC);
				else
					fp_led_data = (int_data != -1) ?
							(BYTE) int_data : 0xff;
			}
		}

		fp_clock++;
		fp_sampleData();
		
		switch (cpu_switch) {
		case 1:
			if (!reset) run_cpu();
			break;
		case 2:
			step_cpu();
			if (cpu_switch == 2)
				cpu_switch = 0;
			break;
		default:
			break;
		}
		
		fp_clock++;
		fp_sampleData();

		SLEEP_MS(10);
	}
	
#ifdef UNIX_TERMINAL
	/* reset terminal */
	reset_unix_terminal();
	putchar('\n');
#endif

	/* all LED's off and update front panel */
	cpu_bus = 0;
	bus_request = 0;
	IFF = 0;
	fp_led_wait = 0;
	fp_led_output = 0xff;
	fp_led_address = 0;
	fp_led_data = 0;
	fp_sampleData();

	/* wait a bit before termination */
	SLEEP_MS(999);

#ifndef ESP_PLATFORM
	/* stop frontpanel */
	fp_quit();
#else
	imsai_cpa_quit(); //should be in iosim.c->exit_io(), but so should the fp_* calls above
	ESP_LOGI(__func__, "IMSAI Mon Finishing on Core#%d",xPortGetCoreID());
	xTaskNotifyGive(parentTask);
	while(1);
#endif //ESP_PLATFORM

}

/*
 *	Report CPU error
 */
void report_error(void)
{
	switch (cpu_error) {
	case NONE:
		break;
	case OPHALT:
		printf("\r\nINT disabled and HALT Op-Code reached at %04x\r\n",
		       PC - 1);
		break;
	case IOTRAPIN:
		printf("\r\nI/O input Trap at %04x, port %02x\r\n",
		       PC, io_port);
		break;
	case IOTRAPOUT:
		printf("\r\nI/O output Trap at %04x, port %02x\r\n",
		       PC, io_port);
		break;
	case IOHALT:
		printf("\r\nSystem halted, bye.\r\n");
		break;
	case IOERROR:
		printf("\r\nFatal I/O Error at %04x\r\n", PC);
		break;
	case OPTRAP1:
		printf("\r\nOp-code trap at %04x %02x\r\n",
		       PC - 1 , *(mem_base() + PC - 1));
		break;
	case OPTRAP2:
		printf("\r\nOp-code trap at %04x %02x %02x\r\n",
		       PC - 2, *(mem_base() + PC - 2), *(mem_base() + PC - 1));
		break;
	case OPTRAP4:
		printf("\r\nOp-code trap at %04x %02x %02x %02x %02x\r\n",
		       PC - 4, *(mem_base() + PC - 4), *(mem_base() + PC - 3),
		       *(mem_base() + PC - 2), *(mem_base() + PC - 1));
		break;
	case USERINT:
		printf("\r\nUser Interrupt at %04x\r\n", PC);
		break;
	case INTERROR:
		printf("\r\nUnsupported bus data during INT: %02x\r\n",
		       int_data);
		break;
	case POWEROFF:
		printf("\r\nSystem powered off, bye.\r\n");
		break;
	default:
		printf("\r\nUnknown error %d\r\n", cpu_error);
		break;
	}
}

/*
 *	Run CPU
 */
void run_cpu(void)
{
	cpu_state = CONTIN_RUN;
	cpu_error = NONE;
	switch(cpu) {
	case Z80:
		cpu_z80();
		break;
	case I8080:
		cpu_8080();
		break;
	}
	report_error();
}

/*
 *	Step CPU
 */
void step_cpu(void)
{
	cpu_state = SINGLE_STEP;
	cpu_error = NONE;
	switch(cpu) {
	case Z80:
		cpu_z80();
		break;
	case I8080:
		cpu_8080();
		break;
	}
	cpu_state = STOPPED;
	report_error();
}

extern unsigned long long b_clk;
extern long long b_start;
/*
 *	Callback for RUN/STOP switch
 */
void run_clicked(int state, int val)
{
	val = val;	/* to avoid compiler warning */

	if (!power)
		return;

	switch (state) {
	case FP_SW_UP:
		if (cpu_state != CONTIN_RUN) {
			cpu_state = CONTIN_RUN;
			fp_led_wait = 0;
			cpu_switch = 1;
			b_clk=0ULL;
			b_start=esp_timer_get_time();
		}
		break;
	case FP_SW_DOWN:
		if (cpu_state == CONTIN_RUN) {
			cpu_state = STOPPED;
			fp_led_wait = 1;
			cpu_switch = 0;
			b_clk=0ULL;
			b_start=0LL;
		}
		break;
	default:
		break;
	}
}

/*
 *	Callback for STEP switch
 */
void step_clicked(int state, int val)
{
	val = val;	/* to avoid compiler warning */

	if (!power)
		return;

	if (cpu_state == CONTIN_RUN)
		return;

	switch (state) {
	case FP_SW_UP:
	case FP_SW_DOWN:
		cpu_switch = 2;
		break;
	default:
		break;
	}
}

/*
 * Single step through the machine cycles after M1
 */
void wait_norun_step(void)
{
	// if (cpu_state != SINGLE_STEP) {
	// 	cpu_bus &= ~CPU_M1;
	// 	m1_step = 0;
	// 	return;
	// }

	// if ((cpu_bus & CPU_M1) && !m1_step) {
	// 	cpu_bus &= ~CPU_M1;
	// 	return;
	// }

	cpu_switch = 3;

	while ((cpu_switch == 3) && !reset) {
		/* when INP on port 0FFh - feed Programmed Input
		   toggles to Data Bus LEDs */
		if ((cpu_bus == (CPU_WO | CPU_INP)) &&
		    (fp_led_address == 0xffff)) {
			fp_led_data = address_switch >> 8;
		}
		fp_clock++;
		fp_sampleData();
		SLEEP_MS(10);
	}

	cpu_bus &= ~CPU_M1;
	m1_step = 0;
}

/*
 * Single step through interrupt machine cycles
 */
void wait_int_step(void)
{
	if (cpu_state != SINGLE_STEP)
		return;

	cpu_switch = 3;

	while ((cpu_switch == 3) && !reset) {
		fp_clock++;
		fp_sampleData();

		SLEEP_MS(10);
	}
}

/*
 *	Callback for RESET switch
 */
void reset_clicked(int state, int val)
{
	val = val;	/* to avoid compiler warning */

	if (!power)
		return;

	switch (state) {
	case FP_SW_UP:
		/* reset CPU only */
		reset = 1;
		cpu_state |= RESET;
		m1_step = 0;
		IFF = 0;
		fp_led_output = 0;
		break;
	case FP_SW_CENTER:
		if (reset) {
			/* reset CPU */
			reset = 0;
			reset_cpu();
			cpu_state &= ~RESET;

			/* update front panel */
			fp_led_address = 0;
			fp_led_data = peek(PC);
			cpu_bus = CPU_WO | CPU_M1 | CPU_MEMR;
		}
		break;
	case FP_SW_DOWN:
		/* reset CPU and I/O devices */
		reset = 1;
		cpu_state |= RESET;
		m1_step = 0;
		IFF = 0;
		fp_led_output = 0;
		reset_io();
		reset_memory();
		break;
	default:
		break;
	}
}

/*
 *	Callback for EXAMINE/EXAMINE NEXT switch
 */
void examine_clicked(int state, int val)
{
	val = val;	/* to avoid compiler warning */

	if (!power)
		return;

	if ((cpu_state == CONTIN_RUN) || (cpu_bus & CPU_HLTA))
		return;

	switch (state) {
	case FP_SW_UP:
		fp_led_address = address_switch;
		fp_led_data = peek(PC);
		PC = address_switch;
		break;
	case FP_SW_DOWN:
		fp_led_address++;
		fp_led_data = peek(PC);
		PC = fp_led_address;
		break;
	default:
		break;
	}
}

/*
 *	Callback for DEPOSIT/DEPOSIT NEXT switch
 */
void deposit_clicked(int state, int val)
{
	val = val;	/* to avoid compiler warning */

	if (!power)
		return;

	if ((cpu_state == CONTIN_RUN) || (cpu_bus & CPU_HLTA))
		return;

	switch (state) {
	case FP_SW_UP:
		fp_led_data = address_switch & 0xff;
		poke(PC, fp_led_data);
		break;
	case FP_SW_DOWN:
		PC++;
		fp_led_address++;
		fp_led_data = address_switch & 0xff;
		poke(PC, fp_led_data);
		break;
	default:
		break;
	}
}

/*
 *	Callback for POWER switch
 */
void power_clicked(int state, int val)
{
	val = val;	/* to avoid compiler warning */

	switch (state) {
	case FP_SW_UP:
		if (power)
			break;
		power++;
		cpu_bus = CPU_WO | CPU_M1 | CPU_MEMR;
		fp_led_address = PC;
		fp_led_data = peek(PC);
		fp_led_wait = 1;
		fp_led_output = 0;
#ifdef UNIX_TERMINAL
		if (isatty(1))
#ifdef ESP_PLATFORM
			linenoiseClearScreen();
#else
			system("tput clear");
#endif //ESP_PLATFORM
		else {
			puts("\r\n\r\n\r\n");
			fflush(stdout);
		}
#endif
		break;
	case FP_SW_DOWN:
		if (!power)
			break;
		power--;
		cpu_switch = 0;
		cpu_state = STOPPED;
		cpu_error = POWEROFF;
		break;
	default:
		break;
	}
}

#ifndef ESP_PLATFORM
/*
 * Callback for quit (graphics window closed)
 */
void quit_callback(void)
{
	power--;
	cpu_switch = 0;
	cpu_state = STOPPED;
	cpu_error = POWEROFF;
}
#endif //ESP_PLATFORM

/*
 *	Calculate the clock frequency of the emulated CPU:
 *	into memory locations 0000H to 0002H the following
 *	code will be stored:
 *		LOOP: JP LOOP
 *	It uses 10 T states for each execution. A 3 second
 *	timer is started and then the CPU. For every opcode
 *	fetch the R register is incremented by one and after
 *	the timer is down and stops the emulation, the clock
 *	speed of the CPU is calculated with:
 *		f = R /	300000
 */
#ifdef ESP_PLATFORM

static void timeout(void* arg)
{
	cpu_state = STOPPED;
}

const esp_timer_create_args_t benchmark_timer_args = {
		.callback = &timeout,
		.name = "benchmark"
};
esp_timer_handle_t benchmark_timer = NULL;

void init_timer(int timer_period_us)
{
    ESP_ERROR_CHECK(esp_timer_create(&benchmark_timer_args, &benchmark_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(benchmark_timer, timer_period_us));
}
#endif //ESP_PLATFORM

unsigned long long b_clk = 0ULL;
long long b_start;
extern unsigned long long b_clk;
extern long long b_start;
WORD ret_ptr = 0x0080;

void do_Zbenchmark(void) {

	int i;
	char result[32];
	int64_t b_end = esp_timer_get_time();
	int64_t b_time = (b_end - b_start);
	double res;

	if(b_start == 0) {
		res = 0.0;
	} else {
		res = (double) b_clk / (double) b_time;
	}

	sprintf(result, "%5.3f MHz$", res);

	for(i=0; i<strlen(result); i++) {
		*(mem_base() + ret_ptr + i) = result[i];
	}
}

void do_benchmark(void)
{
	WORD PROG_COUNTER;
	BYTE save[3];
	char result[32];
	int i;
	static struct sigaction newact;
	static struct itimerval tim;
	int64_t b_end;
	int64_t b_time;

	PROG_COUNTER = PC;

	save[0]	= *(mem_base() + 0x0000); /* save memory locations */
	save[1]	= *(mem_base() + 0x0001); /* 0000H - 0002H */
	save[2]	= *(mem_base() + 0x0002);
	*(mem_base() + 0x0000) = 0xc3;	/* store opcode JP 0000H at address */
	*(mem_base() + 0x0001) = 0x00;	/* 0000H */
	*(mem_base() + 0x0002) = 0x00;
	PC = 0;				/* set PC to this code */
	R = 0L;				/* clear refresh register */
	cpu_state = CONTIN_RUN;		/* initialise CPU */
	cpu_error = NONE;
	newact.sa_handler = (void *)timeout;	/* set timer interrupt handler */
	memset((void *) &newact.sa_mask, 0, sizeof(newact.sa_mask));
	newact.sa_flags = 0;
	sigaction(SIGALRM, &newact, NULL);
	tim.it_value.tv_sec = 3;	/* start 3 second timer */
	tim.it_value.tv_usec = 0;
	tim.it_interval.tv_sec = 0;
	tim.it_interval.tv_usec = 0;
	setitimer(ITIMER_REAL, &tim, NULL);
#ifdef ESP_PLATFORM
	init_timer(3000000);
#endif //ESP_PLATFORM
	b_clk = 0ULL;
	b_start = esp_timer_get_time();
	switch(cpu) {			/* start CPU */
	case Z80:
		cpu_z80();
		break;
	case I8080:
		cpu_8080();
		break;
	}
	b_end = esp_timer_get_time();
	b_time = b_end - b_start;
	// newact.sa_handler = SIG_DFL;	/* reset timer interrupt handler */
	sigaction(SIGALRM, &newact, NULL);
	*(mem_base() + 0x0000) = save[0]; /* restore memory locations */
	*(mem_base() + 0x0001) = save[1]; /* 0000H - 0002H */
	*(mem_base() + 0x0002) = save[2];
	// printf("Benchmark finished\n");
	if (cpu_error == NONE)
		sprintf(result, "%5.3f MHz$", ((double) b_clk) / (double) b_time);
	else
		sprintf(result, "ERROR$");

	for(i=0; i<strlen(result); i++) {
		*(mem_base() + ret_ptr + i) = result[i];
	}

#ifdef ESP_PLATFORM
	if(benchmark_timer != NULL) {
		ESP_ERROR_CHECK(esp_timer_delete(benchmark_timer));
	}
 #endif //ESP_PLATFORM

	PC = PROG_COUNTER;
	
}