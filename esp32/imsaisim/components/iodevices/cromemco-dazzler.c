/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2015-2018 by Udo Munk
 *
 * Emulation of a Cromemco DAZZLER S100 board
 *
 * History:
 * 24-APR-15 first version
 * 25-APR-15 fixed a few things, good enough for a BETA release now
 * 27-APR-15 fixed logic bugs with on/off state and thread handling
 * 08-MAY-15 fixed Xlib multithreading problems
 * 26-AUG-15 implemented double buffering to prevent flicker
 * 27-AUG-15 more bug fixes
 * 15-NOV-16 fixed logic bug, display wasn't always clear after
 *           the device is switched off
 * 06-DEC-16 added bus request for the DMA
 * 16-DEC-16 use DMA function for memory access
 * 26-JAN-17 optimization
 * 15-JUL-18 use logging
 */
#ifndef ESP_PLATFORM
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <pthread.h>
#endif
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

static const char *TAG = "DAZZLER";

#ifndef ESP_PLATFORM
/* X11 stuff */
#define WSIZE 512
static int size = WSIZE;
static Display *display;
static Window window;
static int screen;
static GC gc;
static XWindowAttributes wa;
static Pixmap pixmap;
static Colormap colormap;
static XColor colors[16];
static XColor grays[16];
static char color0[] =  "#000000";
static char color1[] =  "#800000";
static char color2[] =  "#008000";
static char color3[] =  "#808000";
static char color4[] =  "#000080";
static char color5[] =  "#800080";
static char color6[] =  "#008080";
static char color7[] =  "#808080";
static char color8[] =  "#000000";
static char color9[] =  "#FF0000";
static char color10[] = "#00FF00";
static char color11[] = "#FFFF00";
static char color12[] = "#0000FF";
static char color13[] = "#FF00FF";
static char color14[] = "#00FFFF";
static char color15[] = "#FFFFFF";
static char gray0[] =   "#000000";
static char gray1[] =   "#101010";
static char gray2[] =   "#202020";
static char gray3[] =   "#303030";
static char gray4[] =   "#404040";
static char gray5[] =   "#505050";
static char gray6[] =   "#606060";
static char gray7[] =   "#707070";
static char gray8[] =   "#808080";
static char gray9[] =   "#909090";
static char gray10[] =  "#A0A0A0";
static char gray11[] =  "#B0B0B0";
static char gray12[] =  "#C0C0C0";
static char gray13[] =  "#D0D0D0";
static char gray14[] =  "#E0E0E0";
static char gray15[] =  "#FFFFFF";
#endif
/* DAZZLER stuff */
static int state;
static WORD dma_addr;
static BYTE flags = 64;
static BYTE format;

#ifdef HAS_NETSERVER
static void ws_clear(void);
static BYTE formatBuf = 0;
#endif

#ifdef ESP_PLATFORM
static TaskHandle_t dazRefresh = NULL;
#else 
/* UNIX stuff */
static pthread_t thread;

