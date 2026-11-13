/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	baseram - emulation of base ROM module for both models
*/

#include "sysconf.h"
#include "types.h"
#include "streams.h"
#include "debug.h"

#include "memory.h"
#include "runmgrint.h"

#include <stdio.h>


#define MEM7_BLOCK_SIZE 0x4000
#define MEM7_BLOCK_SHIFT 0x0E

#define RAM9_BANK_SHIFT	13
#define RAM9_BANK_SIZE	0x2000

/* r1297: единый тег для прототипов и определения (GCC строже MSVC). */
struct BASERAM_STATE;

void system9_apple_mode(word adr, byte d, struct BASERAM_STATE*st);
void system9_cancel_apple_mode(word adr, byte d, struct BASERAM_STATE*st);

static const int startup_ram9_mapping[16]={0,1,2,3,4,5,6,7,0,0,0,0,0,0,0,0};

struct BASERAM_STATE_SAV
{
	struct SYS_RUN_STATE*sr;
	dword ram_size;
	dword basemem7_mapping[3];
	int   ram9_mapping[16];
	int   apple_emu;
	byte  psrom9_mode;
	int   psrom9_ofs;
	byte *ram;
	byte  apple_rom_mode;
};

struct RAM2E_STATE
{
	int ramrd, ramwrt, store80, altzp;
};

struct BASERAM_STATE
{
	struct SYS_RUN_STATE*sr;
	dword ram_size;
	dword basemem7_mapping[3];
	int   ram9_mapping[16];
	int   apple_emu;
	byte  psrom9_mode;
	int   psrom9_ofs;
	byte *ram;
	byte  apple_rom_mode;
	int   last_write;
	struct RAM2E_STATE ram2e;
};



static byte ram_read(word adr,struct BASERAM_STATE*st);
static void ram_write(word adr,byte d, struct BASERAM_STATE*st);

static byte rame_read(word adr,struct BASERAM_STATE*st);
static void rame_write(word adr,byte d, struct BASERAM_STATE*st);

static byte ram7_read(word adr,struct BASERAM_STATE*st);
static void ram7_write(word adr,byte d, struct BASERAM_STATE*st);

static int ram_free(struct SLOT_RUN_STATE*ss)
{
	struct BASERAM_STATE*st = ss->data;
	free(st->ram);
	free(st);
	return 0;
}

static void apple_set_ext_rom_mode(int read, int write, int what, struct BASERAM_STATE*st);
static byte apple_read_psrom_mode(word adr, struct BASERAM_STATE*st);
static void apple_write_psrom_mode(word adr, byte d, struct BASERAM_STATE*st);

static struct BASERAM_STATE*ram_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*ss, struct SLOTCONFIG*sc)
{
	struct BASERAM_STATE*st;

	st = calloc(1, sizeof(*st));
	if (!st) return NULL;

	st->sr = sr;
	st->ram_size = memsizes_b[sc->cfgint[CFG_INT_MEM_SIZE]];
	st->ram = malloc(st->ram_size);
	if (!st->ram) {
		free(st);
		return NULL;
	}
	clear_block(st->ram, st->ram_size);
	return st;
}

static int baseram_save(struct SLOT_RUN_STATE*ss, OSTREAM*out)
{
	struct BASERAM_STATE_SAV st0;
	struct BASERAM_STATE*st = ss->data;
	memcpy(&st0, st, sizeof(st0));
	WRITE_FIELD(out, st0);
	printf("written apple rom mode: %02X, ofs = %X\n", st->apple_rom_mode, st->psrom9_ofs);
	oswrite(out, st->ram, st->ram_size);
	if (ss->sr->sys.save_system)
		ss->sr->sys.save_system(ss->sr, out);
	switch (ss->sr->cursystype) {
	case SYSTEM_E:
		WRITE_FIELD(out, st->ram2e);
		break;
	}
	return 0;
}

static void upd_apple(struct BASERAM_STATE*st, int what)
{
	switch (st->apple_rom_mode&3) {
	case 2:
		apple_set_ext_rom_mode(1, 0, what, st);
		break;
	case 1:
		apple_set_ext_rom_mode(0, 1, what, st);
		break;
	case 0:
		apple_set_ext_rom_mode(0, 0, what, st);
		break;
	case 3:
		apple_set_ext_rom_mode(1, 1, what, st);
		break;
	}
}

static int baseram_load(struct SLOT_RUN_STATE*ss, ISTREAM*in)
{
	struct BASERAM_STATE*st = ss->data;
	struct BASERAM_STATE_SAV st0;
	int lm = st->apple_emu;
	READ_FIELD(in, st0);
	st0.sr = st->sr;
	st0.ram = st->ram;
	memcpy(st, &st0, sizeof(st0));
	isread(in, st->ram, st->ram_size);
	system_command(st->sr, SYS_COMMAND_REPAINT, 0, 0);
	if (st->apple_emu != lm) {
		if (st->apple_emu) {
			system9_apple_mode(0, 0, st);
		} else {
			system9_cancel_apple_mode(0, 0, st);
		}
		st->apple_rom_mode = st0.apple_rom_mode;
		st->psrom9_ofs = st0.psrom9_ofs;
		printf("loaded apple rom mode: %02X, ofs = %X\n", st->apple_rom_mode, st->psrom9_ofs);
	}
	if (ss->sr->sys.load_system)
		ss->sr->sys.load_system(ss->sr, in);
	switch (ss->sr->cursystype) {
	case SYSTEM_9:
		if (!st->apple_emu) break;
		upd_apple(st, 3);
		break;
	case SYSTEM_E:
		READ_FIELD(in, st->ram2e);
	case SYSTEM_A:
	case SYSTEM_P:
		upd_apple(st, 3);
		break;
	}
	return 0;
}

