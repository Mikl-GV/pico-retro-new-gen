/*
	Agat Emulator version 1.19.2
	Copyright (c) NOP, nnop@newmail.ru
	chargen - emulation of Agat-9's programmable character generator
*/

#include "videoint.h"
#include <stdio.h>

struct CHARGEN_STATE
{
	struct SLOT_RUN_STATE*st;
	byte	mode;
};

static int chargen_term(struct SLOT_RUN_STATE*st)
{
	free(st->data);
	return 0;
}


static void set_mode(struct CHARGEN_STATE*ccs, byte mode)
{
//	printf("new mode: %X\n", mode);
	system_command(ccs->st->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
	if (ccs->mode == mode) return;
//	mode  &= ~4;
	ccs->mode = mode;
	enable_slot_xio(ccs->st, mode&1);
	video_select_font(ccs->st->sr, (mode&4)?-1:((mode&2)?1:0));
}

static void finish_init(struct CHARGEN_STATE*ccs)
{
	struct SLOT_RUN_STATE*st = ccs->st->sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;
	vs->num_fonts = 2;
//	memcpy(vs->font[1], vs->font[0], sizeof(vs->font[1]));
}

static int chargen_save(struct SLOT_RUN_STATE*st, OSTREAM*out)
{
	struct CHARGEN_STATE*ccs = st->data;
	WRITE_FIELD(out, ccs->mode);
	return 0;
}

static int chargen_load(struct SLOT_RUN_STATE*st, ISTREAM*in)
{
	struct CHARGEN_STATE*ccs = st->data;
	byte mode;

	READ_FIELD(in, mode);

	set_mode(ccs, mode);
	return 0;
}


static int chargen_command(struct SLOT_RUN_STATE*st, int cmd, int data, long param)
{
	struct CHARGEN_STATE*ccs = st->data;
	switch (cmd) {
	case SYS_COMMAND_HRESET:
		set_mode(ccs, 0);
		return 0;
	case SYS_COMMAND_INIT_DONE:
		finish_init(ccs);
		return 0;
	}
	return 0;
}

static byte chargen_xrom_r(word adr, struct CHARGEN_STATE*ccs) // C800-CFFF
{
	struct SLOT_RUN_STATE*st = ccs->st->sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;
	const byte*fnt = vs->font[1][0];
	return fnt[adr&0x7FF];
}

static void chargen_xrom_w(word adr, byte data, struct CHARGEN_STATE*ccs) // C800-CFFF
{
	struct SLOT_RUN_STATE*st = ccs->st->sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;
	byte*fnt = vs->font[1][0];
	fnt[adr&0x7FF] = data;
	if (vs->cur_font == 1) video_repaint_screen(vs);
}

static void chargen_io_w(word adr, byte data, struct CHARGEN_STATE*ccs) // C0X0-C0XF
{
	set_mode(ccs, (adr & 0x07));
}

int  chargen_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*cf)
{
	struct CHARGEN_STATE*ccs;

	puts("in chargen_init");

	ccs = calloc(1, sizeof(*ccs));
	if (!ccs) return -1;

	ccs->st = st;

	ccs->mode = 0;

	st->data = ccs;
	st->free = chargen_term;
	st->command = chargen_command;
	st->load = chargen_load;
	st->save = chargen_save;

	fill_rw_proc(st->baseio_sel, 1, empty_read_addr, chargen_io_w, ccs);
	fill_rw_proc(&st->xio_sel, 1, chargen_xrom_r, chargen_xrom_w, ccs);

	return 0;
}
