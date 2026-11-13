#include "videoint.h"

extern void flash_system_1(struct SYS_RUN_STATE*sr);

void video_flash_text(struct VIDEO_STATE*vs)
{
	int i, d = 0;
	struct RASTER_BLOCK*rb;
	vs->pal.flash_mode=!vs->pal.flash_mode;
	for (i = vs->n_rb, rb = vs->rb; i; --i, ++rb) {
		if (rb->vtype!=2&&vs->video_mode==VIDEO_MODE_AGAT) continue;
		rb->dirty = 1;
		d = 1;
	}
	if (vs->video_mode==VIDEO_MODE_APPLE_1) flash_system_1(vs->sr);
	if (d && (!vs->rb_enabled || vs->video_mode==VIDEO_MODE_APPLE)) 
		video_repaint_screen(vs);
}