/* create the X11 window for DAZZLER display */
static void open_display(void)
{
	Window rootwindow;
	XSizeHints *size_hints = XAllocSizeHints();
	Atom wm_delete_window;

	display = XOpenDisplay(NULL);
	XLockDisplay(display);
	screen = DefaultScreen(display);
	rootwindow = RootWindow(display, screen);
	XGetWindowAttributes(display, rootwindow, &wa);
	window = XCreateSimpleWindow(display, rootwindow, 0, 0,
					size, size, 1, 0, 0);
	XStoreName(display, window, "Cromemco DAzzLER");
	size_hints->flags = PSize | PMinSize | PMaxSize;
	size_hints->min_width = size;
	size_hints->min_height = size;
	size_hints->base_width = size;
	size_hints->base_height = size;
	size_hints->max_width = size;
	size_hints->max_height = size;
	XSetWMNormalHints(display, window, size_hints);
	XFree(size_hints);
	wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &wm_delete_window, 1);
	colormap = DefaultColormap(display, 0);
	gc = XCreateGC(display, window, 0, NULL);
	XSetFillStyle(display, gc, FillSolid);
	pixmap = XCreatePixmap(display, rootwindow, size, size,
		 wa.depth);

	XParseColor(display, colormap, color0, &colors[0]);
	XAllocColor(display, colormap, &colors[0]);
	XParseColor(display, colormap, color1, &colors[1]);
	XAllocColor(display, colormap, &colors[1]);
	XParseColor(display, colormap, color2, &colors[2]);
	XAllocColor(display, colormap, &colors[2]);
	XParseColor(display, colormap, color3, &colors[3]);
	XAllocColor(display, colormap, &colors[3]);
	XParseColor(display, colormap, color4, &colors[4]);
	XAllocColor(display, colormap, &colors[4]);
	XParseColor(display, colormap, color5, &colors[5]);
	XAllocColor(display, colormap, &colors[5]);
	XParseColor(display, colormap, color6, &colors[6]);
	XAllocColor(display, colormap, &colors[6]);
	XParseColor(display, colormap, color7, &colors[7]);
	XAllocColor(display, colormap, &colors[7]);
	XParseColor(display, colormap, color8, &colors[8]);
	XAllocColor(display, colormap, &colors[8]);
	XParseColor(display, colormap, color9, &colors[9]);
	XAllocColor(display, colormap, &colors[9]);
	XParseColor(display, colormap, color10, &colors[10]);
	XAllocColor(display, colormap, &colors[10]);
	XParseColor(display, colormap, color11, &colors[11]);
	XAllocColor(display, colormap, &colors[11]);
	XParseColor(display, colormap, color12, &colors[12]);
	XAllocColor(display, colormap, &colors[12]);
	XParseColor(display, colormap, color13, &colors[13]);
	XAllocColor(display, colormap, &colors[13]);
	XParseColor(display, colormap, color14, &colors[14]);
	XAllocColor(display, colormap, &colors[14]);
	XParseColor(display, colormap, color15, &colors[15]);
	XAllocColor(display, colormap, &colors[15]);

	XParseColor(display, colormap, gray0, &grays[0]);
	XAllocColor(display, colormap, &grays[0]);
	XParseColor(display, colormap, gray1, &grays[1]);
	XAllocColor(display, colormap, &grays[1]);
	XParseColor(display, colormap, gray2, &grays[2]);
	XAllocColor(display, colormap, &grays[2]);
	XParseColor(display, colormap, gray3, &grays[3]);
	XAllocColor(display, colormap, &grays[3]);
	XParseColor(display, colormap, gray4, &grays[4]);
	XAllocColor(display, colormap, &grays[4]);
	XParseColor(display, colormap, gray5, &grays[5]);
	XAllocColor(display, colormap, &grays[5]);
	XParseColor(display, colormap, gray6, &grays[6]);
	XAllocColor(display, colormap, &grays[6]);
	XParseColor(display, colormap, gray7, &grays[7]);
	XAllocColor(display, colormap, &grays[7]);
	XParseColor(display, colormap, gray8, &grays[8]);
	XAllocColor(display, colormap, &grays[8]);
	XParseColor(display, colormap, gray9, &grays[9]);
	XAllocColor(display, colormap, &grays[9]);
	XParseColor(display, colormap, gray10, &grays[10]);
	XAllocColor(display, colormap, &grays[10]);
	XParseColor(display, colormap, gray11, &grays[11]);
	XAllocColor(display, colormap, &grays[11]);
	XParseColor(display, colormap, gray12, &grays[12]);
	XAllocColor(display, colormap, &grays[12]);
	XParseColor(display, colormap, gray13, &grays[13]);
	XAllocColor(display, colormap, &grays[13]);
	XParseColor(display, colormap, gray14, &grays[14]);
	XAllocColor(display, colormap, &grays[14]);
	XParseColor(display, colormap, gray15, &grays[15]);
	XAllocColor(display, colormap, &grays[15]);

	XMapWindow(display, window);
	XUnlockDisplay(display);
}
#endif