static int rama_command(struct SLOT_RUN_STATE*ss, int cmd, int data, long param)
{
	struct BASERAM_STATE*st = ss->data;
	switch (cmd) {
	case SYS_COMMAND_HRESET:
		st->psrom9_ofs = 0;
		apple_set_ext_rom_mode(0, 1, 3, st);
		clear_block(st->ram, st->ram_size);
		memset(&st->ram2e, 0, sizeof(st->ram2e));
	}
	return 0;
}


int rama_install(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*ss, struct SLOTCONFIG*sc)
{

	struct BASERAM_STATE*st;

	st = ram_init(sr, ss, sc);
	if (!st) return -1;

	ss->data = st;
	ss->free = ram_free;
	ss->command = rama_command;

	{
		int nb = (st->ram_size>>BASEMEM_BLOCK_SHIFT);
		if (nb > 24) nb = 24;
//		printf("ram_size = %i; nb = %i\n", st->ram_size, nb);
		fill_rw_proc(sr->base_mem, nb, ram_read, ram_write, st);
	}
	if (sr->config->slots[CONF_SLOT0].dev_type == DEV_MEMORY_XRAMA) { // language card installed
		st->ram_size = 0x10000;
		st->ram = realloc(st->ram, st->ram_size);
		if (!st->ram) {
			free(st);
			return -1;
		}
		fill_rw_proc(st->sr->baseio_sel+8, 1, apple_read_psrom_mode, apple_write_psrom_mode, st);
		st->apple_rom_mode = 0xC1;
		apple_set_ext_rom_mode(0, 1, 3, st);
	}
	return 0;
}

int rame_install(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*ss, struct SLOTCONFIG*sc)
{

	struct BASERAM_STATE*st;

	st = ram_init(sr, ss, sc);
	if (!st) return -1;

	ss->data = st;
	ss->free = ram_free;
	ss->command = rama_command;

	{
		int nb = (st->ram_size>>BASEMEM_BLOCK_SHIFT);
		if (nb > 24) nb = 24;
//		printf("ram_size = %i; nb = %i\n", st->ram_size, nb);
		fill_rw_proc(sr->base_mem, nb, rame_read, rame_write, st);
	}

	fill_rw_proc(st->sr->baseio_sel+8, 1, apple_read_psrom_mode, apple_write_psrom_mode, st);
	st->apple_rom_mode = 0xC1;
	apple_set_ext_rom_mode(0, 1, 3, st);
	return 0;
}

static int ram1_command(struct SLOT_RUN_STATE*ss, int cmd, int data, long param)
{
	struct BASERAM_STATE*st = ss->data;
	switch (cmd) {
	case SYS_COMMAND_HRESET:
		clear_block(st->ram, st->ram_size);
		break;
	}
	return 0;
}

static byte ram1_read(word adr,struct BASERAM_STATE*st)
{
	adr &= 0xFFF;
	adr = st->ram_size - 0x1000 + adr;
	return st->ram[adr];
}

static void ram1_write(word adr,byte d,struct BASERAM_STATE*st)
{
	adr &= 0xFFF;
	adr = st->ram_size - 0x1000 + adr;
	st->ram[adr] = d;
}


int ram1_install(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*ss, struct SLOTCONFIG*sc)
{

	struct BASERAM_STATE*st;

	st = ram_init(sr, ss, sc);
	if (!st) return -1;

	ss->data = st;
	ss->free = ram_free;
	ss->command = ram1_command;

	{
		int nb = (st->ram_size>>BASEMEM_BLOCK_SHIFT);
		printf("ram_size = %i; nb = %i\n", st->ram_size, nb);
		switch (nb) {
		case 4: // 8K, 4K low + 4K high
			fill_rw_proc(sr->base_mem + (0xE000>>BASEMEM_BLOCK_SHIFT), 0x1000>>BASEMEM_BLOCK_SHIFT, ram1_read, ram1_write, st);
		case 2: // 4K, only low
			fill_rw_proc(sr->base_mem, 1, ram_read, ram_write, st);
			break;
		case 16: // 32K low
			fill_rw_proc(sr->base_mem, nb, ram_read, ram_write, st);
			break;
		default: // 32K low + 4K high
			fill_rw_proc(sr->base_mem, 16, ram_read, ram_write, st);
			fill_rw_proc(sr->base_mem + (0xE000>>BASEMEM_BLOCK_SHIFT), 0x1000>>BASEMEM_BLOCK_SHIFT, ram1_read, ram1_write, st);
			break;
		}
	}
	return 0;
}
static int ramaa_command(struct SLOT_RUN_STATE*ss, int cmd, int data, long param)
{
	struct BASERAM_STATE*st = ss->data;
	switch (cmd) {
	case SYS_COMMAND_HRESET:
		clear_block(st->ram, st->ram_size);
		break;
	}
	return 0;
}

static byte ramaa_read1(word adr,struct BASERAM_STATE*st) // 0..3FF
{
	if (adr >= MEM_1K_SIZE) 
		return mem_proc_read(adr, st->sr->slots[CONF_MEMORY].service_procs + 0);
	return st->ram[adr];
}

static void ramaa_write1(word adr,byte d,struct BASERAM_STATE*st) // 0..3FF
{
	if (adr >= MEM_1K_SIZE)
		mem_proc_write(adr, d, st->sr->slots[CONF_MEMORY].service_procs + 0);
	else {
//		printf("ram[%X (%X)]=%X\n", adr, st->ram_size, d);
		st->ram[adr] = d;
	}
}

