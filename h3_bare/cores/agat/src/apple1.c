/*
	Agat Emulator version 1.19
	Copyright (c) NOP, nnop@newmail.ru
*/

#include "runmgr.h"
#include "runmgrint.h"
#include "runstate.h"
#include "videow.h"

#include "resource.h"

#define TERM_WAIT 2000

struct APPLE1_DATA_SAVE
{
	int dsp_cr, kbd_cr;
	int remains;
	int pos[2];
	int lsz;
};

struct APPLE1_DATA
{
	int dsp_cr, kbd_cr;
	int remains;
	int pos[2];
	int lsz;
	int tty_speed, tty_flags;
};

static int free_system_1(struct SYS_RUN_STATE*sr);
static int restart_system_1(struct SYS_RUN_STATE*sr);
static int save_system_1(struct SYS_RUN_STATE*sr, OSTREAM*out);
static int load_system_1(struct SYS_RUN_STATE*sr, ISTREAM*in);


static void kbd_write(word adr, byte data, struct SYS_RUN_STATE*sr) // d010
{
}

static void kbdcr_write(word adr, byte data, struct SYS_RUN_STATE*sr) // d011
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
//	a1->kbd_cr = data & ~0x80;
}

static void scroll_bitmap(struct SYS_RUN_STATE*sr)
{
	int bmp_pitch = sr->bmp_pitch;
	byte*bmp_bits = sr->bmp_bits;
	memmove(bmp_bits, bmp_bits + bmp_pitch * CHAR_H, bmp_pitch * CHAR_H * 23);
	memset(bmp_bits + bmp_pitch * CHAR_H * 23, 0, bmp_pitch * CHAR_H);
	invalidate_video_window(sr, NULL);
}


static void output_bitmap(struct SYS_RUN_STATE*sr, int x, int y, byte data, int inv)
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
	int bmp_pitch = sr->bmp_pitch;
	byte*bmp_bits = sr->bmp_bits;
	byte*ptr;
	const byte*fnt = video_get_font(sr);
	int tc = 15, bc = 0;
	int xi, yi, xn, yn;
	if (!fnt) return;
	ptr =(byte*)bmp_bits+((y*bmp_pitch*CHAR_H+x*CHAR_W/2));
	fnt += data * 8;
	if (inv) { tc = 0; bc = 15; }
	for (yn=8;yn;yn--,fnt++) {
		byte*p=ptr, mask;
		for (xn=8,mask=0x01;xn;xn--,mask<<=1,p++) {
			byte cl;
#ifdef DOUBLE_X
			byte c=((*fnt)&mask)?tc:bc;
			cl=c|(c<<4);
#else
			byte c1, c2;
			c1=((*fnt)&mask)?tc:bc;
			mask>>=1;
			xn--;
			c2=((*fnt)&mask)?tc:bc;
			cl=c2|(c1<<4);
#endif
			p[0]=cl;
#ifdef DOUBLE_Y
			p[bmp_pitch]=cl;
#endif
		}
		ptr+=bmp_pitch;
#ifdef DOUBLE_Y
		ptr+=bmp_pitch;
#endif
	}
	invalidate_video_window(sr, NULL);
}

void flash_system_1(struct SYS_RUN_STATE*sr)
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
//	output_bitmap(sr, a1->pos[0], a1->pos[1], 0xC0, video_get_flash(sr));
	output_bitmap(sr, a1->pos[0], a1->pos[1], video_get_flash(sr)?0xC0:0xA0, 0);
}

static void video_clear(struct SYS_RUN_STATE*sr)
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
	int bmp_pitch = sr->bmp_pitch;
	byte*bmp_bits = sr->bmp_bits;
	memset(bmp_bits, 0, bmp_pitch * CHAR_H * 24);
	a1->lsz = 0;
	a1->pos[0] = a1->pos[1] = 0;
}

static void video_write(byte data, struct SYS_RUN_STATE*sr)
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
//	printf("output char: %i\n", data);
	switch (data) {
	case 0xDF: // backspace
		output_bitmap(sr, a1->pos[0], a1->pos[1], 0xA0, 0);
		if (a1->pos[0]) --a1->pos[0];
		else { a1->pos[0] = 39; if (a1->pos[1]) --a1->pos[1]; }
		if (a1->lsz) -- a1->lsz;
		break;
	case 0xFF: // right
		break;
	case 0x8C: // ctrl+L, extension
		if (a1->tty_flags & CFG_INT_TTY_FLAG_CLEAR)
			video_clear(sr);
		break;
	case 0x8A:
	case 0x8D:
		if (a1->pos[0] || !a1->lsz) {
			output_bitmap(sr, a1->pos[0], a1->pos[1], 0xA0, 0);
			a1->pos[0] = 0;
			++ a1->pos[1];
		}	
		a1->lsz = 0;
		break;
	default:
		output_bitmap(sr, a1->pos[0], a1->pos[1], data, 0);
		++ a1->pos[0];
		++ a1->lsz;
	}
	if (a1->pos[0] >= 40) { a1->pos[0] = 0; ++ a1->pos[1]; }
	if (a1->pos[1] >= 24) {
		scroll_bitmap(sr);
		a1->pos[1] = 23;
	}