/* switch DAZZLER off from front panel */
void cromemco_dazzler_off(void)
{
	state = 0;
	SLEEP_MS(50);

#ifndef ESP_PLATFORM
	if (thread != 0) {
		pthread_cancel(thread);
		pthread_join(thread, NULL);
		thread = 0;
	}

	if (display != NULL) {
		XLockDisplay(display);
		XFreePixmap(display, pixmap);
		XFreeGC(display, gc);
		XUnlockDisplay(display);
		XCloseDisplay(display);
		display = NULL;
	}
#else
	if (dazRefresh != NULL) { 
		vTaskDelete(dazRefresh); 
		dazRefresh = NULL; 
	};

#endif

#ifdef HAS_NETSERVER
	ws_clear();
#endif

	bus_request = 0;
}
#ifndef ESP_PLATFORM
/* draw pixels for one frame in hires */
static void draw_hires(void)
{
	int psize, x, y, i;
	WORD addr = dma_addr;

	/* set color or grayscale from lower nibble in graphics format */
	i = format & 0x0f;
	if (format & 16) 
		XSetForeground(display, gc, colors[i].pixel);
	else
		XSetForeground(display, gc, grays[i].pixel);

	if (format & 32) {	/* 2048 bytes memory */
		psize = size / 128;	/* size of one pixel for 128x128 */
		for (y = 0; y < 64; y += 2) {
			for (x = 0; x < 64;) {
				i = dma_read(addr);
				if (i & 1)
					XFillRectangle(display, pixmap, gc,
						x * psize, y * psize,
						psize, psize);
				if (i & 2)
					XFillRectangle(display, pixmap, gc,
						(x+1) * psize, y * psize,
						psize, psize);
				if (i & 4)
					XFillRectangle(display, pixmap, gc,
						x * psize, (y+1) * psize,
						psize, psize);
				if (i & 8)
					XFillRectangle(display, pixmap, gc,
						(x+1) * psize, (y+1) * psize,
						psize, psize);
				if (i & 16)
					XFillRectangle(display, pixmap, gc,
						(x+2) * psize, y * psize,
						psize, psize);
				if (i & 32)
					XFillRectangle(display, pixmap, gc,
						(x+3) * psize, y * psize,
						psize, psize);
				if (i & 64)
					XFillRectangle(display, pixmap, gc,
						(x+2) * psize, (y+1) * psize,
						psize, psize);
				if (i & 128)
					XFillRectangle(display, pixmap, gc,
						(x+3) * psize, (y+1) * psize,
						psize, psize);
				x += 4;
				addr++;
			}
		}
		for (y = 0; y < 64; y += 2) {
			for (x = 64; x < 128;) {
				i = dma_read(addr);
				if (i & 1)
					XFillRectangle(display, pixmap, gc,
						x * psize, y * psize,
						psize, psize);
				if (i & 2)
					XFillRectangle(display, pixmap, gc,
						(x+1) * psize, y * psize,
						psize, psize);
				if (i & 4)
					XFillRectangle(display, pixmap, gc,
						x * psize, (y+1) * psize,
						psize, psize);
				if (i & 8)
					XFillRectangle(display, pixmap, gc,
						(x+1) * psize, (y+1) * psize,
						psize, psize);
				if (i & 16)
					XFillRectangle(display, pixmap, gc,
						(x+2) * psize, y * psize,
						psize, psize);
				if (i & 32)
					XFillRectangle(display, pixmap, gc,
						(x+3) * psize, y * psize,
						psize, psize);
				if (i & 64)
					XFillRectangle(display, pixmap, gc,
						(x+2) * psize, (y+1) * psize,
						psize, psize);
				if (i & 128)
					XFillRectangle(display, pixmap, gc,
						(x+3) * psize, (y+1) * psize,
						psize, psize);
				x += 4;
				addr++;
			}
		}
		for (y = 64; y < 128; y += 2) {
			for (x = 0; x < 64;) {
				i = dma_read(addr);
				if (i & 1)
					XFillRectangle(display, pixmap, gc,
						x * psize, y * psize,
						psize, psize);
				if (i & 2)
					XFillRectangle(display, pixmap, gc,
						(x+1) * psize, y * psize,
						psize, psize);
				if (i & 4)
					XFillRectangle(display, pixmap, gc,
						x * psize, (y+1) * psize,
						psize, psize);
				if (i & 8)
					XFillRectangle(display, pixmap, gc,
						(x+1) * psize, (y+1) * psize,
						psize, psize);
				if (i & 16)
					XFillRectangle(display, pixmap, gc,
						(x+2) * psize, y * psize,
						psize, psize);
				if (i & 32)
					XFillRectangle(display, pixmap, gc,
						(x+3) * psize, y * psize,
						psize, psize);
				if (i & 64)
					XFillRectangle(display, pixmap, gc,
						(x+2) * psize, (y+1) * psize,
						psize, psize);
				if (i & 128)
					XFillRectangle(display, pixmap, gc,
						(x+3) * psize, (y+1) * psize,
						psize, psize);
				x += 4;
				addr++;
			}
		}
		for (y = 64; y < 128; y += 2) {
			for (x = 64; x < 128;) {
				i = dma_read(addr);
				if (i & 1)
					XFillRectangle(display, pixmap, gc,
						x * psize, y * psize,
						psize, psize);
				if (i & 2)
					XFillRectangle(display, pixmap, gc,
						(x+1) * psize, y * psize,
						psize, psize);
				if (i & 4)
					XFillRectangle(display, pixmap, gc,
						x * psize, (y+1) * psize,
						psize, psize);
				if (i & 8)
					XFillRectangle(display, pixmap, gc,
						(x+1) * psize, (y+1) * psize,
						psize, psize);
				if (i & 16)
					XFillRectangle(display, pixmap, gc,
						(x+2) * psize, y * psize,
						psize, psize);
				if (i & 32)
					XFillRectangle(display, pixmap, gc,
						(x+3) * psize, y * psize,
						psize, psize);
				if (i & 64)
					XFillRectangle(display, pixmap, gc,
						(x+2) * psize, (y+1) * psize,
						psize, psize);
				if (i & 128)
					XFillRectangle(display, pixmap, gc,
						(x+3) * psize, (y+1) * psize,
						psize, psize);
				x += 4;
				addr++;
			}
		}
	} else {		/* 512 bytes memory */
		psize = size / 64;	/* size of one pixel for 64x64 */
		for (y = 0; y < 64; y += 2) {
			for (x = 0; x < 64;) {
				i = dma_read(addr);
				if (i & 1)
					XFillRectangle(display, pixmap, gc,
						x * psize, y * psize,
						psize, psize);
				if (i & 2)
					XFillRectangle(display, pixmap, gc,
						(x+1) * psize, y * psize,
						psize, psize);
				if (i & 4)
					XFillRectangle(display, pixmap, gc,
						x * psize, (y+1) * psize,
						psize, psize);
				if (i & 8)
					XFillRectangle(display, pixmap, gc,
						(x+1) * psize, (y+1) * psize,
						psize, psize);
				if (i & 16)
					XFillRectangle(display, pixmap, gc,
						(x+2) * psize, y * psize,
						psize, psize);
				if (i & 32)
					XFillRectangle(display, pixmap, gc,
						(x+3) * psize, y * psize,
						psize, psize);
				if (i & 64)
					XFillRectangle(display, pixmap, gc,
						(x+2) * psize, (y+1) * psize,
						psize, psize);
				if (i & 128)
					XFillRectangle(display, pixmap, gc,
						(x+3) * psize, (y+1) * psize,
						psize, psize);
				x += 4;
				addr++;
			}
		}
	}
}

