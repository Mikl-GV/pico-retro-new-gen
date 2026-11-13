/*
	Agat Emulator
	Copyright (c) NOP, nnop@newmail.ru
	fddliberty - emulation of Liberty Drive controller (Tashkent)
*/

#include "common.h"
#include "types.h"
#include "memory.h"
#include "streams.h"
#include "debug.h"
#include "sysconf.h"
#include "runmgr.h"
#include "runmgrint.h"

#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syslib.h"
#include "debug.h"

#include "localize.h"

#ifndef W_OK
#define W_OK 02
#endif

#define FDD_SECTOR_DATA_SIZE 512
#define FDD_MAX_SECTOR_SIZE 1024
#define SECT_PER_TRACK 10

#define FDD_ROM_SIZE 0x800

#define MAX_DRIVES	4

#define FDD_TIMER_DELAY 50

#define Dprintf(...)


struct FDD_DRIVE_DATA
{
	int	error;
	int	readonly;
	int	present;
	int	track, changed;
	int	type;
	int	ntracks, nheads, nsectors;
	int	sectsize, sectind;

	HMENU	submenu;

	char_t	disk_name[1024];
	IOSTREAM *disk;

};

struct FDD_DATA
{
	struct FDD_DRIVE_DATA	drives[MAX_DRIVES];
	int	initialized;
	byte	fdd_rom[FDD_ROM_SIZE];
	struct SYS_RUN_STATE	*sr;
	struct SLOT_RUN_STATE	*st;
	byte	buffer[FDD_MAX_SECTOR_SIZE];
	byte	regs[3]; // cmd, track, sector
	int	data_index;
	int	rom_mode;
	int	last_cmd;
	int	status_delay;
	int	rotate_delay;
	int	data_remains;
	int	step_dir;
	int	cur_drive, cur_head;
	int	enabled;
	int	use_fast;
};









static void fdd_io_write(unsigned short a,unsigned char d, struct FDD_DATA*xd);
static byte fdd_io_read(unsigned short a, struct FDD_DATA*xd);
static byte fdd_rom_read(unsigned short a, struct FDD_DATA*xd);


static void fill_fdd_drive(struct FDD_DRIVE_DATA*data)
{
	data->disk=0;
}

static void fill_fdd1(struct FDD_DATA*data)
{
	int i;
	for (i = 0; i < MAX_DRIVES; ++i)
		fill_fdd_drive(data->drives + i);
	data->initialized=0;
}

static int open_fdd1(struct FDD_DRIVE_DATA*drv,const char_t*name,int ro, int no)
{
	drv->changed = 1;
	if (drv->disk) {
		isclose(drv->disk);
		drv->disk = NULL;
	}
	if (!name) return 1;
//	if (access(name,W_OK)) ro=1;
	drv->disk=ro?isfopen(name):iosfopen(name);
	if (!ro&&!drv->disk) {
		ro = 1;
//		Dprintf("set ro to 1\n");
		drv->disk=isfopen(name);
	}
	if (!drv->disk) {
		ro = 1;
		drv->disk = isfopen_res(MAKEINTRESOURCE(33+no));
	}
	if (!drv->disk) {
		errprint(TEXT("can't open disk file %s"),name);
		return -1;
	}
//	Dprintf("ro: %i\n", ro);
	drv->readonly=ro;
//	Dprintf("readonly: %i\n", drv->readonly);
	drv->error = 0;
	return 0;
}


static int remove_fdd1(struct SLOT_RUN_STATE*sr)
{
	struct FDD_DATA*data = sr->data;
	int i;
	for (i = 0; i < MAX_DRIVES; ++i)
		open_fdd1(data->drives+i, NULL, 0, 0);
	return 0;
}

#define FDD1_BASE_CMD 7000

