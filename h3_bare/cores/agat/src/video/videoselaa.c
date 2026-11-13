#include "videoint.h"

#define ACORN_VIDEO_BASE 15

void acorn_select_vmode(int mode, struct SYS_RUN_STATE*sr)
{
	struct SLOT_RUN_STATE*st = sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;
	if (vs->rb_cur.vmode == mode) return;
	videosel_aa(vs, mode);
}

int acorn_get_vmode(struct SYS_RUN_STATE*sr)
{
	struct SLOT_RUN_STATE*st = sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;
	return vs->rb_cur.vmode;
}

void videosel_aa(struct VIDEO_STATE*vs, int mode)
{
	static const int vtypes[16] = {
		ACORN_VIDEO_BASE + 0, // mode 0, text 32x16
		ACORN_VIDEO_BASE + 1, // mode 1a, color graphic 64x64, 1K
		ACORN_VIDEO_BASE + 2, // mode 1, mono graphic 128x64, 1K
		ACORN_VIDEO_BASE + 2, // mode 1, mono graphic 128x64, 1K
		ACORN_VIDEO_BASE + 3, // mode 2a, color graphic 128x64, 2K
		ACORN_VIDEO_BASE + 3, // mode 2a, color graphic 128x64, 2K
		ACORN_VIDEO_BASE + 4, // mode 2, mono graphic 128x96, 1.5K
		ACORN_VIDEO_BASE + 4, // mode 2, mono graphic 128x96, 1.5K
		ACORN_VIDEO_BASE + 5, // mode 3a, color graphic 128x96, 3K
		ACORN_VIDEO_BASE + 5, // mode 3a, color graphic 128x96, 3K
		ACORN_VIDEO_BASE + 6, // mode 3, mono graphic 128x192, 3K
		ACORN_VIDEO_BASE + 6, // mode 3, mono graphic 128x192, 3K
		ACORN_VIDEO_BASE + 7, // mode 4a, color graphic 128x192, 6K
		ACORN_VIDEO_BASE + 7, // mode 4a, color graphic 128x192, 6K
		ACORN_VIDEO_BASE + 8, // mode 4, mono graphic 256x192, 6K
		ACORN_VIDEO_BASE + 8, // mode 4, mono graphic 256x192, 6K
	};
	static const int vsizes[16] = {
		0x200,
		0x400,
		0x400,
		0x400,
		0x800,
		0x800,
		0x600,
		0x600,
		0xC00,
		0xC00,
		0xC00,
		0xC00,
		0x1800,
		0x1800,
		0x1800,
		0x1800,
	};
	int type;
	mode &= 15;
	type = vtypes[mode];
//	printf("set acorn atom video mode: %i\n", mode);
	vs->rb_cur.vmode=mode;
	vs->rb_cur.vtype=type;
	vs->rb_cur.n_ranges = 1;
	vs->rb_cur.base_addr[0]=0x8000;
	vs->rb_cur.el_size=1;
	vs->rb_cur.mem_size[0]=vsizes[mode];
	video_update_rb(vs, 0);
}

