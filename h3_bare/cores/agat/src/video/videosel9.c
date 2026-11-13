#include "videoint.h"

void videosel_9(struct VIDEO_STATE*vs, int mode)
{
	int page, subpage, type;
	page=(mode>>4)&0x07;
	subpage=(mode>>2)&3;
	type=(mode&3);
//	printf("select video mode: %x (last %x)\n", mode, vs->rb_cur.vmode);
/*	system_command(vs->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
	if ((mode&0xFF) == 0xA8) {
		dump_mem(vs->sr, 0, 0x10000, "memory.dmp");
		exit(1);
	}*/
	vs->rb_cur.vmode=mode;
	vs->rb_cur.vtype=type;
	vs->rb_cur.n_ranges = 1;
	switch (type) {
	case 0:
//		mode &= 0x7F;
		vs->rb_cur.vtype=5;
		if (mode&8) page|=8;
		page&=~1;
		vs->rb_cur.n_ranges = 2;
		vs->rb_cur.base_addr[0]=(page<<0x0D);
		vs->rb_cur.mem_size[0]=0x2000;
		vs->rb_cur.base_addr[1]=vs->rb_cur.base_addr[0]+vs->rb_cur.mem_size[0];
		vs->rb_cur.mem_size[1]=vs->rb_cur.mem_size[0];
		vs->rb_cur.el_size=1;
		break;
	case 1:
		if (mode&8) page|=8;
		vs->rb_cur.base_addr[0]=(page<<0x0D);
		vs->rb_cur.mem_size[0]=0x2000;
		vs->rb_cur.el_size=1;
		break;
	case 2:
		vs->rb_cur.base_addr[0]=(page<<0x0D)+(subpage<<0x0B);
		vs->rb_cur.mem_size[0]=0x800;
		vs->rb_cur.el_size=2;
		if (mode&0x80) {
			vs->rb_cur.vtype=4;
			vs->rb_cur.el_size=1;
		}
		break;
	case 3:
		if (mode&8) page|=8;
		if (mode&0x80) {
			vs->rb_cur.vtype=6;
			page&=~1;
			vs->rb_cur.n_ranges = 2;
			vs->rb_cur.base_addr[0]=(page<<0x0D);
			vs->rb_cur.mem_size[0]=0x2000;
			vs->rb_cur.base_addr[1]=vs->rb_cur.base_addr[0]+vs->rb_cur.mem_size[0];
			vs->rb_cur.mem_size[1]=vs->rb_cur.mem_size[0];
		} else{
			vs->rb_cur.mem_size[0]=0x2000;
			vs->rb_cur.base_addr[0]=(page<<0x0D);
		}
		vs->rb_cur.el_size=1;
		break;
	}
//	printf("selector=%x, vid_type=%i, base_addr=%x, page=%i\n",mode,vs->vid_type,vs->video_base_addr,page);
}