static int init_menu(struct FDD_DRIVE_DATA*drv, int s, int d, HMENU menu)
{
	TCHAR buf[1024], buf1[1024];
	if (drv->submenu) DeleteMenu(menu, (UINT)drv->submenu, MF_BYCOMMAND);
	if (drv->present) {
		drv->submenu = CreatePopupMenu();
		AppendMenu(drv->submenu, MF_STRING, FDD1_BASE_CMD + (s*4 + d*2 + 0), 
			localize_str(LOC_FDD, 2, buf1, sizeof(buf1)));
//			TEXT("Вставить диск..."));
		AppendMenu(drv->submenu, MF_STRING, FDD1_BASE_CMD + (s*4 + d*2 + 1), 
			localize_str(LOC_FDD, 3, buf1, sizeof(buf1)));
//			TEXT("Извлечь диск"));

		//TEXT("Дисковод Liberty (S%i,D%i)"),
		wsprintf(buf, localize_str(LOC_FDD, 5, buf1, sizeof(buf1)), s, d + 1);
		AppendMenu(menu, MF_POPUP, (UINT_PTR)drv->submenu, buf);
	} else drv->submenu = NULL;
	return 0;
}

static int free_menu(struct FDD_DRIVE_DATA*drv, int s, int d, HMENU menu)
{
	if (drv->submenu) {
		DeleteMenu(menu, (UINT)drv->submenu, MF_BYCOMMAND);
	}
	return 0;
}

static int update_menu(struct FDD_DRIVE_DATA*drv, int s, int d, HMENU menu)
{
	if (drv->present) {
		EnableMenuItem(drv->submenu, FDD1_BASE_CMD + (s*4 + d*2 + 1), (drv->disk?MF_ENABLED:MF_GRAYED)|MF_BYCOMMAND);
	}
	return 0;
}

static int fdd1_drvcmd(HWND wnd, struct FDD_DRIVE_DATA*drv, int cmd, int s, int d)
{
	if (drv->present) {
		if (cmd == FDD1_BASE_CMD + (s*4 + d*2 + 0)) {
			if (select_disk(wnd, drv->disk_name, &drv->readonly)) {
				open_fdd1(drv, drv->disk_name, drv->readonly, d);
			}
		} else if (cmd == FDD1_BASE_CMD + (s*4 + d*2 + 1)) {
			open_fdd1(drv, NULL, 0, d);
			drv->disk_name[0] = 0;
		}
	}
	return 0;
}

static int fdd1_wincmd(HWND wnd, struct FDD_DATA*data, int cmd, int s)
{
	int i;
	for (i = 0; i < MAX_DRIVES; ++i)
		fdd1_drvcmd(wnd, data->drives + i, cmd, s, i);
	return 0;
}

static int fdd1_command(struct SLOT_RUN_STATE*st, int cmd, int cdata, long param)
{
	struct FDD_DATA*data = st->data;
	HMENU menu;
	int i;
	switch (cmd) {
	case SYS_COMMAND_HRESET:
		data->rom_mode = 1;
		memset(data->regs, 0, sizeof(data->regs));
		data->last_cmd = 0;
		data->data_index = data->data_remains = 0;
		data->status_delay = data->rotate_delay = 0;
		data->cur_drive = data->cur_head = 0;
		data->regs[0] = 0x80;
		break;
	case SYS_COMMAND_RESET:
		break;
	case SYS_COMMAND_INITMENU:
		menu = (HMENU) param;
		for (i = 0; i < MAX_DRIVES; ++i)
			init_menu(data->drives + i, st->sc->slot_no, i, menu);
		break;
	case SYS_COMMAND_UPDMENU:
		menu = (HMENU) param;
		for (i = 0; i < MAX_DRIVES; ++i)
			update_menu(data->drives + i, st->sc->slot_no, i, menu);
		break;
	case SYS_COMMAND_FREEMENU:
		menu = (HMENU) param;
		for (i = 0; i < MAX_DRIVES; ++i)
			free_menu(data->drives + i, st->sc->slot_no, i, menu);
		break;
	case SYS_COMMAND_WINCMD:
		fdd1_wincmd((HWND)param, data, cdata, st->sc->slot_no);
		break;
	}
	return 0;
}

static int fdd1_save(struct SLOT_RUN_STATE*st, OSTREAM*out)
{
	struct FDD_DATA*data = st->data;
	int i;

	for (i = 0; i < MAX_DRIVES; ++i) {
		WRITE_FIELD(out, data->drives[i].present);
		WRITE_FIELD(out, data->drives[i].readonly);
		WRITE_ARRAY(out, data->drives[i].disk_name);
	}
	WRITE_ARRAY(out, data->regs);
	return 0;
}