static byte ramaa_read2(word adr,struct BASERAM_STATE*st) // 2800..3BFF
{
	adr -= 0x2800;
	if (adr >= MEM_1K_SIZE*5)
		return mem_proc_read(adr + 0x2800, st->sr->slots[CONF_MEMORY].service_procs + 1);
	return st->ram[adr + MEM_1K_SIZE * 7];
}

static void ramaa_write2(word adr,byte d,struct BASERAM_STATE*st) // 2800..3BFF
{
	adr -= 0x2800;
	if (adr >= MEM_1K_SIZE*5) {
		mem_proc_write(adr + 0x2800, d, st->sr->slots[CONF_MEMORY].service_procs + 1);
		return;
	}
//	printf("ram[%X (%X)]=%X\n", adr + MEM_1K_SIZE * 7, st->ram_size, d);
	st->ram[adr + MEM_1K_SIZE * 7] = d;
}

static byte ramaa_read2_40k(word adr,struct BASERAM_STATE*st) // 2800..3BFF
{
	if ((adr - 0x2800) >= MEM_1K_SIZE*5)
		return mem_proc_read(adr, st->sr->slots[CONF_MEMORY].service_procs + 1);
	return st->ram[adr];
}

static void ramaa_write2_40k(word adr,byte d,struct BASERAM_STATE*st) // 2800..3BFF
{
	if ((adr - 0x2800) >= MEM_1K_SIZE*5) {
		mem_proc_write(adr, d, st->sr->slots[CONF_MEMORY].service_procs + 1);
	}
	st->ram[adr] = d;
}

static byte ramaa_noread2(word adr,struct BASERAM_STATE*st) // 2800..3BFF
{
	adr -= 0x2800;
	if (adr >= MEM_1K_SIZE*5)
		return mem_proc_read(adr + 0x2800, st->sr->slots[CONF_MEMORY].service_procs + 1);
	return empty_read_zero(adr, st);
}

static void ramaa_nowrite2(word adr,byte d,struct BASERAM_STATE*st) // 2800..3BFF
{
	adr -= 0x2800;
	if (adr >= MEM_1K_SIZE*5) {
		mem_proc_write(adr + 0x2800, d, st->sr->slots[CONF_MEMORY].service_procs + 1);
	}
	empty_write(adr, d, st);
}

static byte ramaa_read3_2k(word adr,struct BASERAM_STATE*st) // 8000..83FF
{
	adr -= 0x8000;
	if (adr >= MEM_1K_SIZE) return empty_read_zero(adr, st);
	return st->ram[adr + MEM_1K_SIZE];
}

static void ramaa_write3_2k(word adr,byte d,struct BASERAM_STATE*st) // 8000..83FF
{
	word adr0 = adr;
	byte ld;

	adr -= 0x8000;
	if (adr >= MEM_1K_SIZE) return;
	adr += MEM_1K_SIZE;
	ld = st->ram[adr];
	if (ld == d) return;
	st->ram[adr] = d;
	vid_invalidate_addr(st->sr, adr0);
}

static byte ramaa_read3_12k(word adr,struct BASERAM_STATE*st) // 8000..97FF
{
	adr -= 0x8000;
	return st->ram[adr + MEM_1K_SIZE];
}

static void ramaa_write3_12k(word adr,byte d,struct BASERAM_STATE*st) // 8000..97FF
{
	word adr0 = adr;
	byte ld;
//	printf("atom ram write: %04X, %02X\n", adr, d);
	adr -= 0x8000;
	adr += MEM_1K_SIZE;
//	printf("ram[%X (%X)]=%X\n", adr + MEM_1K_SIZE, st->ram_size, d);
	ld = st->ram[adr];
	if (ld == d) return;
	st->ram[adr] = d;
	vid_invalidate_addr(st->sr, adr0);
}

int ramaa_install(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*ss, struct SLOTCONFIG*sc)
{

	struct BASERAM_STATE*st;

	st = ram_init(sr, ss, sc);
	if (!st) return -1;

	printf("acorn atom ram initialization\n");

	ss->data = st;
	ss->free = ram_free;
	ss->command = ram1_command;

	fill_rw_proc(ss->service_procs, N_SERVICE_PROCS, 
		empty_read_zero, empty_write, st);

	{
		int nb = (st->ram_size>>BASEMEM_BLOCK_SHIFT);
		printf("ram_size = %i; nb = %i\n", st->ram_size, nb);
		switch (nb) {
		case 1: // 2K
			fill_rw_proc(sr->base_mem, 1, ramaa_read1, ramaa_write1, st); // 1K, 0..3FF
			fill_rw_proc(sr->base_mem + (0x2800>>BASEMEM_BLOCK_SHIFT), 3, ramaa_noread2, ramaa_nowrite2, st); // no memory
			fill_rw_proc(sr->base_mem + (0x8000>>BASEMEM_BLOCK_SHIFT), 1, ramaa_read3_2k, ramaa_write3_2k, st); // 1K, 8000..83FF
			break;
		case 6: // 12K
			fill_rw_proc(sr->base_mem, 1, ramaa_read1, ramaa_write1, st); // 1K
			fill_rw_proc(sr->base_mem + (0x2800>>BASEMEM_BLOCK_SHIFT), 3, ramaa_read2, ramaa_write2, st); // 5K
			fill_rw_proc(sr->base_mem + (0x8000>>BASEMEM_BLOCK_SHIFT), 3, ramaa_read3_12k, ramaa_write3_12k, st); // 6K
			break;
		case 20: // 40K
			fill_rw_proc(sr->base_mem, 20, ram_read, ram_write, st);
			fill_rw_proc(sr->base_mem, 1, ramaa_read1, ramaa_write1, st);
			fill_rw_proc(sr->base_mem + (0x2800>>BASEMEM_BLOCK_SHIFT), 3, ramaa_read2_40k, ramaa_write2_40k, st);
			fill_rw_proc(ss->service_procs, N_SERVICE_PROCS, ram_read, ram_write, st);
			break;
		default:
			return -1;
		}
	}
	return 0;
}


