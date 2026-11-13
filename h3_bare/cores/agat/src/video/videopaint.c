#include "videoint.h"

static void video_repaint_screen_comb(struct VIDEO_STATE*vs)
{
	dword addr, n;
	dword badr = (vs->ainf.page+1)*0x400;
	if (!vs->ainf.text_mode&&vs->ainf.combined) {
		for (addr=badr,n=0x400;n;n--,addr++) {
			RECT r;
			apaint_t40_addr_mix(vs, addr,&r);
		}
		invalidate_video_window(vs->sr, NULL);
	}
}

void video_repaint_block(struct VIDEO_STATE*vs, int rbi)
{
	dword addr, n;
	int j;
	if (!vs->sr->bmp_bits) return;
//	printf("video_repaint_screen: type = %i, base = %x, size = %x\n", vs->vid_type, vs->video_base_addr, vs->video_mem_size);
	for (j = vs->rb[rbi].n_ranges-1; j>=0; --j) {
		for (addr=vs->rb[rbi].base_addr[j],n=vs->rb[rbi].mem_size[j];n;
			n-=vs->rb[rbi].el_size,addr+=vs->rb[rbi].el_size) {
			RECT r;
			paint_addr[vs->rb[rbi].vtype](vs, addr, &r);
		}
	}
	if (vs->video_mode==VIDEO_MODE_APPLE) video_repaint_screen_comb(vs);
	else invalidate_video_window(vs->sr, &vs->rb[rbi].r);
}

void video_repaint_screen(struct VIDEO_STATE*vs)
{
	struct RASTER_BLOCK*rb;
	int i;
	for (i = vs->n_rb - 1, rb = vs->rb; i >= 0; --i, ++rb) {
		rb->dirty = 1;
		video_update_rb(vs, i);
	}
	invalidate_video_window(vs->sr, NULL);
}
