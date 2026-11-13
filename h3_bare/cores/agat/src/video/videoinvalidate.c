#include "videoint.h"

static void vid_invalidate_addr_comb(struct VIDEO_STATE*vs, dword adr)
{
	dword badr = (vs->ainf.page+1)*0x400;
	if (vs->ainf.text_mode||!vs->ainf.combined) return;
	if (!vs->sr->bmp_bits) return;
	if (adr>=badr&&adr<badr+0x400) 
	{
		RECT r;
		apaint_t40_addr_mix(vs, adr, &r);
		if (!vs->sr->sync_update) {
			invalidate_video_window(vs->sr, &r);
		} else {
			UnionRect(&vs->inv_area,&vs->inv_area,&r);
			vs->mem_access = 0;
		}
	}
}


void vid_invalidate_addr(struct SYS_RUN_STATE*sr, dword adr)
{
	struct VIDEO_STATE*vs = get_video_state(sr);
	struct RASTER_BLOCK*rb;
	dword sadr = adr;
	int i, j;
	if (sr->cursystype == SYSTEM_E) sadr &= 0xFFFF;
	if (!sr->bmp_bits) return;
//	printf("video_invalidate: adr = %x; n_rb = %i\n", adr, vs->n_rb);
	for (i = vs->n_rb, rb = vs->rb; i; --i, ++rb) {
//		printf("i = %i; n_ranges = %i\n", i, rb->n_ranges);
		for (j = rb->n_ranges - 1; j >= 0; --j) {
//			printf("j = %i; base_addr = %x; mem_size = %x\n", j, rb->base_addr[j], rb->mem_size[j]);
			if (sadr>=(dword)rb->base_addr[j]&&sadr<(dword)(rb->base_addr[j]+rb->mem_size[j])) {
				RECT r;
				if (vs->video_mode == VIDEO_MODE_AGAT && rb->vmode != vs->rb_cur.vmode) {
					rb->dirty = 1;
				} else {
//					printf("paint_addr[%i]: %x\n", rb->vtype, adr);
					paint_addr[rb->vtype](vs, adr, &r);
					if (sr->sync_update) {
						invalidate_video_window(sr, &r);
					} else {
						UnionRect(&vs->inv_area, &vs->inv_area, &r);
					}
				}
			}
		}
	}
	vs->mem_access = 0;
	if (vs->video_mode==VIDEO_MODE_APPLE)
		vid_invalidate_addr_comb(vs, sadr);
}