//	output_bitmap(sr, a1->pos[0], a1->pos[1], 0xC0, video_get_flash(sr));
	output_bitmap(sr, a1->pos[0], a1->pos[1], video_get_flash(sr)?0xC0:0xA0, 0);
}

struct VIDEO_STATE
{
	struct SYS_RUN_STATE*sr;
	// ...
};

void apaint_apple1(struct VIDEO_STATE*vs, dword addr, RECT*r)
{
//	printf("apaint_apple1(%X): %i,%i,%i,%i\n", addr, r->left, r->top, r->right, r->bottom);
}

static void dsp_write(word adr, byte data, struct SYS_RUN_STATE*sr) // d012
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
	a1->dsp_cr |= 0x80;
	if (a1->tty_speed != 500) a1->remains = TERM_WAIT * 100 / a1->tty_speed;
//	printf("video_write: %X: lsz = %i\n", data, a1->lsz);
//	if (data & 0x80) 
	video_write(data | 0x80, sr);
}

static void dspcr_write(word adr, byte data, struct SYS_RUN_STATE*sr) // d013
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
	a1->dsp_cr = data;
	if (a1->tty_speed != 500) a1->remains = TERM_WAIT * 100 / a1->tty_speed;
}

static byte kbd_read(word adr, struct SYS_RUN_STATE*sr) // d010
{
	byte r = keyb_read(adr, sr);
	keyb_clear(sr);
//	printf("r = %x\n", r);
	return r;
}

static byte kbdcr_read(word adr, struct SYS_RUN_STATE*sr) // d011
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
	if (!keyb_preview(adr, sr)) a1->kbd_cr &= ~0x80;
	else a1->kbd_cr |= 0x80;
	return a1->kbd_cr;
}

static byte dsp_read(word adr, struct SYS_RUN_STATE*sr) // d012
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
	byte r = a1->dsp_cr;
//	printf("remains = %i\n", a1->remains);
	if (!a1->remains) a1->dsp_cr &= ~0x80;
	else -- a1->remains;
	return r;
}

static byte dspcr_read(word adr, struct SYS_RUN_STATE*sr) // d013
{
	return 0;
}

static void d000_write(word adr, byte data, struct SYS_RUN_STATE*sr) // d000-d7ff
{
//	printf("d000_write: %X = %X\n", adr, data);
	switch (adr) {
	case 0xD010: // KBD
		kbd_write(adr, data, sr);
		return;
	case 0xD011: // KBD CR
		kbdcr_write(adr, data, sr);
		return;
	case 0xD012: // DSP
		dsp_write(adr, data, sr);
		return;
	case 0xD013: // DSP CR
		dspcr_write(adr, data, sr);
		return;
	}
	empty_write(adr, data, sr);
}

static byte d000_read(word adr, struct SYS_RUN_STATE*sr) // d000-d7ff
{
	byte r;
	switch (adr) {
	case 0xD010: // KBD
		r = kbd_read(adr, sr);
		break;
	case 0xD011: // KBD CR
		r = kbdcr_read(adr, sr);
		break;
	case 0xD012: // DSP
		r = dsp_read(adr, sr);
		break;
	case 0xD013: // DSP CR
		r = dspcr_read(adr, sr);
		break;
	default:
		r = empty_read(adr, sr);
		break;
	}
//	printf("d000_read: %04X = %02X\n", adr, r);
	return r;
}

