#include "videoint.h"
#include <stdio.h>

static int get_apple_mode_id(struct APPLE_INFO*ai)
{
	int res = 0;
	if (ai->text_mode) res |= 1;
	if (ai->combined) res |= 2;
	if (ai->page) res |= 4;
	if (ai->hgr) res |= 8;
	if (ai->videoterm) res |= 16;
	if (ai->text80) res |= 32;
	if (ai->dhgr) res |= 64;
	return res;
}

void update_video_ap(struct VIDEO_STATE*vs)
{
	struct APPLE_INFO*ai = &vs->ainf;
	int page = ai->page;
	int hgr = ai->hgr;
	if (baseram_read_ext_state(0xC018, vs->sr)) {
		page = (vs->rb_cur.base_addr[0]==0x800) || (vs->rb_cur.base_addr[0]==0x4000);
//		hgr = (vs->rb_cur.base_addr[0] >= 0x2000);
	}
//	printf("hgr = %i, page = %i, dhgr = %i\n", hgr, page, ai->dhgr);
	if (ai->text_mode) {
		if (ai->videoterm) {
			set_video_active_range(vs, 0x10000, vs->vinf.ram_size, 1);
			set_video_type(vs, 11);
		} else {
			if (ai->text80) {
				set_video_active_range(vs, (page+1)*0x400, 0x400, 1);
				set_video_type(vs, 13);
			} else {
				set_video_active_range(vs, (page+1)*0x400, 0x400, 1);
				set_video_type(vs, 7);
			}	
		}
	} else if (ai->dhgr) {
		set_video_active_range(vs, (page+1)*0x2000, 0x2000, 1);
		set_video_type(vs, 14);
	} else if (hgr) {
		if (basemem_n_blocks(vs->sr) < (page+2) * 4) return;
			set_video_active_range(vs, (page+1)*0x2000, 0x2000, 1);
			set_video_type(vs, 9);
	} else {
		set_video_active_range(vs, (page+1)*0x400, 0x400, 1);
		set_video_type(vs, 8);
	}
	vs->rb_cur.vmode = (vs->rb_cur.vmode & 0xFF) | (get_apple_mode_id(ai) << 8);
	video_update_rb(vs, 0);
//	video_repaint_screen(vs);
}


void vsel_ap(struct VIDEO_STATE*vs, word adr)
{
	struct APPLE_INFO*ai = &vs->ainf;
	struct VTERM_INFO*vi = &vs->vinf;
//	printf("apple video select: %x\n", adr);
	video_set_mode(vs, VIDEO_MODE_APPLE);
	adr&=15;
//	printf("vs->page = %i\n", vs->page);
	switch(adr) {
	case 0: ai->text_mode = 0; 
		break;
	case 1: ai->text_mode = 1; 
		break;
	case 2: ai->combined = 0; 
		break;
	case 3: ai->combined  = 1; 
		break;
	case 4: ai->page = 0; 
		break;
	case 5: ai->page = 1; 
		break;
	case 6: 
		ai->videoterm = 0;
		if (vs->sr->cursystype != SYSTEM_9) {
			ai->hgr = 0;
		} else {
			ai->hgr = 1; // low-res emulation was not implemented in Agat
		}
		break;
	case 7: ai->hgr = 1; 
		ai->videoterm = 0;
		break;
	case 8:
		if (ai->videoterm) puts("videoterm disabled");
		ai->videoterm = 0;
		break;
	case 9:
		if (!ai->videoterm) puts("videoterm enabled");
		if (vi->ram) ai->videoterm = 1;
		break;
	case 14:
		if (vs->sr->cursystype == SYSTEM_E) ai->dhgr = 1;
		break;
	case 15:
		ai->dhgr = 0;
		break;
	}
	update_video_ap(vs);
	video_update_mode(vs);
}