static void ram7_set_state(struct BASERAM_STATE*st, int state)
{
//	printf("basemem7: state=%i\n", state&15);
	switch (st->ram_size) {
	case 0x10000: // 64K
		if (state&1) {
			st->basemem7_mapping[2]=3 << MEM7_BLOCK_SHIFT;
		} else {
			st->basemem7_mapping[2]=2 << MEM7_BLOCK_SHIFT;
		}
		break;
	case 0x20000: // 128K
		if (state&8) {
			static const int no1[8]={1,1,4,5,1,1,4,5};
			static const int no2[8]={2,3,6,7,2,3,6,7};
			st->basemem7_mapping[0]=0;
			st->basemem7_mapping[1]=no1[state&7]<<MEM7_BLOCK_SHIFT;
			st->basemem7_mapping[2]=no2[state&7]<<MEM7_BLOCK_SHIFT;
		} else {
			static const int no[8]={2,3,6,7,2,3,4,5};
			st->basemem7_mapping[0]=0;
			st->basemem7_mapping[1]=1<<MEM7_BLOCK_SHIFT;
			st->basemem7_mapping[2]=no[state&7]<<MEM7_BLOCK_SHIFT;
		}
		break;
	}
}


void basemem7_state_w(word adr,byte data, struct BASERAM_STATE*st) // C0F0-C0FF
{
//	printf("basemem7_state_w @ %04X: %02X\n", adr, data);
	ram7_set_state(st, adr&0x0F);
}

byte basemem7_state_r(word adr, struct BASERAM_STATE*st) // C0F0-C0FF
{
//	printf("basemem7_state_r @ %04X\n", adr);
	ram7_set_state(st, adr&0x0F);
	return empty_read(adr, st);
}

static int ram7_command(struct SLOT_RUN_STATE*ss, int cmd, int data, long param)
{
	struct BASERAM_STATE*st = ss->data;
	switch (cmd) {
	case SYS_COMMAND_HRESET:
		st->basemem7_mapping[0] = 0;
		st->basemem7_mapping[1] = 1<<MEM7_BLOCK_SHIFT;
		st->basemem7_mapping[2] = 2<<MEM7_BLOCK_SHIFT;
		clear_block(st->ram, st->ram_size);
		if (st->ram_size > 0x8000) {
			fill_rw_proc(st->sr->base_mem + (0x8000>>BASEMEM_BLOCK_SHIFT), 0x4000>>BASEMEM_BLOCK_SHIFT, ram7_read, ram7_write, st);
		} else {
			fill_rw_proc(st->sr->base_mem + (0x8000>>BASEMEM_BLOCK_SHIFT), 0x4000>>BASEMEM_BLOCK_SHIFT, empty_read, empty_write, NULL);
		}
		break;
	case SYS_COMMAND_XRAM_RELEASE:
//		puts("baseram: restore ram");
		if (st->ram_size > 0x8000) {
			fill_rw_proc(st->sr->base_mem + (0x8000>>BASEMEM_BLOCK_SHIFT), 0x4000>>BASEMEM_BLOCK_SHIFT, ram7_read, ram7_write, st);
		} else {
			fill_rw_proc(st->sr->base_mem + (0x8000>>BASEMEM_BLOCK_SHIFT), 0x4000>>BASEMEM_BLOCK_SHIFT, empty_read, empty_write, NULL);
		}
		return 1;
	}
	return 0;
}

int ram7_install(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*ss, struct SLOTCONFIG*sc)
{
	struct BASERAM_STATE*st;
	st = ram_init(sr, ss, sc);
	if (!st) return -1;

	ss->data = st;
	ss->command = ram7_command;
	ss->free = ram_free;

	st->basemem7_mapping[0] = 0;
	st->basemem7_mapping[1] = 1<<MEM7_BLOCK_SHIFT;
	st->basemem7_mapping[2] = 2<<MEM7_BLOCK_SHIFT;

	switch (st->ram_size) {
	case 0x8000: // 32K
		fill_rw_proc(sr->base_mem, 0x8000>>BASEMEM_BLOCK_SHIFT, ram_read, ram_write, st);
		break;
	case 0x10000: // 64K
		fill_rw_proc(sr->base_mem, 0x8000>>BASEMEM_BLOCK_SHIFT, ram_read, ram_write, st);
		fill_rw_proc(sr->base_mem + (0x8000>>BASEMEM_BLOCK_SHIFT), 0x4000>>BASEMEM_BLOCK_SHIFT, ram7_read, ram7_write, st);
		break;
	case 0x20000: // 128K
		fill_rw_proc(sr->base_mem, 0x4000>>BASEMEM_BLOCK_SHIFT, ram_read, ram_write, st);
		fill_rw_proc(sr->base_mem + (0x4000>>BASEMEM_BLOCK_SHIFT), 0x8000>>BASEMEM_BLOCK_SHIFT, ram7_read, ram7_write, st);
		break;
	default:
		abort();
	}
	if (st->ram_size > 0x8000)
		fill_rw_proc(sr->baseio_sel + 15, 1, basemem7_state_r, basemem7_state_w, st);
	return 0;
}