static int fdd1_load(struct SLOT_RUN_STATE*st, ISTREAM*in)
{
	struct FDD_DATA*data = st->data;
	int i;

	for (i = 0; i < MAX_DRIVES; ++i) {
		READ_FIELD(in, data->drives[i].present);
		READ_FIELD(in, data->drives[i].readonly);
		READ_ARRAY(in, data->drives[i].disk_name);
	}
	READ_ARRAY(in, data->regs);

	for (i = 0; i < MAX_DRIVES; ++i) {
		if (data->drives[i].present) {
			open_fdd1(data->drives, data->drives[i].disk_name, data->drives[i].readonly & 1, i);
		}
	}
	data->initialized = 1;
	return 0;
}

static void enable_fdd_rom(struct FDD_DATA*data, int en)
{
//	printf("fdd_enable_rom: %i\n", en);
	enable_slot_xio(data->st, en);
}

static void set_rom_mode(struct FDD_DATA*data, byte mode)
{
	if ((mode ^ data->rom_mode) & 3) {
		if ((data->rom_mode & 3) == 3) enable_fdd_rom(data, 0);
		if ((mode & 3) == 3) enable_fdd_rom(data, 1);
		data->rom_mode = mode;
	}
}


static byte fdd_xrom_r(word adr, struct FDD_DATA*data) // C800-CFFF
{
//	Dprintf("fdd_xrom_r: %X\n", adr);
	if ((adr & 0xF00) == 0xF00) {
		set_rom_mode(data, data->rom_mode & ~2);
	}
	return data->fdd_rom[adr & (FDD_ROM_SIZE-1)];
}

static byte fdd_rom_r(word adr, struct FDD_DATA*data) // CX00-CXFF
{
//	extern int cpu_debug;
//	cpu_debug = 1;
//	Dprintf("fdd_rom_r: %X\n", adr);
	set_rom_mode(data, data->rom_mode | 2);
	return data->fdd_rom[(adr & 0xFF) | 0x700];
}


static void fdd_read_sector(struct FDD_DATA*data, int drv)
{
	struct FDD_DRIVE_DATA*d = data->drives + drv;
	int t, s, h, ofs, nr;
	if (!d->present || !d->disk) {
		data->regs[0] |= 0x10;
		return;
	}
	t = data->regs[1];
	s = data->regs[2];
	h = data->cur_head;
	
	if (h >= d->nheads) h = d->nheads - 1;

	if (s > d->nsectors || t >= d->ntracks) {
		printf("fdd: read: wrong t/s/h: %i/%i/%i\n", t, s, h);
		data->regs[0] |= 0x20; // sector not found
		return;
	}
	ofs = ((t * d->nheads + h) * d->nsectors + s - 1) * d->sectsize;
	Dprintf("reading sector from offset %X: sector %i, track %i head %i\n", ofs, s, t, h);
	iosseek(d->disk, ofs, SSEEK_SET);
	nr = iosread(d->disk, data->buffer, d->sectsize);
	data->data_remains = nr;
	data->data_index = 0;
	if (nr != d->sectsize) {
		data->regs[0] |= 0x10; // CRC error
	}
	data->regs[0] |= 0x02; // DRQ
}


static void fdd_write_sector(struct FDD_DATA*data, int drv)
{
	struct FDD_DRIVE_DATA*d = data->drives + drv;
	int t, s, h, ofs, nr;
	if (!d->present || !d->disk) {
		data->regs[0] |= 0x80;
		return;
	}
	if (d->readonly) {
		data->regs[0] |= 0x40;
		return;
	}
	t = data->regs[1];
	s = data->regs[2];
	h = data->cur_head;
	if (h >= d->nheads || s > d->nsectors || t >= d->ntracks) {
		printf("fdd: write: wrong t/s/h: %i/%i/%i\n", t, s, h);
		data->regs[0] |= 0x20; // sector not found
		return;
	}
	ofs = ((t * d->nheads + h) * d->nsectors + s - 1) * d->sectsize;
	Dprintf("writing sector into offset %X: sector %i, track %i head %i\n", ofs, s, t, h);
	iosseek(d->disk, ofs, SSEEK_SET);
	nr = ioswrite(d->disk, data->buffer, d->sectsize);
	if (nr != d->sectsize) {
		data->regs[0] |= 0x10; // CRC error
	}
	data->regs[0] &= ~0x02; // DRQ
}