/* draw pixels for one frame in lowres */
static void draw_lowres(void)
{
	int psize, x, y, i;
	WORD addr = dma_addr;

	/* get size of DMA memory and draw the pixels */
	if (format & 32) {	/* 2048 bytes memory */
		psize = size / 64;	/* size of one pixel for 64x64 */
		for (y = 0; y < 32; y++) {
			for (x = 0; x < 32;) {
				i = dma_read(addr) & 0x0f;
				if (format & 16)
					XSetForeground(display, gc,
							colors[i].pixel);
				else
					XSetForeground(display, gc,
							grays[i].pixel);
				XFillRectangle(display, pixmap, gc,
					x * psize, y * psize,
					psize, psize);
				x++;
				i = (dma_read(addr) & 0xf0) >> 4;
				if (format & 16)
					XSetForeground(display, gc,
							colors[i].pixel);
				else
					XSetForeground(display, gc,
							grays[i].pixel);
				XFillRectangle(display, pixmap, gc,
					x * psize, y * psize,
					psize, psize);
				x++;
				addr++;
			}
		}
		for (y = 0; y < 32; y++) {
			for (x = 32; x < 64;) {
				i = dma_read(addr) & 0x0f;
				if (format & 16)
					XSetForeground(display, gc,
							colors[i].pixel);
				else
					XSetForeground(display, gc,
							grays[i].pixel);
				XFillRectangle(display, pixmap, gc,
					x * psize, y * psize,
					psize, psize);
				x++;
				i = (dma_read(addr) & 0xf0) >> 4;
				if (format & 16)
					XSetForeground(display, gc,
							colors[i].pixel);
				else
					XSetForeground(display, gc,
							grays[i].pixel);
				XFillRectangle(display, pixmap, gc,
					x * psize, y * psize,
					psize, psize);
				x++;
				addr++;
			}
		}
		for (y = 32; y < 64; y++) {
			for (x = 0; x < 32;) {
				i = dma_read(addr) & 0x0f;
				if (format & 16)
					XSetForeground(display, gc,
							colors[i].pixel);
				else
					XSetForeground(display, gc,
							grays[i].pixel);
				XFillRectangle(display, pixmap, gc,
					x * psize, y * psize,
					psize, psize);
				x++;
				i = (dma_read(addr) & 0xf0) >> 4;
				if (format & 16)
					XSetForeground(display, gc,
							colors[i].pixel);
				else
					XSetForeground(display, gc,
							grays[i].pixel);
				XFillRectangle(display, pixmap, gc,
					x * psize, y * psize,
					psize, psize);
				x++;
				addr++;
			}
		}
		for (y = 32; y < 64; y++) {
			for (x = 32; x < 64;) {
				i = dma_read(addr) & 0x0f;
				if (format & 16)
					XSetForeground(display, gc,
							colors[i].pixel);
				else
					XSetForeground(display, gc,
							grays[i].pixel);
				XFillRectangle(display, pixmap, gc,
					x * psize, y * psize,
					psize, psize);
				x++;
				i = (dma_read(addr) & 0xf0) >> 4;
				if (format & 16)
					XSetForeground(display, gc,
							colors[i].pixel);
				else
					XSetForeground(display, gc,
							grays[i].pixel);
				XFillRectangle(display, pixmap, gc,
					x * psize, y * psize,
					psize, psize);
				x++;
				addr++;
			}
		}
	} else {		/* 512 bytes memory */
		psize = size / 32;	/* size of one pixel for 32x32 */
		for (y = 0; y < 32; y++) {
			for (x = 0; x < 32;) {
				i = dma_read(addr) & 0x0f;
				if (format & 16)
					XSetForeground(display, gc,
							colors[i].pixel);
				else
					XSetForeground(display, gc,
							grays[i].pixel);
				XFillRectangle(display, pixmap, gc,
					x * psize, y * psize,
					psize, psize);
				x++;
				i = (dma_read(addr) & 0xf0) >> 4;
				if (format & 16)
					XSetForeground(display, gc,
							colors[i].pixel);
				else
					XSetForeground(display, gc,
							grays[i].pixel);
				XFillRectangle(display, pixmap, gc,
					x * psize, y * psize,
					psize, psize);
				x++;
				addr++;
			}
		}
	}
}
#endif