static __inline dword ram7_offset(struct BASERAM_STATE*st, word adr)
{
	int ind=adr>>MEM7_BLOCK_SHIFT;
	return st->basemem7_mapping[ind]+(adr&(MEM7_BLOCK_SIZE-1));
}

static byte ram7_read(word adr,struct BASERAM_STATE*st)
{
	return st->ram[ram7_offset(st, adr)];
}

static void ram7_write(word adr, byte d,struct BASERAM_STATE*st)
{
	byte ld;
	dword radr;
	radr = ram7_offset(st, adr);
	ld = st->ram[radr];
	if (ld == d) return;
	st->ram[radr] = d;
	vid_invalidate_addr(st->sr, radr);
}

static byte ram_read(word adr,struct BASERAM_STATE*st)
{
	return st->ram[adr];
}

static void ram_write(word adr,byte d,struct BASERAM_STATE*st)
{
	byte ld = st->ram[adr];
	if (ld == d) return;
	st->ram[adr] = d;
	vid_invalidate_addr(st->sr, adr);
}

static byte rame_read(word adr, struct BASERAM_STATE*st)
{
	dword ladr = adr;
	if (adr < 0x200) { if (st->ram2e.altzp) ladr |= 0x10000; }
	else if (st->ram2e.store80) {
		if ((adr >=0x400 && adr < 0x800) || 
			(adr >=0x2000 && adr < 0x4000 && video_get_flags(st->sr, 0xC01D))) {
			if (video_get_flags(st->sr, 0xC01C)) ladr |= 0x10000;
		} else if (st->ram2e.ramrd) ladr |= 0x10000;
	} else { if (st->ram2e.ramrd) ladr |= 0x10000; }
	ladr &= (st->ram_size - 1);
	return st->ram[ladr];
}

static void rame_write(word adr, byte d, struct BASERAM_STATE*st)
{
	dword ladr = adr;
	if (adr < 0x200) { if (st->ram2e.altzp) ladr |= 0x10000; }
	else if (st->ram2e.store80) {
		if ((adr >=0x400 && adr < 0x800) || 
			(adr >=0x2000 && adr < 0x4000 && video_get_flags(st->sr, 0xC01D))) {
			if (video_get_flags(st->sr, 0xC01C)) ladr |= 0x10000;
		} else if (st->ram2e.ramwrt) ladr |= 0x10000;
	} else { if (st->ram2e.ramwrt) ladr |= 0x10000; }
	ladr &= (st->ram_size - 1);
	if (st->ram[ladr] == d) return;
	st->ram[ladr] = d;
	vid_invalidate_addr(st->sr, ladr);
}


static byte baseram9_read(word adr,struct BASERAM_STATE*st);
static void baseram9_write(word adr,byte d,struct BASERAM_STATE*st);

static byte baseram9_read_psrom(word adr,struct BASERAM_STATE*st);
static void baseram9_write_psrom(word adr,byte d,struct BASERAM_STATE*st);

static byte baseram9_read_mapping(word adr,struct BASERAM_STATE*st);
static void baseram9_write_mapping(word adr,byte d,struct BASERAM_STATE*st);

static byte baseram9_read_psrom_mode(word adr,struct BASERAM_STATE*st);
static void baseram9_write_psrom_mode(word adr,byte d,struct BASERAM_STATE*st);

static void baseram9_psrom_setmodeprocs(struct BASERAM_STATE*st, int rd, int wr, int flags)
{
//	printf("baseram9_psrom_mode: r %i, w %i\n", rd, wr);
	if (wr) { // write access
		if (flags&1) fill_write_proc(st->sr->base_mem+26,2,baseram9_write_psrom, st);
		if (flags&2) fill_write_proc(st->sr->base_mem+28,4,baseram9_write, st);
	} else {
		if (flags&1) fill_write_proc(st->sr->base_mem+26,2,empty_write, NULL);
		if (flags&2) fill_write_proc(st->sr->base_mem+28,4,empty_write, NULL);
	}
	if (rd) { // read_access
		if (flags&1) fill_read_proc(st->sr->base_mem+26,2,baseram9_read_psrom, st);
		if (flags&2) fill_read_proc(st->sr->base_mem+28,4,baseram9_read, st);
	} else {
			if (flags&1) fill_read_proc(st->sr->base_mem+26,2,empty_read_addr, NULL);
			if (flags&2) fill_read_proc(st->sr->base_mem+28,3,empty_read_addr, NULL);
			if (flags&2) system_command(st->sr, SYS_COMMAND_PSROM_RELEASE, 0, 0);
	}
}

static void baseram9_restore_segment(struct BASERAM_STATE*st, int ind)
{
	if (ind<6) {
		fill_read_proc(st->sr->base_mem+ind*4, 4, baseram9_read, st);
		fill_write_proc(st->sr->base_mem+ind*4, 4, baseram9_write, st);
	} else {
		switch (ind) {
		case 6: // d000
			switch (st->psrom9_mode&3) {
			case 0: baseram9_psrom_setmodeprocs(st, 1, 0, 1); break;
			case 1: baseram9_psrom_setmodeprocs(st, 0, 1, 1); break;
			case 2: baseram9_psrom_setmodeprocs(st, 1, 0, 1); break;
			case 3: baseram9_psrom_setmodeprocs(st, 1, 1, 1); break;
			}
			break;
		case 7: // e000
			switch (st->psrom9_mode&3) {
			case 0: baseram9_psrom_setmodeprocs(st, 1, 0, 2); break;
			case 1: baseram9_psrom_setmodeprocs(st, 0, 1, 2); break;
			case 2: baseram9_psrom_setmodeprocs(st, 1, 0, 2); break;
			case 3: baseram9_psrom_setmodeprocs(st, 1, 1, 2); break;
			}
			break;
		}
	}
}