static void fdd_post_write(struct FDD_DATA*data)
{
	struct FDD_DRIVE_DATA*dd = data->drives + data->cur_drive;
	switch (data->last_cmd >> 4) {
	case 0x0A: // write single sector
		fdd_write_sector(data, data->cur_drive);
		data->regs[2] = (data->regs[2] + 1) % dd->nsectors;
		data->regs[0] &= ~0x01; // clear busy flag
		break;
	case 0x0B: // write multiple sectors
		fdd_write_sector(data, data->cur_drive);
		data->regs[2] = (data->regs[2] + 1) % dd->nsectors;
		data->data_remains = dd->sectsize;
		break;
	}
	data->data_index = 0;
}

static void fdd_post_read(struct FDD_DATA*data)
{
	struct FDD_DRIVE_DATA*dd = data->drives + data->cur_drive;
	data->data_index = 0;
	switch (data->last_cmd >> 4) {
	case 0x08: // read single sector
		data->regs[0] &= ~0x01; // clear busy flag
		break;
	case 0x09: // read multiple sectors
		fdd_read_sector(data, data->cur_drive);
		data->regs[2] = (data->regs[2] + 1) % dd->nsectors;
		break;
	}
}

static void sound_phase()
{
	PlaySound(NULL, NULL, SND_NODEFAULT);
	PlaySound("teac.wav", GetModuleHandle(NULL), SND_RESOURCE | SND_ASYNC | SND_NOWAIT);
}

static void sound_loop(int cnt, int fast)
{
	if (!cnt) return;
	PlaySound(NULL, NULL, SND_NODEFAULT);
	for (; cnt; --cnt) {
		PlaySound("teac.wav", GetModuleHandle(NULL), SND_RESOURCE | SND_ASYNC | SND_NOSTOP);
		if (!fast) Sleep(10);
	}
}

