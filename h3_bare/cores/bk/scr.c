#include "defines.h"

unsigned char req_page[512], req_palette[512];
unsigned char param_change_line;
unsigned char change_req;

int upper_porch = 0;	/* Default */
int lower_porch = 3;	/* Default */

static unsigned char active_palette, active_page;
static unsigned char half_frame = 0;

#define LINES_TOTAL     (256+upper_porch+lower_porch)

/* Returns the scan line number that is supposedly being displayed "now".
 * Each half frame is TICK_RATE/50 long and consists of 128 lines.
 */
static unsigned current_scan_line() {
	unsigned nframes = ticks/half_frame_delay;
	unsigned frame_ticks = ticks - half_frame_delay * nframes;
	unsigned line = frame_ticks / scan_line_duration;
	if (line < upper_porch) return 0;
	line -= upper_porch;
	if (line < 256) return line; else return 256;
}

void scr_param_change(int pal, int buf) {
	int cur = current_scan_line();
	unsigned int i;
	for (i = param_change_line; i < cur; i++) {
		req_palette[2 * i + half_frame] = active_palette;
		req_page[2 * i + half_frame] = active_page;
	}
	active_palette = pal;
	active_page = buf;
	param_change_line = cur;
	change_req = 3;	/* For 2 half-frames */
	fprintf(stderr, "l=%d\n", cur); 
}

void scr_common_init() {
	active_palette = bkmodel ? 15 : 0;
	scan_line_duration = TICK_RATE/(LINES_TOTAL*50);
}

/*
 * Just before a half frame ends, fill up the buffer and palette
 * requests to the end with the current values.
 */
void scr_sync() {
	unsigned int i;
	for (i = param_change_line; i < 256; i++) {
		req_palette[2 * i + half_frame] = active_palette;
		req_page[2 * i + half_frame] = active_page;
	}
	half_frame ^= 1;
	param_change_line = 0;
}
