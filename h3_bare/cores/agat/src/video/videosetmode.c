#include "videoint.h"

static void video_set_size(struct VIDEO_STATE*vs, int w, int h)
{
	struct RASTER_BLOCK*rb;
	int i;
	int bh = vs->n_rb ? h / vs->n_rb: h;
	int y = 0;

	for (i = vs->n_rb, rb = vs->rb; i; --i, ++rb) {
		rb->r.left = 0;
		rb->r.right = w;
		rb->r.top = y;
		y += bh;
		rb->r.bottom = y;
	}

	set_video_size(vs->sr, w, h);
}


void video_update_mode(struct VIDEO_STATE*vs)
{
	video_init_rb(vs);
	switch (vs->video_mode) {
	case VIDEO_MODE_AGAT:
		video_set_size(vs, PIX_W*256, PIX_H*256);
		break;
	case VIDEO_MODE_APPLE:
		if (vs->ainf.videoterm && vs->ainf.text_mode) {
			video_set_size(vs, 
				vs->vinf.scr_size[0]*vs->vinf.char_size[0]*vs->vinf.char_scl[0], 
				vs->vinf.scr_size[1]*vs->vinf.char_size[1]*vs->vinf.char_scl[1]);
		} else {
			video_set_size(vs, PIX_W*280, PIX_H*192);
		}
		break;
	case VIDEO_MODE_APPLE_1:
		video_set_size(vs, CHAR_W * 40, CHAR_H * 24);
		break;
	case VIDEO_MODE_ACORN:
		video_set_size(vs, CHAR_W * 32, CHAR_H * 24);
		break;
	}
}


void video_set_mode(struct VIDEO_STATE*vs, int md)
{
	if (vs->video_mode == md) return;
	vs->video_mode = md;
	video_update_mode(vs);
}

