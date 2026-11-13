#include "videoint.h"
#include <stdio.h>

int video_set_mono(struct VIDEO_STATE*vs, int a, int x)
{
	puts("toggle_mono");
	if (vs->video_mode!=VIDEO_MODE_APPLE) return 1;
	vs->pal.cur_mono = (vs->pal.cur_mono & a) ^ x;
	video_repaint_screen(vs);
	return 0;
}               
