#include "videoint.h"

void videosel_7(struct VIDEO_STATE*vs, int mode)
{
	int page, subpage, type;
	int basemem_blocks = basemem_n_blocks(vs->sr)>>3;
//	printf("basemem_blocks = %i\n", basemem_blocks);
	page=(mode>>4)&(basemem_blocks*2-1);
	subpage=(mode>>2)&3;
	type=(mode&3);
//	printf("video page = %i, subpage = %i, type = %i\n",
//		page, subpage, type);
	vs->rb_cur.vmode=mode;
	vs->rb_cur.vtype=type;
	vs->rb_cur.n_ranges = 1;
	switch (type) {
	case 0:
		vs->rb_cur.base_addr[0]=(page<<0x0D)+(subpage<<0x0B);
		vs->rb_cur.mem_size[0]=0x800;
		vs->rb_cur.el_size=1;
		break;
	case 1:
		vs->rb_cur.base_addr[0]=(page<<0x0D);
		vs->rb_cur.mem_size[0]=0x2000;
		vs->rb_cur.el_size=1;
		break;
	case 2:
		page&=7;
		vs->rb_cur.base_addr[0]=(page<<0x0D)+(subpage<<0x0B);
		vs->rb_cur.mem_size[0]=0x800;
		vs->rb_cur.el_size=2;
		if (mode&0x80) {
			vs->rb_cur.vtype=10;
			if (mode&4) { vs->rb_cur.vtype = 4; }
			vs->rb_cur.el_size=1;
		}
		break;
	case 3:
		vs->rb_cur.base_addr[0]=(page<<0x0D);
		vs->rb_cur.mem_size[0]=0x2000;
		vs->rb_cur.el_size=1;
		break;
	}
}

