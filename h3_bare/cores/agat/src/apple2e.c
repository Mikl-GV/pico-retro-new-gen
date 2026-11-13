/*
	Agat Emulator version 1.19
	Copyright (c) NOP, nnop@newmail.ru
*/

#include "runmgr.h"
#include "runmgrint.h"
#include "runstate.h"
#include "videow.h"

#include "resource.h"

struct APPLE2E_DATA
{
	byte ext_switches[8]; // C000..C00F
};


static int free_system_2e(struct SYS_RUN_STATE*sr);
static int restart_system_2e(struct SYS_RUN_STATE*sr);
static int save_system_2e(struct SYS_RUN_STATE*sr, OSTREAM*out);
static int load_system_2e(struct SYS_RUN_STATE*sr, ISTREAM*in);
static int xio_control_2e(struct SYS_RUN_STATE*sr, int req);

byte basemem_ext_bank(struct SYS_RUN_STATE*sr);
byte basemem_ext_enabled_ram(struct SYS_RUN_STATE*sr);

byte baseram_read_ext_state(word adr, struct SYS_RUN_STATE*sr);
void baseram_write_ext_state(word adr, struct SYS_RUN_STATE*sr);


static void write_C00X(word adr, byte data, struct SYS_RUN_STATE*sr)
{	
	struct APPLE2E_DATA*a2e = sr->sys.ptr;
	int en = adr & 1;
	int sel = adr&0x0E;
//	printf("set ext switch: %X\n", adr);
	a2e->ext_switches[sel>>1] = (en<<7);
	switch (sel) {
	case 0x00: //enable access to video memory via PAGE2 switch
	case 0x02: //read main/aux memory
	case 0x04: //write main/aux memory
	case 0x08: //select main/aux stack&zp
		baseram_write_ext_state(adr, sr);
		break;
	case 0x06:
		//select internal/external I/O ROM
		update_xio_status(sr);
		break;
	case 0x0A:
		//select internal/external ROM in slot 3
		break;
	case 0x0C:
		//select 40/80 col display
		if (basemem_n_blocks(sr) > 32)
			video_select_80col(sr, en);
		break;
	case 0x0E:
		video_select_font(sr, en);
		break;
	}
}

static byte read_C00X(word adr, struct SYS_RUN_STATE*sr)
{
	return keyb_read(adr, sr);
}


static void write_C01X(word adr, byte data, struct SYS_RUN_STATE*sr)
{
	sr->cur_key &= ~0x80;
}

static byte read_C01X(word adr, struct SYS_RUN_STATE*sr)
{
	struct APPLE2E_DATA*a2e = sr->sys.ptr;
	byte r = 0;
	switch (adr & 0x0F) {
	case 0x00:
		sr->cur_key &= ~0x80;
		if (sr->key_down) r = keyb_is_pressed(sr, sr->key_down);
		break;
	case 0x01:  //read BANK2
		r = basemem_ext_bank(sr);
		break;
	case 0x02:  //read HIGHRAM
		r = basemem_ext_enabled_ram(sr);
		break;
	case 0x03: // read RAMRD switch
	case 0x04: // read RAMWRT switch
	case 0x06: // read ALTZP switch
	case 0x08: // read 80STORE switch
		r = baseram_read_ext_state(adr, sr);
		break;
	case 0x05: // read SLOTCXROM switch
		r = a2e->ext_switches[3];
		break;
	case 0x07: // read SLOTC3ROM switch
		r = a2e->ext_switches[5];
		break;
	case 0x09: // read VBL
	case 0x0A: // read text mode switch
	case 0x0B: // read mixed switch
	case 0x0C: // read PAGE2 switch
	case 0x0D: // read HIRES switch
	case 0x0E: // read font charset
	case 0x0F: // read 80col switch
		r = video_get_flags(sr, adr);
		break;
	}
	r |=  (sr->cur_key&0x7F);
//	printf("get ext switch: %X = %x\n", adr, r);
	return r;
}

static void io_write_e(word adr, byte data, struct SYS_RUN_STATE*sr) // C000-C7FF
{
	struct APPLE2E_DATA*a2e = sr->sys.ptr;
	int ind = (adr>>8) & 7;

//	printf("io_write_e: %X, %X\n", adr, data);
	if ((ind && (a2e->ext_switches[3] >= 0x80)) || 
		((ind==3) && (a2e->ext_switches[5] < 0x80))) return;
	mem_proc_write(adr, data, sr->io_sel + ind);
}

static byte io_read_e(word adr, struct SYS_RUN_STATE*sr) // C000-C7FF
{
	struct APPLE2E_DATA*a2e = sr->sys.ptr;
	int ind = (adr>>8) & 7;
//	printf("io_read_e: %X: switches %X, %X\n", adr, a2e->ext_switches[3], a2e->ext_switches[5]);
	if ((ind && (a2e->ext_switches[3] >= 0x80)) || 
		((ind==3) && (a2e->ext_switches[5] < 0x80))) {
			return system_read_rom(adr, sr);
	}
	return mem_proc_read(adr, sr->io_sel + ind);
}