void baseram9_write_psrom_mode(word adr,byte d, struct BASERAM_STATE*st)
{
	st->psrom9_mode=(adr&0x0F);
//	printf("baseram9_write_psrom_mode(%x) = %x\n",adr, st->psrom9_mode);
	if (st->psrom9_mode&8) st->psrom9_ofs=RAM9_BANK_SIZE/2;
	else st->psrom9_ofs=0;
	switch (st->psrom9_mode&3) {
	case 0: baseram9_psrom_setmodeprocs(st, 1, 0, 3); break;
	case 1: baseram9_psrom_setmodeprocs(st, 0, 1, 3); break;
	case 2: baseram9_psrom_setmodeprocs(st, 1, 0, 3); break;
	case 3: baseram9_psrom_setmodeprocs(st, 1, 1, 3); break;
	}
}


static int ram9_command(struct SLOT_RUN_STATE*ss, int cmd, int data, long param)
{
	struct BASERAM_STATE*st = ss->data;
	switch (cmd) {
	case SYS_COMMAND_HRESET:
		clear_block(st->ram, st->ram_size);
		system9_cancel_apple_mode(0, 0, st);
		memcpy(st->ram9_mapping, startup_ram9_mapping, sizeof(st->ram9_mapping));
		st->apple_rom_mode = 0xC0;
		st->psrom9_ofs = 0;
		baseram9_write_psrom_mode(1, 0, st);
		break;
	case SYS_COMMAND_BASEMEM9_RESTORE:
//		printf("*** baseram9_restore_segment(%i)\n", param);
		baseram9_restore_segment(st, param);
		return 1;
	case SYS_COMMAND_APPLE9_RESTORE:
//		printf("restore base ram for %s %s\n", (*(int*)param&1)?"read":"", (*(int*)param&2)?"write":"");
		switch (st->apple_rom_mode&3) {
		case 2:
			apple_set_ext_rom_mode(1, 0, *(int*)param, st);
			break;
		case 1:
			apple_set_ext_rom_mode(0, 1, *(int*)param, st);
			break;
		case 0:
			apple_set_ext_rom_mode(0, 0, *(int*)param, st);
			break;
		case 3:
			apple_set_ext_rom_mode(1, 1, *(int*)param, st);
			break;
		}
		return 1;
	}
	return 0;
}


static int get_addr_bank9(struct BASERAM_STATE*st, word adr, int bank)
{
	return (st->ram9_mapping[bank]<<RAM9_BANK_SHIFT)|(adr&(RAM9_BANK_SIZE-1));
}

static int get_addr9(struct BASERAM_STATE*st, word adr)
{
	return get_addr_bank9(st, adr, adr>>RAM9_BANK_SHIFT);
}

byte baseram9_read(word adr, struct BASERAM_STATE*st)
{
	return st->ram[get_addr9(st,adr)];
}

void baseram9_write(word adr, byte d, struct BASERAM_STATE*st)
{
	int ad1=get_addr9(st, adr);
	if (st->ram[ad1]==d) return;
	st->ram[ad1]=d;
	vid_invalidate_addr(st->sr, ad1);
}

byte baseram9_read_psrom(word adr,struct BASERAM_STATE*st)
{
	return baseram9_read((word)(adr+st->psrom9_ofs-RAM9_BANK_SIZE/2), st);
}

void baseram9_write_psrom(word adr,byte d, struct BASERAM_STATE*st)
{
	baseram9_write((word)(adr + st->psrom9_ofs-RAM9_BANK_SIZE/2), d, st);
}

byte apple_read_psrom_d(word adr, struct BASERAM_STATE*st)
{
	return st->ram[get_addr_bank9(st, adr, 14) + 
			st->psrom9_ofs - RAM9_BANK_SIZE/2];
}

byte apple_read_psrom_e(word adr, struct BASERAM_STATE*st)
{
	return st->ram[get_addr_bank9(st, adr, 15)];
}

void apple_write_psrom_d(word adr, byte d, struct BASERAM_STATE*st)
{
	st->ram[get_addr_bank9(st, adr, 14) + 
			st->psrom9_ofs - RAM9_BANK_SIZE/2] = d;
}

void apple_write_psrom_e(word adr, byte d, struct BASERAM_STATE*st)
{
	st->ram[get_addr_bank9(st, adr, 15)] = d;
}


byte apple_read_lang_d(word adr, struct BASERAM_STATE*st)
{
	dword ladr = adr;
	if (st->ram2e.altzp) ladr |= 0x10000;
	ladr &= (st->ram_size - 1);
	return st->ram[ladr + st->psrom9_ofs - RAM9_BANK_SIZE/2];
}

byte apple_read_lang_e(word adr, struct BASERAM_STATE*st)
{
	dword ladr = adr;
	if (st->ram2e.altzp) ladr |= 0x10000;
	ladr &= (st->ram_size - 1);
	return st->ram[ladr];
}

