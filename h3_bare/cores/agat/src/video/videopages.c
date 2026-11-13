#include "videoint.h"

void video_switch_block_pages(struct VIDEO_STATE*vs, int rbi)
{
	RECT inv={0,0,0,0};
	dword sa, da, n;
	byte*mem = ramptr(vs->sr);
	int upd=0;
	int j;
//	printf("video_switch_block_pages[%i]: type = %i, base = %x, prev_base = %x, size = %x\n", 
//		rbi, vs->rb[rbi].vtype, vs->rb[rbi].base_addr[0], vs->rb[rbi].prev_base[0], vs->rb[rbi].mem_size[0]);
	switch (vs->rb[rbi].el_size) {
	case 1:
		for (j = vs->rb[rbi].n_ranges-1; j>=0; --j) {
			for (sa=vs->rb[rbi].prev_base[j],da=vs->rb[rbi].base_addr[j],n=vs->rb[rbi].mem_size[j];n;n--,sa++,da++) {
				if (mem[sa]!=mem[da]) {
					RECT r;
					paint_addr[vs->rb[rbi].vtype](vs,da,&r);
					upd=1;
					UnionRect(&inv,&inv,&r);
				}
			}
		}
		break;
	case 2:
		for (j = vs->rb[rbi].n_ranges-1; j>=0; --j) {
			for (sa=vs->rb[rbi].prev_base[j],da=vs->rb[rbi].base_addr[j],n=vs->rb[rbi].mem_size[j];n;n-=2,sa+=2,da+=2) {
				if (mem[sa]!=mem[da]||mem[sa+1]!=mem[da+1]) {
					RECT r;
					paint_addr[vs->rb[rbi].vtype](vs,da,&r);
					upd=1;
					UnionRect(&inv,&inv,&r);
				}
			}
		}
		break;
	default:;
		abort();
	}
//	printf("upd = %i; inv = {%i,%i,%i,%i}\n", upd, inv.left, inv.top, inv.right, inv.bottom);
	if (upd) invalidate_video_window(vs->sr, &inv);
	vs->mem_access = 0;
	vs->tot_access = 0;
}