#ifdef HAS_NETSERVER
static uint8_t dblbuf[2048];

static struct {
	uint16_t format;
	uint16_t addr;
	uint16_t len;
	uint8_t buf[2048];
} msg;

void ws_clear(void) {

	memset(dblbuf, 0, 2048);

	msg.format = 0;
	msg.addr = 0xFFFF;
	msg.len = 0;
	net_device_send(DEV_DZLR, (char *) &msg, msg.len + 6);
	LOGD(TAG, "Clear the screen.");
}

static void ws_refresh(void) {

	int len = (format & 32)?2048:512;
	int addr;
	int i, n, x, la_count;
	bool cont;
	uint8_t val;

	for (i=0; i<len; i++) {
		addr = i;
		n=0;
		la_count=0;
		cont = true;
		while (cont && (i<len)) {
			val=peek(dma_addr + i);
			while ((val != dblbuf[i]) && (i<len)) {
				dblbuf[i++] = val;
				msg.buf[n++] = val;
				cont = false;
				val=peek(dma_addr + i);
			}
			if (cont) break;
			x=0;
#define LOOKAHEAD 6
			/* look-ahead up to n bytes for next change */
			while ((x<LOOKAHEAD) && !cont && (i<len)) {
				val=peek(dma_addr + i++);
				msg.buf[n++] = val;
				la_count++;
				val=peek(dma_addr + i);
				if ((i<len) && (val != dblbuf[i])) {
					cont = true;
				}
				x++;
			}
			if (!cont) {
				n -= x;
				la_count -= x;
			}
		}
		if (n || (format != formatBuf)) {
			formatBuf = format;
			msg.format = format;
			msg.addr = addr;
			msg.len = n;
			net_device_send(DEV_DZLR, (char *) &msg, msg.len + 6);
			LOGD(TAG, "BUF update 0x%04X-0x%04X len: %d format: 0x%02X l/a: %d", 
				msg.addr, msg.addr + msg.len, msg.len, msg.format, la_count);
		}
	}
}
#endif

