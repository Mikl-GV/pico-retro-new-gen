#include "videoint.h"


int video_set_pal(struct PAL_INFO*pi, int mode)
{
	if (mode == pi->prev_pal) return 0;
	switch (mode) {
	case 0: // 8/a
		pi->c2_palette[0]=0; pi->c2_palette[1]=15;
		pi->c4_palette[0]=0; pi->c4_palette[1]=1;
		pi->c4_palette[2]=2; pi->c4_palette[3]=4;
		pi->c1_palette[0]=0;
		break;
	case 1: // 9/a
		pi->c2_palette[0]=15; pi->c2_palette[1]=0;
		pi->c4_palette[0]=15; pi->c4_palette[1]=1;
		pi->c4_palette[2]=2; pi->c4_palette[3]=4;
		pi->c1_palette[0]=4;
		break;
	case 2: // 8/b
		pi->c2_palette[0]=0; pi->c2_palette[1]=2;
		pi->c4_palette[0]=0; pi->c4_palette[1]=0;
		pi->c4_palette[2]=2; pi->c4_palette[3]=4;
		pi->c1_palette[0]=0;
		break;
	case 3: // 9/b
		pi->c2_palette[0]=2; pi->c2_palette[1]=0;
		pi->c4_palette[0]=0; pi->c4_palette[1]=1;
		pi->c4_palette[2]=0; pi->c4_palette[3]=4;
		pi->c1_palette[0]=5;
		break;
	default:
		return 0;
	}
	pi->prev_pal = mode;
	return 1;
}



void video_set_palette(struct VIDEO_STATE*vs, int mode)
{
	struct PAL_INFO *pi = &vs->pal;
	switch (mode) {
	case 0:
		pi->pal_regs[0] = 0;
		break;
	case 1:
		pi->pal_regs[0] = 1;
		break;
	case 2:
		pi->pal_regs[1] = 0;
		break;
	case 3:
		pi->pal_regs[1] = 1;
		break;
	default:
		return;
	}
	if (video_set_pal(pi, pi->pal_regs[0] | (pi->pal_regs[1]<<1))) video_repaint_screen(vs);
}
