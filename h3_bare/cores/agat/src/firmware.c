/*
	Agat Emulator version 1.20
	Copyright (c) NOP, nnop@newmail.ru
	firmware - emulation of firmware card
*/

#include "sysconf.h"
#include "types.h"
#include "streams.h"
#include "debug.h"

#include "memory.h"
#include "runmgrint.h"

#define APPLEROM_SIZE 0x3000

struct FIRMWARE_STATE
{
	struct SYS_RUN_STATE*sr;
	word rom_size, rom_mask, rom_ofs;
	byte *rom;
	int  active, ini_active;
	int  flags;
};



static byte rom_read(word adr,struct FIRMWARE_STATE*st);


static void rom_reset_procs(struct SLOT_RUN_STATE*ss)
{
	struct FIRMWARE_STATE*st = ss->data;
	int sz = st->rom_size;
	int nb;
//	printf("firmware: rom_reset_procs\n");
	if (sz > 0x3000) sz = 0x3000;
	nb = sz>>BASEMEM_BLOCK_SHIFT;
	fill_read_proc(ss->sr->base_mem + (0xD000>>BASEMEM_BLOCK_SHIFT), (0x3000 - sz) >> BASEMEM_BLOCK_SHIFT, empty_read_addr, NULL);
	fill_read_proc(ss->sr->base_mem+BASEMEM_NBLOCKS-nb, nb, rom_read, st);
}

static int rom_restore_procs(struct SLOT_RUN_STATE*ss)
{
	struct FIRMWARE_STATE*st = ss->data;
	printf("firmware: rom_restore_procs(%X)\n", st->active);
	if (st->flags & CFG_INT_ROM_FLAG_F8MOD) {
		if (st->active == 1) return 0; // skip to base rom
	} else {
		if (st->active & 1) return 0; // skip to base rom
	}
	rom_reset_procs(ss);
	return 1;
}

static void activate_firmware(struct FIRMWARE_STATE*st, int act)
{
	if (act == st->active) return;
	st->active = act;
	system_command(st->sr, SYS_COMMAND_PSROM_RELEASE, 0, 0);
}


static int rom_save(struct SLOT_RUN_STATE*ss, OSTREAM*out)
{
	struct FIRMWARE_STATE*st = ss->data;
	WRITE_FIELD(out, st->active);
	return 0;
}

static int rom_load(struct SLOT_RUN_STATE*ss, ISTREAM*in)
{
	struct FIRMWARE_STATE*st = ss->data;
	READ_FIELD(in, st->active);
	system_command(ss->sr, SYS_COMMAND_PSROM_RELEASE, 0, 0);
	return 0;
}


static int rom_command(struct SLOT_RUN_STATE*ss, int cmd, int data, long param)
{
	struct FIRMWARE_STATE*st = ss->data;
	switch (cmd) {
	case SYS_COMMAND_RESET:
	case SYS_COMMAND_HRESET:
		activate_firmware(st, st->ini_active);
		rom_restore_procs(ss);
		break;
	case SYS_COMMAND_PSROM_RELEASE:
//		puts("firmware: restore rom");
		return rom_restore_procs(ss);
	case SYS_COMMAND_INIT_DONE:
		rom_restore_procs(ss);
		break;
	}
	return 0;
}

static int rom_free(struct SLOT_RUN_STATE*ss)
{
	struct FIRMWARE_STATE*st = ss->data;
	free(st->rom);
	free(st);
	return 0;
}

static byte rom_io_r(word adr, struct FIRMWARE_STATE*st) // C0X0-C0XF
{
	byte data = empty_read(adr, st);
//	printf("firmware: read io[%04X] <= %02X\n", adr, data);
//	activate_firmware(st, adr&3);
	return data;
}

static void rom_io_w(word adr, byte data, struct FIRMWARE_STATE*st) // C0X0-C0XF
{
//	printf("firmware: write io[%04X] <= %02X\n", adr, data);
	activate_firmware(st, adr&3);
}

int firmware_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*ss, struct SLOTCONFIG*sc)
{
	struct FIRMWARE_STATE*st;
	ISTREAM*in;

	st = calloc(sizeof(*st), 1);
	if (!st) return -1;

	st->sr = sr;

	st->rom_size = sc->cfgint[CFG_INT_ROM_SIZE];
	st->rom_mask = sc->cfgint[CFG_INT_ROM_MASK];
	st->rom_ofs = sc->cfgint[CFG_INT_ROM_OFS];
	st->rom = malloc(st->rom_size);
	if (!st->rom) {
		free(st);
		return -1;
	}

	st->flags = sc->cfgint[CFG_INT_ROM_FLAGS];
	if (!(st->flags & CFG_INT_ROM_FLAG_ACTIVE)) st->ini_active |= 1;
	if ((st->flags & CFG_INT_ROM_FLAG_F8MOD) && (st->flags & CFG_INT_ROM_FLAG_F8ACTIVE)) 
		st->ini_active |= 2;

	st->active = st->ini_active;

	printf("firmware card rom size: %X (%i bytes), rom file: %s\n", st->rom_size, st->rom_size, sc->cfgstr[CFG_STR_ROM]);
	printf("firmware card initial state: %X\n", st->ini_active);
	in=isfopen(sc->cfgstr[CFG_STR_ROM]);
	if (!in) {
		int r;
		r = load_buf_res(sc->cfgint[CFG_INT_ROM_RES], st->rom, st->rom_size);
		if (r < 0) {
			free(st->rom);
			free(st);
			return -2;
		}
	} else {
		if (isread(in,st->rom,st->rom_size)!=st->rom_size) {
			errprint(TEXT("can't read rom file %s"),sc->cfgstr[CFG_STR_ROM]);
			isclose(in);
			free(st->rom);
			free(st);
			return -3;
		}
		isclose(in);
	}

	fill_rw_proc(ss->baseio_sel, 1, rom_io_r, rom_io_w, st);

	ss->data = st;
	ss->free = rom_free;
	ss->command = rom_command;

	rom_restore_procs(ss);
	return 0;
}


static byte rom_read(word adr,struct FIRMWARE_STATE*st)
{
	if (adr >= 0xF800) {
		if ((st->flags & CFG_INT_ROM_FLAG_F8MOD) && !(st->active & 2))
			return system_read_rom(adr, st->sr);
	} else {
		if (st->active & 1) return system_read_rom(adr, st->sr);
	}
	return st->rom[(adr & st->rom_mask) - st->rom_ofs];
}

