/**
 * cromemco-88ccc.c
 * 
 * Emulation of the Cromemco 88 CCC - Cyclops Camera Controller
 *
 * Copyright (C) 2018 by David McNaughton
 * 
 * History:
 * 14-AUG-18    1.0     Initial Release
 */

#define HAS_NETSERVER

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include "sim.h"
#include "simglb.h"
#include "config.h"
#include "frontpanel.h"
#include "memory.h"
#ifdef HAS_NETSERVER
#include "netsrv.h"
#endif
// #define LOG_LOCAL_LEVEL LOG_DEBUG
#include "log.h"
#include "esp_task.h"
#include "freertos/freertos.h"
#include "freertos/task.h"

static const char *TAG = "88CCC";

/* 88CCC stuff */
static int state;
static WORD dma_addr;
static BYTE flags;
static BYTE format;
#define FIELDSIZE (1024 / 8)
BYTE buffer[FIELDSIZE];

#ifdef ESP_PLATFORM
static TaskHandle_t thread = NULL;
#else 
/* UNIX stuff */
static pthread_t thread = 0;
#endif

/* thread for requesting, receiving & storing the camera image using DMA */
#ifndef ESP_PLATFORM
static void *store_image(void *arg)
{
	extern int time_diff(struct timeval *, struct timeval *);
	struct timeval t1, t2;
	int tdiff;
#else
void store_image(void) {
	// int64_t t1, t2, tdiff;
	unsigned int frame = 0;
#endif

	int i, j, len;
	// BYTE buffer[FIELDSIZE];
	struct {
		BYTE fields;
		BYTE interval;
		BYTE auxiliary;
		BYTE bias;
	} msg;
	BYTE msgB;

	msg.fields = 0; 
	msg.interval = 0;

#ifndef ESP_PLATFORM
	UNUSED(arg);	/* to avoid compiler warning */
	gettimeofday(&t1, NULL);
#else
	// t1 = esp_timer_get_time();
#endif
while(true) {
	while (state) {	/* do until total frame is recieved */
		if (cpu_state == CONTIN_RUN)
			bus_request = 1;

		if (net_device_alive(DEV_88ACC)) {

			msg.auxiliary = flags & 0x0f;
			msg.bias = (flags & 0x20) >> 5;
			msg.fields = format & 0x0f;
			msg.interval = (format & 0x30) >> 4;
 
			msgB = format | (msg.bias << 6);

			LOGD(TAG, "CCC/ACC Capture: to addr %04x, fields: %d, interval %d", dma_addr, msg.fields, msg.interval);

			net_device_send(DEV_88ACC, (char *)&msgB, sizeof(msgB));

			for (i = 0; i < msg.fields; i++) {
				len = net_device_get_data(DEV_88ACC, (char *) buffer, FIELDSIZE);

				if (len != FIELDSIZE) {
					LOGW(TAG,"[Frame: %d] Error in field[%d] length, recieved %d of %d bytes.", frame, i, len, FIELDSIZE);
				} else {
					// LOGD(TAG, "received field %d, length %d, %d stored at %04x", i, len, (BYTE)*buffer, dma_addr + (i * FIELDSIZE));
					for (j = 0; j < FIELDSIZE; j++) {
						dma_write(dma_addr + (i * FIELDSIZE) + j, buffer[j]);
					}
					// memcpy(mem_base() + dma_addr + (i * FIELDSIZE), buffer, len);
				}
			}
		} else {
			// No 88ACC camera attached
			LOGW(TAG, "No Cromemeco Cyclops 88ACC attached.");
		}

		frame++;
		/* frame done, calculate total frame time */
		j = msg.fields * (msg.interval +1) * 2;

		// SLEEP_MS(j);

		/* sleep rest of total frame time */
#ifndef ESP_PLATFORM
		gettimeofday(&t2, NULL);
		tdiff = time_diff(&t1, &t2);
		if (tdiff < (j*1000)) 
			SLEEP_MS(j - tdiff/1000);

		LOGD(TAG, "Time: %d", tdiff);
#else
		// t2 = esp_timer_get_time();
		// tdiff = t2 - t1;
		// if(tdiff < j) {
		// 	usleep(j - tdiff);
		// }
#endif

		bus_request = 0;
		state = 0;
	}

	/* DMA complete, end the thread */
	state = 0;

#ifndef ESP_PLATFORM	
	thread = 0;
	pthread_exit(NULL);
#else
	if (thread != NULL) { 
		// thread = NULL; 
		// vTaskDelete(NULL); 
		vTaskSuspend(NULL);
	};
#endif
}
}

void cromemco_88ccc_ctrl_a_out(BYTE data)
{

	flags = data & 0x7f;

	if (data & 0x80) {
		if (net_device_alive(DEV_88ACC)) {
			state = 1;

#ifndef ESP_PLATFORM
			if (thread == 0) {
				if (pthread_create(&thread, NULL, store_image, (void *) NULL)) {
					LOGE(TAG, "can't create thread");
					exit(1);
				}
			} else {
				LOGW(TAG, "Transfer with 88CCC already in progess.");
			}
#else 
		if(thread == NULL) {
			xTaskCreatePinnedToCore(store_image, "cccCyclopsTask", 2500, NULL, ESP_TASK_MAIN_PRIO + 14, &thread, 0);
			if (thread == NULL) 
				ESP_LOGE(TAG, "WS CCC CYCLOPS TASK CREATE FAILED");
		} else {
			// LOGW(TAG, "Transfer with 88CCC already in progess.");
			vTaskResume(thread);
		}
#endif
		} else {
			/* No 88ACC camera attached */
			LOGW(TAG, "No Cromemeco Cyclops 88ACC attached.");
			state = 0;
		}
	} else {
		if (state == 1) {
			state = 0;
			SLEEP_MS(50); /* Arbitraray 50ms timeout to let thread exit after state change, TODO: maybe should end thread? */
			if(thread != NULL) {
				vTaskDelete(thread);
				thread = NULL;
			}
			bus_request = 0;
		}
	}
}

void cromemco_88ccc_ctrl_b_out(BYTE data)
{
	format = data;
}

void cromemco_88ccc_ctrl_c_out(BYTE data)
{
	/* get DMA address for storage memory */
	dma_addr = (WORD)data << 7;
}

BYTE cromemco_88ccc_ctrl_a_in(void)
{
	/* return flags along with state in the msb */
	return(flags | (state << 7));
}