void apple_write_lang_d(word adr, byte d, struct BASERAM_STATE*st)
{
	dword ladr = adr;
	if (st->ram2e.altzp) ladr |= 0x10000;
	ladr &= (st->ram_size - 1);
	st->ram[ladr + st->psrom9_ofs - RAM9_BANK_SIZE/2] = d;
}

void apple_write_lang_e(word adr, byte d, struct BASERAM_STATE*st)
{
	dword ladr = adr;
	if (st->ram2e.altzp) ladr |= 0x10000;
	ladr &= (st->ram_size - 1);
	st->ram[ladr] = d;
}



byte baseram9_read_mapping(word adr, struct BASERAM_STATE*st)
{
	int ind=(adr&0xF0)>>4;
//	printf("baseram9_read_mapping[%x] = %i\n", adr, st->ram9_mapping[ind]);
//	system_command(st->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
	return (adr&0xF0)|st->ram9_mapping[ind];
}

void baseram9_write_mapping(word adr, byte d, struct BASERAM_STATE*st)
{
	int ind=(adr&0xF0)>>4;
//	printf("ram9_mapping[%i]=%x (%x)\n",ind, adr&0x0F, adr);
//	system_command(st->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
	st->ram9_mapping[ind]=adr&0x0F;
}

byte baseram9_read_psrom_mode(word adr, struct BASERAM_STATE*st)
{
	byte res = st->psrom9_mode|0xF0;
	if (!(res & 3)) res |= 2;
//	printf("baseram9_read_psrom_mode(%x) = %x\n",adr, st->psrom9_mode|(adr&0xF0)|4);
//	system_command(st->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
	return res;
}

void apple_set_ext_rom_mode(int read, int write, int what, struct BASERAM_STATE*st)
{
//	printf("apple language card emulation: read=%i, write=%i\n", read, write);
	if (st->apple_emu) {
		if (what & 1) {
			fill_read_proc(st->sr->base_mem+26,2,read?(void*)apple_read_psrom_d:(void*)baseram9_read_psrom,st);
			fill_read_proc(st->sr->base_mem+28,4,read?(void*)apple_read_psrom_e:(void*)baseram9_read,st);
		}
		if (what & 2) {
			fill_write_proc(st->sr->base_mem+26,2,write?(void*)apple_write_psrom_d:(void*)empty_write,st);
			fill_write_proc(st->sr->base_mem+28,4,write?(void*)apple_write_psrom_e:(void*)empty_write,st);
		}
	} else {
		if (what & 1) {
			if (!read) {
				system_command(st->sr, SYS_COMMAND_PSROM_RELEASE, 0, 0);
			} else {
				fill_read_proc(st->sr->base_mem+26,2,apple_read_lang_d,st);
				fill_read_proc(st->sr->base_mem+28,4,apple_read_lang_e,st);
			}
		}
		if (what & 2) {
			fill_write_proc(st->sr->base_mem+26,2,write?(void*)apple_write_lang_d:(void*)empty_write,st);
			fill_write_proc(st->sr->base_mem+28,4,write?(void*)apple_write_lang_e:(void*)empty_write,st);
		}
	}
}

byte apple_read_psrom_mode(word adr, struct BASERAM_STATE*st)
{
	int hi = adr & 8;
	int mde = adr & 3;
	byte last_mode = st->apple_rom_mode;
//	printf("apple_read_psrom_mode(%x)\n", adr);
	if (hi) st->psrom9_ofs=RAM9_BANK_SIZE/2;
	else st->psrom9_ofs=0;
	switch (mde) {
	case 0:
		st->apple_rom_mode = 0xC2 | hi;
		apple_set_ext_rom_mode(1, 0, 3, st);
		st->last_write = 0;
		break;
	case 1:
		st->apple_rom_mode = 0xC0 | hi | st->last_write;
		apple_set_ext_rom_mode(0, st->last_write, 3, st);
		st->last_write = 1;
		break;
	case 2:
		st->apple_rom_mode = 0xC0 | hi;
		apple_set_ext_rom_mode(0, 0, 3, st);
		st->last_write = 0;
		break;
	case 3: 
		st->apple_rom_mode = 0xC2 | hi | st->last_write;
		apple_set_ext_rom_mode(1, st->last_write, 3, st);
		st->last_write = 1;
		break;
	}
	return last_mode;
}

void apple_write_psrom_mode(word adr, byte d, struct BASERAM_STATE*st)
{
	apple_read_psrom_mode(adr, st);
}


void system9_apple_mode(word adr, byte d, struct BASERAM_STATE*st)
{
	extern int cpu_debug;
	puts("apple mode");
//	dump_mem(st->sr, 0, 0x10000, "before_apple.bin");
//	dump_baseram(st->sr, "baseram_before_apple.bin");
//	cpu_debug = 1;
//	system_command(st->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
	st->sr->apple_emu = st->apple_emu = 1;
	st->last_write = 0;
	fill_rw_proc(st->sr->baseio_sel+8, 1, apple_read_psrom_mode, apple_write_psrom_mode, st);
	fill_rw_proc(st->sr->io_sel+1, 1, empty_read_addr, empty_write, st);
	fill_read_proc(st->sr->base_mem,24,baseram9_read, st);
	fill_write_proc(st->sr->base_mem,24,baseram9_write, st);
	st->apple_rom_mode = 0xC1;
	apple_set_ext_rom_mode(0, 1, 3, st);
	system_command(st->sr, SYS_COMMAND_APPLEMODE, 1, 0);
//	dump_mem(st->sr, 0, 0x10000, "after_apple.bin");
}