static void fdd_command(struct FDD_DATA*data, byte cmd)
{
	int lt;
	int st = 10;
	int st_t = 100;
	struct FDD_DRIVE_DATA*dd = data->drives + data->cur_drive;
	data->regs[0] = 1; // busy & ready
	data->data_remains = 0;
	data->last_cmd = cmd;
	data->rotate_delay = 100;
//	Dprintf("fdd_command (%i): data_index = %i, data_remains = %i\n", cmd, data->data_index, data->data_remains);

	switch (cmd >> 4) {
	case 0x00: // recalibrate
		Dprintf("fdd: recalibrate\n");
		if (data->st->sr->gconfig->flags & EMUL_FLAGS_TEAC_SOUNDS)
			sound_loop(data->regs[1], data->use_fast);
		data->regs[1] = 0;
		if (dd->present) {
			data->regs[0] |= 4; // track 0
		} else {
			data->regs[0] |= 0x10; // seek error
		}
		if (!data->use_fast) st = 100;
		break;
	case 0x01: // seek cylinder
		if (data->st->sr->gconfig->flags & EMUL_FLAGS_TEAC_SOUNDS)
			sound_phase();
		if (!dd->present) {
			Dprintf("fdd: seek: no drive present!\n");
			data->regs[0] |= 0x10; // seek error
			data->last_cmd = 0;
			break;
		}
		if (data->data_index != 1) {
			Dprintf("fdd: seek: no cylinder number!\n");
			data->regs[0] |= 0x10; // seek fail
			data->last_cmd = 0;
			break;
		}
		Dprintf("fdd: seek from track %i to track %i\n", data->regs[1], data->buffer[0] & 0x7F);
		lt = data->regs[1];
		data->regs[1] = data->buffer[0] & 0x7F;
		if (data->regs[1] >= dd->ntracks) {
			data->regs[0] |= 0x10; // seek fail
			data->regs[1] = 39;
		}
		if (!data->regs[1]) {
			data->regs[0] |= 4; // track 0
		}
		{
			int nt = (lt > data->regs[1])?lt - data->regs[1]: data->regs[1] - lt;
                	if (data->st->sr->gconfig->flags & EMUL_FLAGS_TEAC_SOUNDS)
				sound_loop(nt, 1);
			if (!data->use_fast) st = (nt + 1) * st_t;
		}
		break;
	case 0x02: // step, no change
		if (data->st->sr->gconfig->flags & EMUL_FLAGS_TEAC_SOUNDS)
			sound_phase();
		if (!data->use_fast) st = st_t;
		Dprintf("fdd: step, no change\n");
		break;
	case 0x03: // step, change
		Dprintf("fdd: step, change\n");
		if (data->st->sr->gconfig->flags & EMUL_FLAGS_TEAC_SOUNDS)
			sound_phase();
		if (!data->use_fast) st = st_t;
		if (data->step_dir < 0) {
			if (data->regs[1] >= data->step_dir) data->regs[1] += data->step_dir;
		} else {
			if (data->regs[1] >= dd->ntracks) {
				data->regs[0] |= 0x10; // seek fail
				data->regs[1] = dd->ntracks - 1;
			}
		}
		data->regs[1] += data->step_dir;
		if (!data->regs[1]) {
			data->regs[0] |= 4; // track 0
		}
		break;
	case 0x04: // step forward, no change
		Dprintf("fdd: step forward, no change\n");
		data->step_dir = 1;
		if (data->st->sr->gconfig->flags & EMUL_FLAGS_TEAC_SOUNDS)
			sound_phase();
		if (!data->use_fast) st = st_t;
		break;
	case 0x05: // step forward, change
		Dprintf("fdd: step forward, change\n");
		if (data->st->sr->gconfig->flags & EMUL_FLAGS_TEAC_SOUNDS)
			sound_phase();
		if (!data->use_fast) st = st_t;
		data->step_dir = 1;
		data->regs[1] += data->step_dir;
		if (data->regs[1] >= dd->ntracks) {
			data->regs[0] |= 0x10; // seek fail
			data->regs[1] = dd->ntracks - 1;
		}
		if (!data->regs[1]) {
			data->regs[0] |= 4; // track 0
		}
		break;
	case 0x06: // step backward, no change
		Dprintf("fdd: step backward, no change\n");
		if (data->st->sr->gconfig->flags & EMUL_FLAGS_TEAC_SOUNDS)
			sound_phase();
		if (!data->use_fast) st = st_t;
		data->step_dir = -1;
		break;
	case 0x07: // step backward, change
		Dprintf("fdd: step backward, change\n");
		if (data->st->sr->gconfig->flags & EMUL_FLAGS_TEAC_SOUNDS)
			sound_phase();
		if (!data->use_fast) st = st_t;
		data->step_dir = -1;
		if (data->regs[1]) data->regs[1] += data->step_dir;
		else {
			Dprintf("fdd: seek: seek error!\n");
			data->regs[0] |= 0x10;
		}
		if (!data->regs[1]) {
			data->regs[0] |= 4; // track 0
		}
		break;
	case 0x08: // read single sector
	case 0x09: // read multiple sectors
		if (!dd->present) {
			Dprintf("fdd: read: no drive present!\n");
			data->regs[0] |= 0x80; // not ready
			data->last_cmd = 0;
			break;
		}
		Dprintf("fdd: reading sector %i\n", data->regs[2]);
		fdd_read_sector(data, data->cur_drive);
		data->regs[2] = (data->regs[2] + 1) % dd->nsectors;
		break;
	case 0x0A: // write single sector
	case 0x0B: // write multiple sectors
		if (!dd->present) {
			Dprintf("fdd: write: no drive present!\n");
			data->regs[0] |= 0x80; // not ready
			data->last_cmd = 0;
			break;
		}
		if (dd->readonly) {
			Dprintf("fdd: write: drive is readonly\n");
			data->regs[0] |= 0x40; // read only
			data->last_cmd = 0;
			break;
		}
		Dprintf("fdd: writing sector %i\n", data->regs[2]);
		data->data_remains = dd->sectsize;
		break;
	case 0x0C: // read address mark
		Dprintf("fdd: read address mark\n");
		if (!dd->present) {
			Dprintf("fdd: read_address: no drive present!\n");
			data->regs[0] |= 0x80; // not ready
			data->last_cmd = 0;
			break;
		}
		data->buffer[0] = data->regs[1];
		data->buffer[1] = (data->cur_head>=dd->nheads)?dd->nheads-1:data->cur_head; // side
		data->buffer[2] = dd->sectind; // type
		data->buffer[3] = data->regs[2]; // sector
		data->regs[2] =   data->regs[1]>=dd->ntracks?dd->ntracks-1:data->regs[1]; // track to sector field
		data->buffer[4] = 0x00; // cs
		data->buffer[5] = 0x00; // cs
		data->data_remains = 6;
		break;
	case 0x0D: // reset
		Dprintf("fdd: reset execution\n");
		data->regs[0] &= ~1;
		break;
	case 0x0E: // read track
		Dprintf("fdd: read track\n");
		break;
	case 0x0F: // write track
		Dprintf("fdd: write track\n");
		break;
	}
	data->data_index = 0; // data index
	data->status_delay = st;
}