static byte keyb_apple_read(word adr, struct SYS_RUN_STATE*sr)
{
	return keyb_apple_state(sr, (adr&1)^1);
}

static byte keyb_lang_read(word adr, struct SYS_RUN_STATE*sr)
{
//	printf("hbit: %i\n", sr->input_hbit);
//	system_command(sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
	return ((sr->input_hbit||!sr->input_8bit)?0x80:0x00) | (sr->input_8bit?0x00:0x40);
}

static void keyb_lang_write(word adr, byte data, struct SYS_RUN_STATE*sr)
{
//	printf("lang_write: %02X\n", data);
//	system_command(sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
	sr->input_8bit = data & 1;
}

int init_system_2e(struct SYS_RUN_STATE*sr)
{
	struct APPLE2E_DATA*p;
	p = calloc(1, sizeof(*p));
	if (!p) return -1;
	sr->sys.ptr = p;
	sr->sys.free_system = free_system_2e;
	sr->sys.restart_system = restart_system_2e;
	sr->sys.save_system = save_system_2e;
	sr->sys.load_system = load_system_2e;
	sr->sys.xio_control = xio_control_2e;

	puts("init_system_2e");


	fill_read_proc(sr->base_mem, BASEMEM_NBLOCKS - 6, empty_read, NULL);
	fill_read_proc(sr->base_mem + BASEMEM_NBLOCKS - 6, 6, empty_read_addr, NULL);
	fill_write_proc(sr->base_mem, BASEMEM_NBLOCKS, empty_write, NULL);
	fill_read_proc(sr->baseio_sel, 16, empty_read, NULL);
	fill_write_proc(sr->baseio_sel, 16, empty_write, NULL);
	fill_read_proc(sr->io_sel, 8, empty_read, NULL);
	fill_write_proc(sr->io_sel, 8, empty_write, NULL);
	fill_read_proc(sr->io6_sel, 16, empty_read, NULL);
	fill_write_proc(sr->io6_sel, 16, empty_write, NULL);

	// C000
	fill_rw_proc(sr->base_mem + 24, 1, io_read_e, io_write_e, sr);
	fill_read_proc(sr->io_sel, 1, baseio_read, sr->baseio_sel);
	fill_write_proc(sr->io_sel, 1, baseio_write, sr->baseio_sel);
	fill_read_proc(sr->baseio_sel + 6, 1, io6_read, sr->io6_sel);
	fill_write_proc(sr->baseio_sel + 6, 1, io6_write, sr->io6_sel);

	fill_rw_proc(sr->baseio_sel + 0, 1, read_C00X, write_C00X, sr);
	fill_rw_proc(sr->baseio_sel + 1, 1, read_C01X, write_C01X, sr);
	fill_read_proc(sr->io6_sel + 1, 2, keyb_apple_read, sr); // will be overwritten by a joystick

	fill_rw_proc(&sr->rom_c800, 1, system_read_rom, empty_write, sr);

	if (sr->config->systype == SYSTEM_8A) {
		fill_rw_proc(sr->io6_sel + 0, 1, keyb_lang_read, keyb_lang_write, sr);
		sr->input_8bit = 1;
	}

	return 0;
}


int free_system_2e(struct SYS_RUN_STATE*sr)
{
	if (sr->sys.ptr) free(sr->sys.ptr);
	sr->sys.ptr = NULL;
	return 0;
}

int restart_system_2e(struct SYS_RUN_STATE*sr)
{
	struct APPLE2E_DATA*a2e = sr->sys.ptr;
	video_select_font(sr, sr->config->systype == SYSTEM_8A);
	memset(a2e, 0, sizeof(*a2e));
	return 0;
}

int save_system_2e(struct SYS_RUN_STATE*sr, OSTREAM*out)
{
	struct APPLE2E_DATA*a2e = sr->sys.ptr;
	puts("save_system_2e");
	oswrite(out, a2e, sizeof(*a2e));
	return 0;
}

int load_system_2e(struct SYS_RUN_STATE*sr, ISTREAM*in)
{
	struct APPLE2E_DATA*a2e = sr->sys.ptr;
	puts("load_system_2e");
	isread(in, a2e, sizeof(*a2e));
	return 0;
}

int xio_control_2e(struct SYS_RUN_STATE*sr, int req)
{
	struct APPLE2E_DATA*a2e = sr->sys.ptr;
	switch (req) {
	case 0:
//		printf("xio_control_2e: switch to system rom\n");
		sr->base_mem[0xC800 >> BASEMEM_BLOCK_SHIFT] = sr->rom_c800;
		break;
	case 1:
		return (a2e->ext_switches[3] < 0x80);
	}
	return 0;
}