void system9_cancel_apple_mode(word adr, byte d, struct BASERAM_STATE*st)
{
	puts("cancel apple mode");
	st->sr->apple_emu = st->apple_emu = 0;
	fill_rw_proc(st->sr->base_mem, 24, baseram9_read, baseram9_write, st);
	fill_read_proc(st->sr->base_mem+26, 5, empty_read_addr, NULL);
	fill_write_proc(st->sr->base_mem+26, 2, baseram9_write_psrom, st);
	fill_write_proc(st->sr->base_mem+28,4, baseram9_write, st);
	fill_rw_proc(st->sr->baseio_sel+8, 1, baseram9_read_psrom_mode, baseram9_write_psrom_mode, st);
	fill_rw_proc(st->sr->io_sel+1, 1, baseram9_read_mapping, baseram9_write_mapping, st);
	fill_write_proc(st->sr->baseio_sel + 15, 1, system9_apple_mode, st);
	baseram9_write_psrom_mode(1, 0, st);
	system_command(st->sr, SYS_COMMAND_APPLEMODE, 0, 0);
}

int ram9_install(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*ss, struct SLOTCONFIG*sc)
{
	struct BASERAM_STATE*st;
	st = ram_init(sr, ss, sc);
	if (!st) return -1;

	ss->data = st;
	ss->command = ram9_command;
	ss->free = ram_free;

	clear_block(st->ram, st->ram_size);
	memcpy(st->ram9_mapping, startup_ram9_mapping, sizeof(st->ram9_mapping));
	baseram9_write_psrom_mode(1, 0, st);

	fill_rw_proc(sr->base_mem, 24, baseram9_read, baseram9_write, st);
	fill_read_proc(sr->base_mem+26, 5, empty_read_addr, NULL);
	fill_write_proc(sr->base_mem+26, 2, baseram9_write_psrom, st);
	fill_write_proc(sr->base_mem+28,4, baseram9_write, st);
	fill_rw_proc(sr->baseio_sel+8, 1, baseram9_read_psrom_mode, baseram9_write_psrom_mode, st);
	fill_rw_proc(sr->io_sel+1, 1, baseram9_read_mapping, baseram9_write_mapping, st);
	fill_write_proc(sr->baseio_sel + 15, 1, system9_apple_mode, st);
	return 0;
}

int ram_install(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*ss, struct SLOTCONFIG*sc)
{
	ss->save = baseram_save;
	ss->load = baseram_load;
	switch (sr->cursystype) {
	case SYSTEM_7:
		return ram7_install(sr, ss, sc);
	case SYSTEM_9:
		return ram9_install(sr, ss, sc);
	case SYSTEM_A:
	case SYSTEM_P:
		return rama_install(sr, ss, sc);
	case SYSTEM_1:
		return ram1_install(sr, ss, sc);
	case SYSTEM_E:
		return rame_install(sr, ss, sc);
	case SYSTEM_AA:
		return ramaa_install(sr, ss, sc);
	}
	return -1;
}

byte* ramptr(struct SYS_RUN_STATE*sr)
{
	struct BASERAM_STATE*st = sr->slots[CONF_MEMORY].data;
	return st->ram;
}

int basemem_n_blocks(struct SYS_RUN_STATE*sr)
{
	struct BASERAM_STATE*st = sr->slots[CONF_MEMORY].data;
	return st->ram_size>>BASEMEM_BLOCK_SHIFT;
}


int dump_baseram(struct SYS_RUN_STATE*sr, const char*fname)
{
	struct BASERAM_STATE *st = sr->slots[CONF_MEMORY].data;
	FILE*f = fopen(fname, "wb");
	if (!f) return -1;
	fwrite(st->ram, 1, st->ram_size, f);
	fclose(f);
	return 0;
}


byte baseram_read_ext_state(word adr, struct SYS_RUN_STATE*sr)
{
	struct BASERAM_STATE*st = sr->slots[CONF_MEMORY].data;
	switch (adr) {
	case 0xC013: return st->ram2e.ramrd?0x80:0x00;
	case 0xC014: return st->ram2e.ramwrt?0x80:0x00;
	case 0xC016: return st->ram2e.altzp?0x80:0x00;
	case 0xC018: return st->ram2e.store80?0x80:0x00;
	}
	return 0;
}

void baseram_write_ext_state(word adr, struct SYS_RUN_STATE*sr)
{
	struct BASERAM_STATE*st = sr->slots[CONF_MEMORY].data;
//	printf("baseram_write_ext_state: %X\n", adr);
	switch (adr) {
	case 0xC000:
		st->ram2e.store80 = 0;
		break;
	case 0xC001:
		st->ram2e.store80 = 1;
		break;
	case 0xC002:
		st->ram2e.ramrd = 0;
		break;
	case 0xC003:
		st->ram2e.ramrd = 1;
		break;
	case 0xC004:
		st->ram2e.ramwrt = 0;
		break;
	case 0xC005:
		st->ram2e.ramwrt = 1;
		break;
	case 0xC008:
		st->ram2e.altzp = 0;
		break;
	case 0xC009:
		st->ram2e.altzp = 1;
		break;
	}
}

byte basemem_ext_bank(struct SYS_RUN_STATE*sr)
{
	struct BASERAM_STATE*st = sr->slots[CONF_MEMORY].data;
	return st->psrom9_ofs?0x0:0x80;
}

byte basemem_ext_enabled_ram(struct SYS_RUN_STATE*sr)
{
	struct BASERAM_STATE*st = sr->slots[CONF_MEMORY].data;
	return (st->apple_rom_mode&2)?0x80:0;
}