static void fdd_set_track_num(struct FDD_DATA*data, byte d)
{
	data->regs[1] = d;
}

static void fdd_set_sector_num(struct FDD_DATA*data, byte d)
{
	data->regs[2] = d;
}

static void fdd_write_data(struct FDD_DATA*data, byte d)
{
	data->buffer[data->data_index] = d;
	if (data->data_remains) {
		-- data->data_remains;
		if (!data->data_remains) fdd_post_write(data);
	}
	data->data_index = (data->data_index + 1) % FDD_MAX_SECTOR_SIZE;
}

static void fdd_control(struct FDD_DATA*data, byte d)
{
	data->cur_drive = d & 1;
	data->cur_head = (d>>1) & 1;
//	Dprintf("selected drive %i head %i (%02X)\n", data->cur_drive, data->cur_head, d);
}


static byte fdd_get_status(struct FDD_DATA*data)
{
	if (!(data->last_cmd&0x80)) { // type I command, update index mark and head loaded
		data->regs[0] |= 0x20; // head loaded
		data->regs[0] |= data->regs[2]? 0x00: 0x02; // index mark
//		Dprintf("rotate_delay = %i, sector = %i\n", data->rotate_delay, data->regs[2]);
		if (data->rotate_delay) --data->rotate_delay;
		else {
			data->regs[2] = (data->regs[2] + 1) % SECT_PER_TRACK;
			data->rotate_delay = 100;
		}
	}
	if (data->status_delay) -- data->status_delay;
//	if (data->enabled) {
		if (!data->drives[data->cur_drive].present) 
			data->regs[0] |= 0x80;
		if (data->drives[data->cur_drive].readonly) 
			data->regs[0] |= 0x40;
//	}
	if (data->data_remains) data->regs[0] |= 0x02;
	if (!data->status_delay) data->regs[0] &= ~0x01; // clear busy
	return data->regs[0];
}


static byte fdd_get_track_num(struct FDD_DATA*data)
{
	return data->regs[1];
}

static byte fdd_get_sector_num(struct FDD_DATA*data)
{
	return data->regs[2];
}

static byte fdd_get_data(struct FDD_DATA*data)
{
	byte r;
	if (data->data_remains) -- data->data_remains;
	else return 0x00;
	r = data->buffer[data->data_index];
	if (!data->data_remains) {
		fdd_post_read(data);
		data->regs[0] &= ~0x01; // clear busy flag
		data->data_index = 0;
	} else {
		data->data_index = (data->data_index + 1) % FDD_MAX_SECTOR_SIZE;
	}
	return r;
}

static byte fdd_get_state(struct FDD_DATA*data)
{
	return data->data_remains?0x80: 0x00; // data ready
}