#ifdef ESP_PLATFORM
void dazRefreshTask() {
	int64_t t1, t2, tdiff;
	int64_t f1, f2, tt;
	int i;
	int f = 0;

	// memset(dblbuf, 0, 2048);
	for(i=0; i<2048; i++) dblbuf[i] = 0x00;

	f2 = f1 = t1 = esp_timer_get_time();
	tt = f2 - f1;
	tt = tt;
	f = f;

	while (1) {	/* do forever or until canceled */

		/* update display window */
		ws_refresh();

		/* frame done, set frame flag for 4ms */
		flags = 0;
		SLEEP_MS(4);
		flags = 64;

		/* compute time used for processing */
		t2 = esp_timer_get_time();
		tdiff = t2 - t1;
		/* sleep rest to 33ms so that we get 30 fps */
		if(tdiff < 33000) {
			usleep(33000 - tdiff);
		}

		t1 = esp_timer_get_time();

#ifdef DEBUG
		if(++f >= 300) {
			f2 = esp_timer_get_time();
			tt = (f2 - f1) / 1000;

			ESP_LOGD(TAG, "Framerate = %d", f*1000/tt);

			f = 0;
			f1 = esp_timer_get_time();
		}
#endif //DEBUG
	}
}
#endif //ESP_PLATFORM

#ifndef ESP_PLATFORM
/* thread for updating the display */
static void *update_display(void *arg)
{
	extern int time_diff(struct timeval *, struct timeval *);

	struct timeval t1, t2;
	int tdiff;

	arg = arg;	/* to avoid compiler warning */
	gettimeofday(&t1, NULL);

	while (1) {	/* do forever or until canceled */

		/* draw one frame dependend on graphics format */
		if (state == 1) {	/* draw frame if on */
			if (cpu_state == CONTIN_RUN)
				bus_request = 1;
#ifndef HAS_NETSERVER
			XLockDisplay(display);
			XSetForeground(display, gc, colors[0].pixel);
			XFillRectangle(display, pixmap, gc, 0, 0, size, size);
			if (format & 64)
				draw_hires();
			else
				draw_lowres();
			XCopyArea(display, pixmap, window, gc, 0, 0,
				  size, size, 0, 0);
			XSync(display, True);
			XUnlockDisplay(display);
#else 
			// UNUSED(draw_hires);
			// UNUSED(draw_lowres);
			if (net_device_alive(DEV_DZLR)) {
				ws_refresh();
			} else {
				if (msg.format) { 
					memset(dblbuf, 0, 2048);
					msg.format = 0;
				}
			}

#endif
			bus_request = 0;
		}

		/* frame done, set frame flag for 4ms */
		flags = 0;
		SLEEP_MS(4);
		flags = 64;

		/* sleep rest to 33ms so that we get 30 fps */
		gettimeofday(&t2, NULL);
		tdiff = time_diff(&t1, &t2);
		if ((tdiff > 0) && (tdiff < 33000))
			SLEEP_MS(33 - (tdiff / 1000));

		gettimeofday(&t1, NULL);
	}

	/* just in case it ever gets here */
	pthread_exit(NULL);
	return;
}
#endif

void cromemco_dazzler_ctl_out(BYTE data)
{
	/* get DMA address for display memory */
	dma_addr = (data & 0x7f) << 9;

	/* switch DAZZLER on/off */
	if (data & 128) {
#ifndef HAS_NETSERVER
		state = 1;
		if (display == NULL) {
			open_display();
		}
#else
		// UNUSED(open_display);
		if (state == 0) 
			ws_clear();
		state = 1;
#endif

#ifndef ESP_PLATFORM
		if (thread == 0) {
			if (pthread_create(&thread, NULL, update_display,
			    (void *) NULL)) {
				LOGE(TAG, "can't create thread");
				exit(1);
			}
		}
#else 
		if(dazRefresh == NULL) {
			xTaskCreatePinnedToCore(dazRefreshTask, "dazRefreshTask", 2000, NULL, ESP_TASK_MAIN_PRIO + 5, &dazRefresh, 0);
			if (dazRefresh == NULL) 
				ESP_LOGE(TAG, "WS DZLR REFREASH TASK CREATE FAILED");
		}
#endif
	} else {
		if (state == 1) {
			state = 0;
			SLEEP_MS(50);
#ifndef HAS_NETSERVER
			XLockDisplay(display);
			XClearWindow(display, window);
			XSync(display, True);
			XUnlockDisplay(display);
#else
			ws_clear();
#endif
			bus_request = 0;
		}
	}
}

BYTE cromemco_dazzler_flags_in(void)
{
#ifndef ESP_PLATFORM
	if (thread != 0)
#else
	if (dazRefresh != NULL)
#endif
		return(flags);
	else
		return((BYTE) 0xff);
}

void cromemco_dazzler_format_out(BYTE data)
{
	format = data;
}