int init_system_1(struct SYS_RUN_STATE*sr)
{
	struct APPLE1_DATA*p;
	struct SLOTCONFIG*sc = sr->config->slots + CONF_MONITOR;
	p = calloc(1, sizeof(*p));
	if (!p) return -1;

	p->tty_speed = sc->cfgint[CFG_INT_TTY_SPEED];
	if (!p->tty_speed) p->tty_speed = 100;
	p->tty_flags = sc->cfgint[CFG_INT_TTY_FLAGS];

	sr->sys.ptr = p;
	sr->sys.free_system = free_system_1;
	sr->sys.restart_system = restart_system_1;
	sr->sys.save_system = save_system_1;
	sr->sys.load_system = load_system_1;

	fill_read_proc(sr->base_mem, BASEMEM_NBLOCKS, empty_read, NULL);
	fill_write_proc(sr->base_mem, BASEMEM_NBLOCKS, empty_write, NULL);
	fill_rw_proc(sr->base_mem + (0xD000>>BASEMEM_BLOCK_SHIFT), 1, d000_read, d000_write, sr);

	fill_read_proc(sr->io_sel, 8, empty_read, NULL);
	fill_write_proc(sr->io_sel, 8, empty_write, NULL);

	fill_read_proc(sr->base_mem + 24, 1, io_read, sr->io_sel);
	fill_write_proc(sr->base_mem + 24, 1, io_write, sr->io_sel);
	puts("init_system_1");
	return 0;
}


static int free_system_1(struct SYS_RUN_STATE*sr)
{
	if (sr->sys.ptr) free(sr->sys.ptr);
	sr->sys.ptr = NULL;
	return 0;
}

static int restart_system_1(struct SYS_RUN_STATE*sr)
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
	int bmp_pitch = sr->bmp_pitch;
	byte*bmp_bits = sr->bmp_bits;
	a1->dsp_cr = a1->kbd_cr = a1->remains = a1->pos[0] = a1->pos[1] = a1->lsz = 0;
	memset(bmp_bits, 0, bmp_pitch * CHAR_H * 24);
	return 0;
}

static int save_system_1(struct SYS_RUN_STATE*sr, OSTREAM*out)
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
	struct APPLE1_DATA_SAVE*a1s = (void*)a1;
	int bmp_pitch = sr->bmp_pitch;
	byte*bmp_bits = sr->bmp_bits;
	oswrite(out, a1s, sizeof(*a1s));
	oswrite(out, bmp_bits, bmp_pitch * 24 * CHAR_H);
	puts("save_system_1");
//	printf("pos: %i %i\n", a1->pos[0], a1->pos[1]);
	return 0;
}

static int load_system_1(struct SYS_RUN_STATE*sr, ISTREAM*in)
{
	struct APPLE1_DATA*a1 = sr->sys.ptr;
	struct APPLE1_DATA_SAVE a1s;
	int bmp_pitch = sr->bmp_pitch;
	byte*bmp_bits = sr->bmp_bits;
	isread(in, &a1s, sizeof(a1s));
	memcpy(a1, &a1s, sizeof(a1s));
	isread(in, bmp_bits, bmp_pitch * 24 * CHAR_H);
	puts("load_system_1");
//	printf("pos: %i %i\n", a1->pos[0], a1->pos[1]);
	invalidate_video_window(sr, NULL);
	return 0;
}


struct ACI_DATA
{
	struct SLOT_RUN_STATE*st;
	byte rom[256];
};

static byte aci_rom_r(word adr, struct ACI_DATA*aci) // CX00-CXFF
{
	return aci->rom[adr & 0xFF];
}

static void aci_io_w(word adr, byte data, struct ACI_DATA*aci) // c000-c0ff
{
//	printf("aci_io_w: %x, %x\n", adr, data);
	tape_toggle(aci->st->sr->slots + CONF_TAPE);
}

static byte aci_io_r(word adr, struct ACI_DATA*aci) // c000-c0ff
{
//	printf("aci_io_r: %x\n", adr);
	if (adr >= 0xC080) {
		byte v = tape_read(aci->st->sr->slots + CONF_TAPE);
		if (v & 0x80) adr ^= 1;
	}
	tape_toggle(aci->st->sr->slots + CONF_TAPE);
	return aci->rom[adr & 0xFF];
}


static int aci_term(struct SLOT_RUN_STATE*st)
{
	free(st->data);
	return 0;
}

int aci_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*cf)
{
	struct ACI_DATA*aci;
	ISTREAM*rom;

	aci = calloc(1, sizeof(*aci));
	if (!aci) return -1;

	aci->st = st;

	rom = isfopen(cf->cfgstr[CFG_STR_ROM]);
	if (!rom) {
		load_buf_res(cf->cfgint[CFG_INT_ROM_RES], aci->rom, 256);
	} else {
		isread(rom, aci->rom, 256);
		isclose(rom);
	}

	st->data = aci;
	st->free = aci_term;
	st->command = NULL;
	st->load = NULL;
	st->save = NULL;

	fill_rw_proc(st->io_sel, 1, aci_rom_r, empty_write, aci);
	fill_rw_proc(sr->io_sel + 0, 1, aci_io_r, aci_io_w, aci);
	return 0;
}