static void init_drive(struct FDD_DATA*data, struct SLOTCONFIG*cf, int no)
{
	struct FDD_DRIVE_DATA*dd = data->drives + no;
	char_t buf[32];
	const char_t*name;

	switch (cf->cfgint[CFG_INT_DRV_TYPE1 + no]) {
	case DRV_TYPE_1S1D:
		dd->ntracks = 40;
		dd->nheads = 1;
		break;
	case DRV_TYPE_1S2D:
		dd->ntracks = 80;
		dd->nheads = 1;
		break;
	case DRV_TYPE_2S1D:
		dd->ntracks = 40;
		dd->nheads = 2;
		break;
	case DRV_TYPE_2S2D:
		dd->ntracks = 80;
		dd->nheads = 2;
		break;
	default:
		dd->present = 0;
		return;
	}
	dd->nsectors = 10;
	dd->sectsize = 512;
	dd->sectind =  2;
	dd->present = 1;

	printf("Liberty Drive %i: Tracks %i, Heads %i, Sectors per Track: %i, Bytes per Sector: %i\n",
		no + 1, dd->ntracks, dd->nheads, dd->nsectors, dd->sectsize);

	sprintf(buf, "liberty%i", no + 1);
	name = sys_get_parameter(buf);
	if (!name) name = cf->cfgstr[CFG_STR_DRV_IMAGE1 + no];
	strcpy(dd->disk_name, name);
	open_fdd1(dd, name, cf->cfgint[CFG_INT_DRV_RO_FLAGS] & (1<<no), 0);
}


int  fddliberty_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*cf)
{
	struct FDD_DATA*data;
	ISTREAM*rom;

	printf("in fddliberty_init\n");
	data = calloc(1, sizeof(*data));
	if (!data) return -1;

	data->sr = sr;
	data->st = st;

	rom=isfopen(cf->cfgstr[CFG_STR_DRV_ROM]);
	if (!rom) {
		fprintf(stderr, "no FDD rom: %s", cf->cfgstr[CFG_STR_DRV_ROM]);
	} else {
		isread(rom, data->fdd_rom, sizeof(data->fdd_rom));
		isclose(rom);
	}

	data->fdd_rom[0x707] = 0x3C; // to make bootable

	fill_fdd1(data);
	init_drive(data, cf, 0);
	init_drive(data, cf, 1);
	data->initialized = 1;

	st->data = data;
	st->free = remove_fdd1;
	st->command = fdd1_command;
	st->save = fdd1_save;
	st->load = fdd1_load;

	data->rom_mode = 1;

	data->use_fast = cf->cfgint[CFG_INT_DRV_FAST];

	data->regs[0] |= 0x80;

	fill_rw_proc(st->baseio_sel, 1, fdd_io_read, fdd_io_write, data);
	fill_rw_proc(st->io_sel, 1, fdd_rom_r, empty_write, data);
	fill_rw_proc(&st->xio_sel, 1, fdd_xrom_r, empty_write, data);

	return 0;
}




static void fdd_io_write(unsigned short a,unsigned char d,struct FDD_DATA*data)
{
//	Dprintf("fdd_write(%X, %X)\n", a, d);
	a &= 15;
	switch (a) {
	case 0: // command
		fdd_command(data, d);
		break;
	case 1: // track
		fdd_set_track_num(data, d);
		break;
	case 2: // sector
		fdd_set_sector_num(data, d);
		break;
	case 3: // data
		fdd_write_data(data, d);
		break;
	case 8: // control
		fdd_control(data, d);
		break;
	}
}

static byte fdd_io_read(unsigned short a,struct FDD_DATA*data)
{
	byte r = 0x00;
	if (data->sr->in_debug) return 0xFF;
	if (!data->initialized) return r;
	a &= 15;
	switch (a) {
	case 0: // status
		r = fdd_get_status(data);
		break;
	case 1: // track
		r = fdd_get_track_num(data);
		break;
	case 2: // sector
		r = fdd_get_sector_num(data);
		break;
	case 3: // data
		r = fdd_get_data(data);
		break;
	case 4: // state
		r = fdd_get_state(data);
		break;
	}
//	Dprintf("fdd_read(%X) = %X\n", a, r);
	return r;
}



