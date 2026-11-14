#include "videoint.h"

void set_video_active_range(struct VIDEO_STATE*vs, dword adr, word len, int el)
{
//	printf("set_video_active_range start %x len %x\n", adr, len);
	vs->rb_cur.n_ranges = 1;
	vs->rb_cur.base_addr[0] = adr;
	vs->rb_cur.mem_size[0] = len;
	vs->rb_cur.el_size = el;
}

void set_video_type(struct VIDEO_STATE*vs, int t)
{
//	printf("set_video_type %i\n", t);
	vs->rb_cur.vtype = t;
}

int videosel(struct VIDEO_STATE*vs, int mode)
{
	int r=vs->rb_cur.vmode;
	if (vs->sr->apple_emu) return 0xFF;
	video_set_mode(vs, VIDEO_MODE_AGAT);
//	printf("videosel: %x->%x\n",vs->rb_cur.vmode,mode);
	switch(vs->sr->cursystype) {
	case SYSTEM_7:
		r=0xFF;
		if (vs->rb_cur.vmode==mode) return r;
		videosel_7(vs, mode);
		break;
	case SYSTEM_9:
//		if (vs->rb_cur.vmode == 10 && mode == 10) mode = 0x82;
		if (vs->rb_cur.vmode==mode) return r;
		videosel_9(vs, mode);
		break;
	default:
		return 0xFF;
	}
	if (!vs->rb_enabled) video_repaint_screen(vs);
	return r;
}

void video_update_rb(struct VIDEO_STATE*vs, int rbi)
{
	struct RASTER_BLOCK*rb = vs->rb + rbi;
	struct RASTER_BLOCK*crb = &vs->rb_cur;
	int lt;

	/* r1332: qrb_cur лежит сразу за массивом rb[MAX_RASTER_BLOCKS]; запись
	   с rbi вне массива затирает rb_cur (на стенде появлялись
	   vmode=0x4A и base=0x61000 вместо 0x0A/0x1000). */
	if (rbi < 0 || rbi >= MAX_RASTER_BLOCKS) {
		static int logged;
		if (!logged) {
			logged = 1;
			printf("video_update_rb: rbi=%d вне 0..%d (n_rb=%d) — пропуск\n",
			       rbi, MAX_RASTER_BLOCKS - 1, vs->n_rb);
		}
		return;
	}

	if (rb->vmode == crb->vmode) {
		if (rb->dirty) {
//			puts("video_repaint_block");
			video_repaint_block(vs, rbi);
			rb->dirty = 0;
		}
		return;
	}

	lt = rb->vtype;
	rb->vmode = crb->vmode;
	rb->vtype = crb->vtype;
	rb->prev_base[0] = rb->base_addr[0];
	rb->mem_size[0] = crb->mem_size[0] / vs->n_rb;
	rb->base_addr[0] = crb->base_addr[0] + rb->mem_size[0] * rbi;
	rb->n_ranges = crb->n_ranges;

	if (crb->n_ranges > 1) {
		rb->prev_base[1] = rb->base_addr[1];
		rb->mem_size[1] = crb->mem_size[1] / vs->n_rb;
		rb->base_addr[1] = crb->base_addr[1] + rb->mem_size[1] * rbi;
	}	

	rb->el_size = crb->el_size;

	if (lt == crb->vtype && !rb->dirty) {
//		puts("video_switch_block_pages");
		video_switch_block_pages(vs, rbi);
	} else {
//		puts("video_repaint_block");
		video_repaint_block(vs, rbi);
	}
	rb->dirty = 0;
}

void video_first_rb(struct VIDEO_STATE*vs)
{
	vs->rbi = -2;
//	if (vs->rbi < 0) vs->rbi = 0;
//	video_update_rb(vs, vs->rbi);
}

void video_next_rb(struct VIDEO_STATE*vs)
{
	++ vs->rbi;
	if (vs->rbi >= 0 && vs->rbi < vs->n_rb) video_update_rb(vs, vs->rbi);
}

void video_timer(struct VIDEO_STATE*vs, int t)
{
//	printf("video_timer: t = %i\n", t);
	switch (t) {
	case 0:
		if (vs->sr->ints_enabled) {
			system_command(vs->sr, SYS_COMMAND_NMI, 1, 0);
		}
		if (vs->rb_enabled) video_first_rb(vs);
		break;
	case 1:
		if (vs->sr->ints_enabled) {
			int del = 0;
			switch(vs->sr->cursystype) {
			case SYSTEM_7: del = N_RBINT_DELAY_7; break;
			case SYSTEM_9: del = N_RBINT_DELAY_9; break;
			}
			system_command(vs->sr, SYS_COMMAND_IRQ, del, 0);
		}
		if (vs->rb_enabled) video_next_rb(vs);
		break;
	}
}

