/**
 *
 */
#undef USR_COM
#undef USR_REL
#undef USR_CPR
// #undef HAS_DAZZLER      // Remove references to Cromemeco Dazzler IO device
#define IMSAI_VIO    // Remove references to IMSAI VIO device

#define USR_COM	"ESP32 IMSAI 8080 Simulation"
#define USR_REL	"1.18-dev (ESP32)"
#define USR_CPR	"Copyright (C) 2008-2019 by Udo Munk & David McNaughton"

#define CONFDIR CONFIG_CONFDIR
#define DISKSDIR CONFIG_DISKSDIR
#define BOOTROM CONFIG_BOOTROM
#define CORE_PATH CONFIG_CORE_PATH
#define	CORE_FILE	CORE_PATH "/core.z80"

#undef MAX_LFN
#define MAX_LFN CONFIG_FATFS_MAX_LFN

#undef SLEEP_MS
#define SLEEP_MS(t) usleep(t)

#include "esp_system.h"
#include "esp_log.h"
#include <unistd.h>

#include <signal.h>
#include <sys/time.h>

/**
 *  Benchmarking variables
 */
extern WORD ret_ptr;
extern unsigned long long b_clk;

/**
 *  Stubs to overcome missing vio variables
 */
extern char bg_color[];
extern char fg_color[];
extern int slf;

/**
 *  Stubs to overcome missing functions from frontpanel
 */

extern void sample_callback(void);

extern int sampdiv;
extern BYTE	cpu_state;

static inline void	fp_sampleData(void)
{
	if((cpu_state == CONTIN_RUN) && (sampdiv++ < 2000)) return; //todo: make this time/timer based not count based e.g. @ 60Hz or 600Hz or a multiple of 50Hz
	sampdiv = 0;
    sample_callback();
}

static inline void	fp_sampleLightGroup(int groupnum, int clockwarp) 
{
	if((cpu_state == CONTIN_RUN) && (sampdiv++ < 2000)) return; //todo: make this time/timer based not count based e.g. @ 60Hz or 600Hz or a multiple of 50Hz
	sampdiv = 0;
    sample_callback();
}

/**
 *  Stubs to overcome missing functions in ESP32-IDF
 */
static inline int nanosleep (const struct timespec *req, struct timespec *rem) {
	long int usec;

	usec = req->tv_sec * 1000000L;
	usec += req->tv_nsec / 1000L;

	if(usec > 0) return usleep(usec);
	return 0;
}

/**
 *  Macros to overcome missing functions in ESP32-IDF
 */
#define sigaction(...) 	while(0) {};
#define setitimer(a, b, ...) 	while(0) { *b = *b; };
// #define tcsetattr(...) 	while(0) {};
#define basename(path) path