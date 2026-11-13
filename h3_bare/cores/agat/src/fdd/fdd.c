/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	fdd - emulation of TEAC floppy drive
*/

#include "common.h"
#include "types.h"
#include "memory.h"
#include "streams.h"
#include "debug.h"
#include "sysconf.h"
#include "runmgr.h"
#include "runmgrint.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syslib.h"
#include "debug.h"

#include "localize.h"

#ifndef W_OK
#define W_OK 02
#endif


#define DEFAULT_DT 39

#define FDD_SECTOR_COUNT 21
#define FDD_TRACK_COUNT	160

#define FDD_ROM_SIZE 256

#define AIM_SYNC	0x01
#define AIM_EOT		0x02
#define AIM_SINDEX	0x03
#define AIM_EINDEX	0x13

struct FDD_STATE
{
	byte s;
	byte rk;
	byte c1;
	byte rd;
	byte c2;
};

#define FDD_PROLOG_SIZE 21
#define FDD_SECTOR_SIZE 261
#define FDD_SECTOR_DATA_SIZE	256
#define FDD_DATA_OFFSET 3
#define FDD_CS_OFFSET	259

#define FDD_PROLOG_VOLUME 15
#define FDD_PROLOG_TRACK  16
#define FDD_PROLOG_SECTOR 17

#define RAW_SECTOR_SIZE 0x11A
#define RAW_TRACK_SIZE (RAW_SECTOR_SIZE * (FDD_SECTOR_COUNT + 2))
#define RAW_TRACK_FILE_SIZE (RAW_SECTOR_SIZE * FDD_SECTOR_COUNT)
#define AIM_TRACK_SIZE 6464

static byte std_sig[]="Agathe emulator virtual disk\x0D\x0A\x1A""AD";

static const byte def_prolog[FDD_PROLOG_SIZE]={
	0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
	0xFF,0xAA,0xA4,0x95,0x6A,
	254,//254, //volume
	00, // track
	00, // sector
	0x5A, 0xAA, 0xAA
};

struct FDD_DRIVE_DATA
{
	byte sector_data[FDD_SECTOR_SIZE];
	byte prolog[FDD_PROLOG_SIZE];
	byte raw_track_data[RAW_TRACK_SIZE];
	word aim_track_data[AIM_TRACK_SIZE];
	int  use_prolog;
	int  disk_index;
	int  error;
	int  readonly;
	int  present;
	int  write_mode;
	byte disk_header[256];
	IOSTREAM *disk;

	int	rawfmt; // 1 if disk in raw (nibble) format, 2 if disk in aim format
	int  	raw_index;
	int	raw_dirty, raw_data;
	int	raw_track_ofs;
	int	no_mark;

	char disk_name[1024];
	int  start_ofs;
	HMENU submenu;
};

struct FDD_DATA
{
	struct SLOT_RUN_STATE*st;
	struct FDD_DRIVE_DATA	drives[2];
	struct FDD_STATE	state;
	int	drv;
	int	initialized;
	int	time, dt;
	int	type;
	byte	fdd_rom[FDD_ROM_SIZE];
	int	use_fast;
	int	sync, rd, write_sync;
};



static void rotate_sector(struct FDD_DATA*data);

static void fdd_io_write(unsigned short a,unsigned char d,struct FDD_DATA*data);
static byte fdd_io_read(unsigned short a,struct FDD_DATA*data);
static byte fdd_rom_read(unsigned short a,struct FDD_DATA*data);
static byte get_cs(byte*data,int n);




static void sound_phase()
{
	PlaySound(NULL, NULL, SND_NODEFAULT);
	PlaySound("teac.wav", GetModuleHandle(NULL), SND_RESOURCE | SND_ASYNC | SND_NOWAIT);
}

static void fill_fdd_drive(struct FDD_DRIVE_DATA*data)
{
	memcpy(data->prolog,def_prolog,sizeof(def_prolog));
	data->disk=0;
	data->sector_data[0]=0xAA;
	data->sector_data[1]=0x6A;
	data->sector_data[2]=0x95;
	data->sector_data[FDD_CS_OFFSET+1]=0x5A;
}

static void fill_fdd(struct FDD_DATA*data)
{
	fill_fdd_drive(data->drives);
	fill_fdd_drive(data->drives+1);
	data->initialized=0;
	data->time=0;
	data->type=0;
	data->drv=0;
}

int open_fdd(struct FDD_DRIVE_DATA*drv,const char_t*name,int ro, int no)
{
	if (drv->disk) {
		iosclose(drv->disk);
		drv->disk = NULL;
	}
	if (!name) return 1;
//	if (access(name,W_OK)) ro=1;
	drv->disk=ro?isfopen(name):iosfopen(name);
	if (!ro&&!drv->disk) {
		ro=1;
		drv->disk=isfopen(name);
	}
	if (!drv->disk) {
		ro = 1;
		drv->disk = isfopen_res(MAKEINTRESOURCE(31+no));
	}
	if (!drv->disk) {
		errprint(TEXT("can't open disk file %s"),name);
		return -1;
	}
	if (iosread(drv->disk,drv->disk_header,sizeof(drv->disk_header))!=sizeof(drv->disk_header)) {
		errprint(TEXT("can't read header from disk %s"),name);
		iosclose(drv->disk);
		drv->disk=0;
		return -1;
	}
	drv->readonly=ro;
	if (!memcmp(std_sig,drv->disk_header,sizeof(std_sig)-1)) {
		if (drv->disk_header[48]) drv->readonly=1;
		drv->start_ofs=256;
	} else drv->start_ofs=0;
	if (strstr(name,"nib") || strstr(name,"NIB")) {
		puts("using nibble format");
		drv->rawfmt = 1;
		drv->readonly = 1;
		drv->raw_index = 0;
	} else if (strstr(name,"aim") || strstr(name,"AIM")) {
		puts("using aim format");
		drv->rawfmt = 2;
//		drv->readonly = 1;
		drv->raw_index = 0;
	} else drv->rawfmt = 0;
	drv->prolog[FDD_PROLOG_SECTOR]=FDD_SECTOR_COUNT-1;
	drv->disk_index=250;
	drv->use_prolog=0;
	drv->error=0;
	drv->write_mode=0;
	drv->raw_dirty = drv->raw_data = 0;
	return 0;
}


static int remove_fdd(struct SLOT_RUN_STATE*sr)
{
	struct FDD_DATA*data = sr->data;
	open_fdd(data->drives+0, NULL, 0, 0);
	open_fdd(data->drives+1, NULL, 0, 1);
	return 0;
}


#define FDD_BASE_CMD 6000

static int init_menu(struct FDD_DRIVE_DATA*drv, int s, int d, HMENU menu)
{
	TCHAR buf[1024], buf1[1024];
	if (drv->submenu) DeleteMenu(menu, (UINT)drv->submenu, MF_BYCOMMAND);
	if (drv->present) {
		drv->submenu = CreatePopupMenu();
		AppendMenu(drv->submenu, MF_STRING, FDD_BASE_CMD + (s*4 + d*2 + 0), 
			localize_str(LOC_FDD, 2, buf1, sizeof(buf1)));
//			TEXT("Вставить диск..."));
		AppendMenu(drv->submenu, MF_STRING, FDD_BASE_CMD + (s*4 + d*2 + 1), 
			localize_str(LOC_FDD, 3, buf1, sizeof(buf1)));
//			TEXT("Извлечь диск"));

		// TEXT("Дисковод 800Кб (S%i,D%i)")
		wsprintf(buf, localize_str(LOC_FDD, 0, buf1, sizeof(buf1)), s, d + 1); 
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
		EnableMenuItem(drv->submenu, FDD_BASE_CMD + (s*4 + d*2 + 1), (drv->disk?MF_ENABLED:MF_GRAYED)|MF_BYCOMMAND);
	}
	return 0;
}

static int fdd_drvcmd(HWND wnd, struct FDD_DRIVE_DATA*drv, int cmd, int s, int d)
{
	if (drv->present) {
		if (cmd == FDD_BASE_CMD + (s*4 + d*2 + 0)) {
			if (select_disk(wnd, drv->disk_name, &drv->readonly)) {
				open_fdd(drv, drv->disk_name, drv->readonly, d);
			}
		} else if (cmd == FDD_BASE_CMD + (s*4 + d*2 + 1)) {
			open_fdd(drv, NULL, 0, d);
			drv->disk_name[0] = 0;
		}
	}
	return 0;
}

static int fdd_wincmd(HWND wnd, struct FDD_DATA*data, int cmd, int s)
{
	fdd_drvcmd(wnd, data->drives + 0, cmd, s, 0);
	fdd_drvcmd(wnd, data->drives + 1, cmd, s, 1);
	return 0;
}

static int fdd_command(struct SLOT_RUN_STATE*st, int cmd, int cdata, long param)
{
	struct FDD_DATA*data = st->data;
	HMENU menu;
	switch (cmd) {
	case SYS_COMMAND_RESET:
	case SYS_COMMAND_HRESET:
		if (data->state.rk&0x80 && data->use_fast) 
			system_command(st->sr, SYS_COMMAND_FAST, 0, 0);
		data->state.rk &= ~(0x80 | 0x40);
		break;
	case SYS_COMMAND_INITMENU:
		menu = (HMENU) param;
		init_menu(data->drives + 0, st->sc->slot_no, 0, menu);
		init_menu(data->drives + 1, st->sc->slot_no, 1, menu);
		break;
	case SYS_COMMAND_UPDMENU:
		menu = (HMENU) param;
		update_menu(data->drives + 0, st->sc->slot_no, 0, menu);
		update_menu(data->drives + 1, st->sc->slot_no, 1, menu);
		break;
	case SYS_COMMAND_FREEMENU:
		menu = (HMENU) param;
		free_menu(data->drives + 0, st->sc->slot_no, 0, menu);
		free_menu(data->drives + 1, st->sc->slot_no, 1, menu);
		break;
	case SYS_COMMAND_WINCMD:
		fdd_wincmd((HWND)param, data, cdata, st->sc->slot_no);
		break;
	}
	return 0;
}

static int fdd_save(struct SLOT_RUN_STATE*st, OSTREAM*out)
{
	struct FDD_DATA*data = st->data;

	WRITE_FIELD(out, data->drives[0].present);
	WRITE_FIELD(out, data->drives[0].readonly);
	WRITE_ARRAY(out, data->drives[0].disk_name);

	WRITE_FIELD(out, data->drives[1].present);
	WRITE_FIELD(out, data->drives[1].readonly);
	WRITE_ARRAY(out, data->drives[1].disk_name);
	return 0;
}

static int fdd_load(struct SLOT_RUN_STATE*st, ISTREAM*in)
{
	struct FDD_DATA*data = st->data;

	READ_FIELD(in, data->drives[0].present);
	READ_FIELD(in, data->drives[0].readonly);
	READ_ARRAY(in, data->drives[0].disk_name);

	READ_FIELD(in, data->drives[1].present);
	READ_FIELD(in, data->drives[1].readonly);
	READ_ARRAY(in, data->drives[1].disk_name);

	if (data->drives[0].present) {
		open_fdd(data->drives, data->drives[0].disk_name, data->drives[0].readonly & 1, 0);
	}

	if (data->drives[1].present) {
		open_fdd(data->drives + 1, data->drives[1].disk_name, data->drives[1].readonly & 1, 1);
	}
	data->initialized = 1;
	return 0;
}

static int conv_type(int t)
{
	switch (t) {
	case DRV_TYPE_1S1D:
	case DRV_TYPE_NONE:
		return 3;
	case DRV_TYPE_1S2D:
		return 1;
	case DRV_TYPE_2S1D:
		return 2;
	case DRV_TYPE_2S2D:
		return 0;
	}
	return 0;
}

int  fdd_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*cf)
{
	struct FDD_DATA*data;
	ISTREAM*rom;
	const char_t*name;
	char_t buf[32];

	data = calloc(1, sizeof(*data));
	if (!data) return -1;

	data->st = st;

	data->dt = DEFAULT_DT;

	rom = isfopen(cf->cfgstr[CFG_STR_DRV_ROM]);
	if (!rom) {
		load_buf_res(cf->cfgint[CFG_INT_DRV_ROM_RES], data->fdd_rom, FDD_ROM_SIZE);
	} else {
		iosread(rom, data->fdd_rom, sizeof(data->fdd_rom));
		iosclose(rom);
	}

	fill_fdd(data);


	if (cf->cfgint[CFG_INT_DRV_TYPE1] != DRV_TYPE_NONE) {
		data->drives[0].present = 1;
		sprintf(buf, "s%id1", st->sc->slot_no);
		name = sys_get_parameter(buf);
		if (!name) name = cf->cfgstr[CFG_STR_DRV_IMAGE1];
		strcpy(data->drives[0].disk_name, name);
		open_fdd(data->drives, name, cf->cfgint[CFG_INT_DRV_RO_FLAGS] & 1, 0);
	}
	if (cf->cfgint[CFG_INT_DRV_TYPE2] != DRV_TYPE_NONE) {
		data->drives[1].present = 1;
		sprintf(buf, "s%id2", st->sc->slot_no);
		name = sys_get_parameter(buf);
		if (!name) name = cf->cfgstr[CFG_STR_DRV_IMAGE2];
		strcpy(data->drives[1].disk_name, name);
		open_fdd(data->drives+1, name, cf->cfgint[CFG_INT_DRV_RO_FLAGS] & 2, 1);
	}

	data->type = (conv_type(cf->cfgint[CFG_INT_DRV_TYPE1]) << 2) | 
		conv_type(cf->cfgint[CFG_INT_DRV_TYPE2]);

	fill_read_proc(st->baseio_sel, 1, fdd_io_read, data);
	fill_write_proc(st->baseio_sel, 1, fdd_io_write, data);
	fill_read_proc(st->io_sel, 1, fdd_rom_read, data);
	fill_write_proc(st->io_sel, 1, empty_write, data);

	st->data = data;
	st->free = remove_fdd;
	st->command = fdd_command;
	st->load = fdd_load;
	st->save = fdd_save;

	data->use_fast = cf->cfgint[CFG_INT_DRV_FAST];

	return 0;
}


static void aim_update_regs(struct FDD_DATA*data, byte code)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	int rot = 0;
//	printf("aim_update regs %x\n", code);
	if (data->state.rk&0x10) {
		drv->prolog[FDD_PROLOG_TRACK]|=1;
	} else {
		drv->prolog[FDD_PROLOG_TRACK]&=~1;
	}
	{ // register S
		register byte x=((data->state.rk&0x80)^0x80)|data->type|(data->state.s&0x10);
		if (drv->prolog[FDD_PROLOG_TRACK]>1) x|=0x40;
		if (!drv->readonly) x|=0x20;
		if (drv->no_mark) {
			if (drv->raw_index < 0x40) x&=~0x10; else x|=0x10;
		}
		if (code == AIM_SINDEX) {
			x &= ~0x10;
			rot = 1;
		}
		if (code == AIM_EINDEX) {
			x |= 0x10;
			rot = 1;
		}
		if (drv->error) x &= 0x7F;
		data->state.s=x;
	}
	{ // register RD
		if (drv->error) data->state.rd &= ~0x80; // clear ready bit
		if (((code&0x7F) == AIM_SYNC)) {
			data->sync = 1;
//			data->state.rd &= ~0x40; // synchro
		}
	}
	if (rot) rotate_sector(data);
}

static void update_regs(struct FDD_DATA*data)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
//	puts("update regs");
	if (drv->rawfmt == 2) {
		aim_update_regs(data, drv->aim_track_data[drv->raw_index] >> 8);
		return;
	}
	if (data->state.rk&0x10) {
		drv->prolog[FDD_PROLOG_TRACK]|=1;
	} else {
		drv->prolog[FDD_PROLOG_TRACK]&=~1;
	}
	{ // register S
		register byte x=((data->state.rk&0x80)^0x80)|data->type;
		if (drv->prolog[FDD_PROLOG_TRACK]>1) x|=0x40;
		if (!drv->readonly) x|=0x20;
		if (drv->rawfmt) {
			if (drv->raw_index > 250 && drv->raw_index < RAW_TRACK_SIZE-50) x |= 0x10;
		} else {
			if (drv->use_prolog) {
				if (drv->prolog[FDD_PROLOG_SECTOR]>2) x|=0x10;
			} else {
				if ((drv->prolog[FDD_PROLOG_SECTOR]!=FDD_SECTOR_COUNT-1||drv->disk_index<FDD_SECTOR_SIZE-50)
					&&drv->prolog[FDD_PROLOG_SECTOR]>2) x|=0x10;
			}
		}
		if (drv->error) x &= 0x7F;
		data->state.s=x;
	}
	if (drv->error) data->state.rd &= ~0x80; // clear ready bit
	{ // register RD
//		register byte x=data->state.rd&0x94;
		if (drv->rawfmt) {
			int n;
			byte v = 0x40;
			n = drv->raw_index % RAW_SECTOR_SIZE;
//			if (n == 12 || n == 0x15) v = 0;
			if (n >= 0x03 && n < 15 && drv->raw_track_data[drv->raw_index + 1] == 0x95) {
				data->sync = 1;
				data->time += 2*data->dt;
			}
			if (n >= 16 && n < 0x17 && drv->raw_track_data[drv->raw_index + 1] == 0x6A) {
				data->sync = 1;
				data->time += 2*data->dt;
			}
//			x |= v;
		} else {
			if (drv->use_prolog) {
				if (drv->disk_index==12) {
					data->sync = 1;
					data->time += 2*data->dt;
				}	
			} else {
				if (drv->disk_index<=0) {
					data->sync = 1;
					data->time += 2*data->dt;
				}
			}
		}
//		data->state.rd=x;
	}
}


static byte get_cs(byte*data,int n)
{
	int cs=0, d=0;
	for (;n;n--,data++) {
		cs+=*data;
		cs+=d;
		if (cs&0x100) d=1; else d=0;
		cs&=0xFF;
	}
	return cs;
}

static void load_aim_track(struct FDD_DATA*data, struct FDD_DRIVE_DATA*drv)
{
	int t0 = drv->prolog[FDD_PROLOG_TRACK];
	if (!drv->disk) {
		drv->error=1;
		return;
	}
	if (data->state.rk&0x10) t0 |= 1; else t0 &=~ 1;
	drv->raw_track_ofs = drv->start_ofs+(t0 * AIM_TRACK_SIZE*2);
	iosseek(drv->disk,drv->raw_track_ofs,SSEEK_SET);
//	printf("reading aim track %i\n", t0);
	if (iosread(drv->disk,drv->aim_track_data,AIM_TRACK_SIZE*2)!=AIM_TRACK_SIZE*2) {
		errprint(TEXT("can't read aim track"));
		drv->error=1;
	}
//	printf("disk index = %i; state&0x10 = %x\n", drv->disk_index, data->state.s&0x10);
//	if (drv->disk_index > 0x140) data->state.s |= 0x10;
//	else data->state.s &= ~0x10;
	{
		int i;
		for (i = 0; i < AIM_TRACK_SIZE; ++i) {
			if ((drv->aim_track_data[i] & 0xEF00) == 0x0300) break;
		}
		if (i == AIM_TRACK_SIZE) {
			drv->no_mark = 1;
//			logprint(0,TEXT("aim: no index mark on track."));
		} else drv->no_mark = 0;
	}
	drv->raw_data = 1;
}

static void save_aim_track(struct FDD_DATA*data, struct FDD_DRIVE_DATA*drv)
{
	if (!drv->disk) {
		drv->error=1;
		return;
	}
	iosseek(drv->disk,drv->raw_track_ofs,SSEEK_SET);
//	logprint(0,TEXT("writing aim track"));
	if (ioswrite(drv->disk,drv->aim_track_data,AIM_TRACK_SIZE*2)!=AIM_TRACK_SIZE*2) {
		errprint(TEXT("can't write aim track"));
		drv->error=1;
	}
	drv->raw_dirty = 0;
}

static void save_track_fdd(struct FDD_DATA*data, struct FDD_DRIVE_DATA*drv)
{
	if (drv->readonly) return;
	if (!drv->disk) {
		drv->error=1;
		return;
	}
	switch (drv->rawfmt) {
	case 0: return;
	case 1: break;
	case 2: save_aim_track(data, drv); return;
	}
	iosseek(drv->disk,drv->raw_track_ofs,SSEEK_SET);
//	logprint(0,TEXT("writing raw track"));
	if (ioswrite(drv->disk,drv->raw_track_data,RAW_TRACK_FILE_SIZE)!=RAW_TRACK_FILE_SIZE) {
		errprint(TEXT("can't write raw track"));
		drv->error=1;
	}
	drv->raw_dirty = 0;
}



static void load_track(struct FDD_DATA*data, struct FDD_DRIVE_DATA*drv)
{
	int t0 = drv->prolog[FDD_PROLOG_TRACK];
	if (!drv->disk) {
		drv->error=1;
		return;
	}
	if (drv->raw_data && drv->raw_dirty) save_track_fdd(data, drv);
	switch (drv->rawfmt) {
	case 0: return;
	case 1: break;
	case 2: load_aim_track(data, drv); return;
	}
	if (data->state.rk&0x10) t0 |= 1; else t0 &=~ 1;
	drv->raw_track_ofs = drv->start_ofs+(t0 * RAW_TRACK_FILE_SIZE);
	iosseek(drv->disk,drv->raw_track_ofs,SSEEK_SET);
//	logprint(0,TEXT("reading raw track %i"), t0);
	if (iosread(drv->disk,drv->raw_track_data,RAW_TRACK_FILE_SIZE)!=RAW_TRACK_FILE_SIZE) {
		errprint(TEXT("can't read raw track"));
		drv->error=1;
	}
//	drv->raw_index = 0;
}



static void make_step(struct FDD_DATA*data,byte _b)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
//	printf("make_step\n");
	if (!drv->present) { // no step
		return;
	}	
	if (data->st->sr->gconfig->flags & EMUL_FLAGS_TEAC_SOUNDS)
		sound_phase();
	if (data->state.rk&4) {
		if (drv->prolog[FDD_PROLOG_TRACK]<FDD_TRACK_COUNT-2) {
			drv->prolog[FDD_PROLOG_TRACK]+=2;
			load_track(data, drv);
		} else return;
	} else {
		if (drv->prolog[FDD_PROLOG_TRACK]>=2) {
			drv->prolog[FDD_PROLOG_TRACK]-=2;
			load_track(data, drv);
		}
	}
	switch (drv->rawfmt) {
	case 0: break;
	case 1: 
	case 2: drv->raw_index = 0; data->state.s &= ~0x10; break;
	}
	drv->prolog[FDD_PROLOG_SECTOR]=FDD_SECTOR_COUNT-1;
	drv->disk_index=250;
	drv->use_prolog=0;
//	drv->prolog[FDD_PROLOG_TRACK]=0;
//	logprint(0,TEXT("----------- step"));
	update_regs(data);
}


static void prepare_to_write(struct FDD_DATA*data)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	data->write_sync = 0;
	if (drv->rawfmt) {
		update_regs(data);
		return;
	}
	drv->write_mode=0;
//	logprint(0,TEXT("prepare_to_write"));
	if (!drv->use_prolog||drv->disk_index>10) {
		drv->use_prolog=!drv->use_prolog;
	}
	if (drv->use_prolog) {
		drv->disk_index=13;
	} else {
		drv->disk_index=1;
	}
	update_regs(data);
}

// writesync
static void prepare_sector_to_write(struct FDD_DATA*data)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	if (drv->rawfmt) {
		return;
	}
	drv->write_mode=1;
//	logprint(0,TEXT("prepare_sector_to_write"));
	if (drv->use_prolog) {
		if (drv->disk_index < 13) drv->disk_index=13;
		else {
			drv->use_prolog = 0;
			drv->disk_index = 1;
		}
	} else drv->disk_index=1;
	update_regs(data);
}

int get_sector_num(int track,int sector)
{
	return track*FDD_SECTOR_COUNT+sector;
}

static void write_sector(struct FDD_DATA*data)
{
	int ls;
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	if (drv->readonly) return;
	if (!drv->disk) {
		drv->error=1;
		return;
	}
	if (drv->sector_data[FDD_CS_OFFSET]!=get_cs(drv->sector_data+FDD_DATA_OFFSET,FDD_SECTOR_DATA_SIZE)) {
		errprint(TEXT("invalid checksum: sector ignored"));
		return;
	}
	ls=get_sector_num(drv->prolog[FDD_PROLOG_TRACK],
		drv->prolog[FDD_PROLOG_SECTOR]);
	iosseek(drv->disk,drv->start_ofs+(ls<<8),SSEEK_SET);
/*	logprint(0,TEXT("write: (%i,%i): %x %x: %02X %02X %02X"),
		drv->prolog[FDD_PROLOG_TRACK],drv->prolog[FDD_PROLOG_SECTOR],ls,ls<<8,
		drv->sector_data[FDD_DATA_OFFSET], drv->sector_data[FDD_DATA_OFFSET + 1], drv->sector_data[FDD_DATA_OFFSET + 2]);
*/	if (ioswrite(drv->disk,drv->sector_data+FDD_DATA_OFFSET,FDD_SECTOR_DATA_SIZE)!=FDD_SECTOR_DATA_SIZE) {
		errprint(TEXT("can't write sector"));
		drv->error=1;
	}
}

static int locate_raw_start(struct FDD_DATA*data, struct FDD_DRIVE_DATA*drv)
{
	int i, na = 0;
	for (i = 0; i < RAW_TRACK_SIZE; ++i) {
		if (drv->raw_track_data[i] == 0xAA) {
			++na;
		} else {
			if (na > 200) {
				int r = i - 12;
				if (r < 0) r += RAW_TRACK_SIZE;
				return r;
			}
		}
	}
	return 0;
}

static void prepare_sector_to_read(struct FDD_DATA*data)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	if (drv->rawfmt) {
		return;
	}
	drv->use_prolog=1;
	drv->disk_index=0;
	if (!drv->disk) {
		drv->error=1;
	} else {
		int ls=get_sector_num(drv->prolog[FDD_PROLOG_TRACK],
			drv->prolog[FDD_PROLOG_SECTOR]);
		iosseek(drv->disk,drv->start_ofs+(ls<<8),SSEEK_SET);
//		printf("read: (%i,%i): %x %x\n",drv->prolog[FDD_PROLOG_TRACK],drv->prolog[FDD_PROLOG_SECTOR],ls,ls<<8);
		if (iosread(drv->disk,drv->sector_data+FDD_DATA_OFFSET,FDD_SECTOR_DATA_SIZE)!=FDD_SECTOR_DATA_SIZE) {
			errprint(TEXT("can't read sector"));
			drv->error=1;
		}
		drv->sector_data[FDD_CS_OFFSET]=get_cs(drv->sector_data+FDD_DATA_OFFSET,FDD_SECTOR_DATA_SIZE);
	}
}

static void rotate_sector(struct FDD_DATA*data)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	if ((data->state.rk&0x40)&&(drv->write_mode&&!drv->rawfmt)) return;
//	puts("rotate sector");
	switch (drv->rawfmt) {
	case 1:
		++ drv->raw_index;
		if (drv->raw_index == RAW_TRACK_SIZE) drv->raw_index = 0;
//		printf("rotate_sector: raw_index: %i; d = %02X\n", drv->raw_index, drv->raw_track_data[drv->raw_index]);
		goto fin;
	case 2:
		if (!drv->raw_data) load_track(data, drv);
		if (data->state.rk&0x40) {
			if (data->write_sync) {
				drv->aim_track_data[drv->raw_index] |= 0x0100; // | 0x8000
				drv->raw_dirty = 1;
			} else {
				if ((drv->aim_track_data[drv->raw_index] & ~0x80FF) == 0x100) {
//					puts("clear 1");
					drv->aim_track_data[drv->raw_index] &= 0xFF;
					drv->raw_dirty = 1;
				} else if (drv->aim_track_data[drv->raw_index] & ~0xFF) {
//					puts("clear 2");
					drv->aim_track_data[drv->raw_index] &= 0x7FFF;
					drv->raw_dirty = 1;
				}
//				printf("drv->aim_track_data[drv->raw_index] = %04X\n", drv->aim_track_data[drv->raw_index]);
			}	
		}
		++ drv->raw_index;
		if (drv->raw_index == AIM_TRACK_SIZE || (drv->aim_track_data[drv->raw_index] & 0xFF00) == 0x0200) {
			drv->raw_index = 0;
		}
//		printf("rotate_sector: raw_index: %i; d = %02X\n", drv->raw_index, drv->aim_track_data[drv->raw_index]);
		goto fin;
	}
	drv->disk_index++;
	if (drv->use_prolog) {
		if (drv->disk_index>=FDD_PROLOG_SIZE) {
			drv->use_prolog=0;
			drv->disk_index=0;
		}
	} else {
		if (drv->disk_index>=FDD_SECTOR_SIZE) {
			drv->prolog[FDD_PROLOG_SECTOR]++;
			if (drv->prolog[FDD_PROLOG_SECTOR]>=FDD_SECTOR_COUNT) {
				drv->prolog[FDD_PROLOG_SECTOR]=0;
			}
			drv->use_prolog=1;
			drv->disk_index=0;
			if (!(data->state.rk&0x40)) prepare_sector_to_read(data);
		}
	}
fin:
	data->write_sync = 0;
	update_regs(data);
}


static byte fdd_read_s(struct FDD_DATA*data)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	byte s = data->state.s;
	if (!drv->present) {
		s |= 0x70;
	}
	return s;
}


static byte fdd_read_rd(struct FDD_DATA*data)
{
	byte d = data->state.rd;
//	fprintf(stderr, ">>> read_rd: %02X\n", d);
/*	if (data->state.rk & 0x40) { // write
		if (!(d & 0x10)) d &= ~0x80;
	} else {
		if (!(d & 0x04)) d &= ~0x80;
	}*/
	return d;
}


static void fdd_write_rd(struct FDD_DATA*data,byte d)
{
	data->state.rd=d;
}

static byte fdd_read_rk(struct FDD_DATA*data)
{
	return data->state.rk;
}

static byte fdd_read_data(struct FDD_DATA*data)
{
	byte d;
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	if (!data->initialized) return 0;
	if (!(data->state.rd & 0x80)) return 0;
	if (!drv->present) {
		data->state.rd &= ~0x80; // clear ready bit
		return rand()&0xFF;
	}	
	switch (drv->rawfmt) {
	case 0:
		if (drv->use_prolog) d=drv->prolog[drv->disk_index];
		else d=drv->sector_data[drv->disk_index];
//		logprint(0, TEXT("fdd_read_data: use_prolog = %i, index = %i, data = %02x"),
//			drv->use_prolog, drv->disk_index, d);
		break;
	case 1:
		d = drv->raw_track_data[drv->raw_index];
		break;
	case 2:
		if (!drv->raw_data) load_track(data, drv);
//		logprint(0, TEXT("fdd_read_data: raw_index= %-4i, mode = %02X, data = %02x"),
//			drv->raw_index, drv->aim_track_data[drv->raw_index]>>8, drv->aim_track_data[drv->raw_index]&0xFF);
		d = (byte)drv->aim_track_data[drv->raw_index];
		break;
	}
	data->state.rd &= ~0x80; // clear ready bit
	data->time += data->dt/2;
//	printf("fdd_read_data: raw_index: %i; d = %02X\n", drv->raw_index, d);
//	system_command(data->st->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
/*	if (_access("saves/mem3.bin",0)) dump_mem(data->st->sr, 0, 0x10000, "saves/mem3.bin");
	{
		extern int cpu_debug;
		cpu_debug = 1;
	}*/
	return d;
}

static void fdd_write_data(struct FDD_DATA*data,byte d)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
//	fprintf(stderr, ">>> write_data: %02X\n", d);
	if ((!data->initialized) || drv->readonly) goto fin;
	if (!(data->state.rk&0x40)) goto fin;
//	printf("write_data: mode = %i; prolog=%i; index = %i; data = %02X\n",
//		drv->write_mode, drv->use_prolog, drv->disk_index, d);
	switch (drv->rawfmt) {
	case 0:
		if (!drv->use_prolog && !drv->write_mode && !drv->disk_index) {
			if (d == 0xA4) drv->write_mode = 1;
		}
		if (!drv->use_prolog && drv->write_mode && drv->disk_index == 1) {
			if (d == 0x95) { drv->use_prolog = 1; drv->disk_index = 13; }
		}
		if (!drv->write_mode) return;
		if (drv->use_prolog) {
			drv->prolog[drv->disk_index]=d;
		} else {
			drv->sector_data[drv->disk_index]=d;
		}
		drv->disk_index++;
		if (drv->use_prolog) {
			if (drv->disk_index>=FDD_PROLOG_SIZE-1) {
				drv->write_mode=0;
				drv->use_prolog=0;
				drv->disk_index=0;
		}
		} else {
			if (drv->disk_index>=FDD_SECTOR_SIZE) {
				write_sector(data);
				drv->write_mode=0;
				drv->use_prolog=1;
				drv->disk_index=0;
				drv->prolog[FDD_PROLOG_SECTOR]++;
				if (drv->prolog[FDD_PROLOG_SECTOR]==FDD_SECTOR_COUNT)
					drv->prolog[FDD_PROLOG_SECTOR]=0;
			}
		}
		update_regs(data);
		break;
	case 1:
//		printf("nib write[%i]: %x (%x)\n", drv->raw_index, d, drv->raw_track_data[drv->raw_index]);
		drv->raw_track_data[drv->raw_index] = d;
		drv->raw_dirty = 1;
		update_regs(data);
		break;
	case 2:
//		logprint(0, TEXT("aim write[%i]: %02x (%02x)"), drv->raw_index, d, drv->aim_track_data[drv->raw_index] & 0xFF);
		drv->aim_track_data[drv->raw_index] &= ~0xFF;
		drv->aim_track_data[drv->raw_index] |= d;
		drv->raw_dirty = 1;
		update_regs(data);
		break;
	}
fin:
	data->state.rd &= ~0x80; // clear ready bit
	data->time += 4*data->dt;
}


static byte shift_data(byte shift,byte data)
{
	byte b, b1;
//	if (shift&0x80) return data;
	b=shift>>1;
	b1=1<<b;
	if (shift&1) return data|b1;
	else return data&~b1;
}

static void fdd_write_rk(struct FDD_DATA*data,byte rk)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	byte b=data->state.rk^rk;
	data->state.rk=rk;
	if (b&8) { // drive was changed
		if (drv->raw_dirty) save_track_fdd(data, drv);
		if (rk&8) data->drv=1;
		else data->drv=0;
		drv=data->drives+data->drv;
//		printf(">>> drive %i\n", data->drv);
		load_track(data, drv);
		prepare_sector_to_read(data);
		update_regs(data);
	}
	if (b&0x10) { // head was changed
		if (drv->raw_dirty) save_track_fdd(data, drv);
		if (rk&0x10) drv->prolog[FDD_PROLOG_TRACK]|=1;
		else drv->prolog[FDD_PROLOG_TRACK]&=~1;
		b|=0x40; // force read
		drv->raw_data = 0;
		load_track(data, drv);
	}
	if (b&0x40) { // read/write
		if (rk&0x40) {
			prepare_to_write(data);
		} else { // read
			extern int cpu_debug;
	//		cpu_debug = 1;
//			if (_access("saves/mem4.bin",0)) dump_mem(data->st->sr, 0, 0x10000, "saves/mem4.bin");
			prepare_sector_to_read(data);
		}
	}
	if (b&0x80) {
		if (rk&0x80) {
//			printf(">>> enable\n");
		} else {
//			printf(">>> disable\n");
			if (drv->raw_dirty) save_track_fdd(data, drv);
		}	
	}
	if (data->use_fast && (b&0x80)) { // enable
		extern int cpu_debug;
		if (rk&0x80) {
//			cpu_debug = 0;
			system_command(data->st->sr, SYS_COMMAND_FAST, 1, 0);
		} else {
//			cpu_debug = 1;
			system_command(data->st->sr, SYS_COMMAND_FAST, 0, 0);
		}
	}
	update_regs(data);
}


void show_info(struct FDD_DATA*data)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	if (!drv->rawfmt) {
		printf("init=%i, drv=%i, prolog=%i, index=%i, track=%i, sector=%i\n",data->initialized,data->drv,
			drv->use_prolog, drv->disk_index,
			drv->prolog[FDD_PROLOG_TRACK],
			drv->prolog[FDD_PROLOG_SECTOR]);
	} else {
		printf("init=%i, drv=%i, raw_index=%i, track=%i\n",data->initialized,data->drv,
			drv->raw_index,
			drv->prolog[FDD_PROLOG_TRACK]);
	}
	printf("s=%02x rk=%02x c1=%02x rd=%02x c2=%02x\n",
		data->state.s,
		data->state.rk,
		data->state.c1,
		data->state.rd,
		data->state.c2);
}


static void fdd_prepare_run(struct FDD_DATA*data)
{
	int d=data->drv;
	data->initialized=1;
	data->drv=0;
	data->state.rd=0x5F;
	data->state.s=0x10;
	data->sync = 0;
	data->rd = 0;
	fdd_write_rk(data,0);
	prepare_sector_to_read(data);
	update_regs(data);
//	logprint(0,TEXT("prepare_run"));
//	show_info(data);
}

static void fdd_write_c1(struct FDD_DATA*data,byte d)
{
	data->state.c1=d;
	if (data->state.c1==0x92&&data->state.c2==0xBD) {
		fdd_prepare_run(data);
		return;
	}
	if  (data->state.c1==0x9B) {
		data->initialized=0;
		return;
	}
	if (data->state.c1&0x80) return;
	fdd_write_rk(data,shift_data(d,data->state.rk));
}


static void fdd_write_c2(struct FDD_DATA*data,byte d)
{
	data->state.c2=d;
	if (data->state.c1==0x92&&data->state.c2==0xBD) {
		fdd_prepare_run(data);
		return;
	}
	if (data->state.c2&0x80) return;
	data->state.rd=shift_data(d,data->state.rd);
}

static void fdd_nowrite(struct FDD_DATA*data,byte d)
{
}

static byte fdd_noread(struct FDD_DATA*data)
{
	return 0xFF;
}

static byte fdd_read10(struct FDD_DATA*data)
{
	return 0x10;
}


static void fdd_write_syncro(struct FDD_DATA*data,byte d)
{
//	printf(">>> write synchro\n");
	if (data->state.rk&0x40) {
//		logprint(0,TEXT("write_syncro"));
		prepare_sector_to_write(data);
		data->write_sync = 1;
		data->time += data->dt;
	}
}

static void fdd_clear_syncro(struct FDD_DATA*data, byte d)
{
//	logprint(0, TEXT("clear sync"));
//	fprintf(stderr, ">>clear sync\n");
	data->sync = 0;
	data->state.rd |= 0x40;
	data->time += 100;
}


static struct {
	byte (*read)(struct FDD_DATA*d);
	void (*write)(struct FDD_DATA*d,byte b);
} io_access[0x10]={
	{fdd_read10,fdd_nowrite}, //0
	{fdd_read_s,fdd_nowrite}, //1
	{fdd_read_rk,fdd_write_rk},//2
	{fdd_noread,fdd_write_c1},//3
	{fdd_read_data,fdd_nowrite},//4
	{fdd_noread,fdd_write_data},//5
	{fdd_read_rd,fdd_nowrite},//6
	{fdd_read_rd,fdd_write_c2},//7
	{fdd_noread,fdd_write_syncro},//8
	{fdd_noread,make_step},//9
	{fdd_noread,fdd_clear_syncro},//a
	{fdd_noread,fdd_nowrite},//b
	{fdd_noread,fdd_nowrite},//c
	{fdd_noread,fdd_nowrite},//d
	{fdd_noread,fdd_nowrite},//e
	{fdd_noread,fdd_nowrite}//f
};

void fdd_access(struct FDD_DATA*data)
{
	struct FDD_DRIVE_DATA*drv=data->drives+data->drv;
	int t0 =  data->dt;//75;//92;
	int t = cpu_get_tsc(data->st->sr), dt;
	if (!data->time || data->time > t) data->time = t;
//	if (data->state.rd&0x80) data->time -= 8;
	dt = t - data->time;
	if (dt > t0 * 1000) dt = t0 * 1000;
//	else if (dt > 2 * t0 && dt < t0 * 4) { dt -= t0; }
	if (!(data->state.rk & 0x80)) { data->time = t; return; }
//	printf("dt = %i\n", dt);
 	data->time = t - dt;
 	if (dt >= t0) {
//		if (dt > 300) t0 = 30;
		while (data->time < t - t0) { 
//			printf(">>rotate\n");
			rotate_sector(data);
			data->time += t0;
		}
		if (data->sync) {
//			printf(">>sync\n");
			data->state.rd &= ~0x40;
		}
		data->sync = 0;
		data->state.rd |= 0x80;
		if (!drv->present || !drv->disk) {
			data->sync = 1;
		}
		data->rd = 0;
	}

}

void fdd_io_write(unsigned short a,unsigned char d, struct FDD_DATA*data)
{
	a&=0x0F;
	if (a == 10) data->time += 2*data->dt;
	fdd_access(data);
	io_access[a].write(data,d);
//	if (data->state.rk&0x40) {
//		printf("W(%01x)=%02x\n",a,d);
//		show_info(data);
//	}
}

byte fdd_io_read(unsigned short a, struct FDD_DATA*data)
{
	byte r;
	a&=0x0F;
	fdd_access(data);
	if (!data->initialized) return 0;
	r=io_access[a].read(data);
/*	if (data->state.rk&0x40) {
		logprint(0, TEXT("R(%01x)=%02x "),a,r);
//		show_info(data);
	}*/
//	logprint(0, TEXT("R(%01x)=%02x "),a,r);
//	show_info(data);
//	system_command(data->st->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
	return r;
}

byte fdd_rom_read(unsigned short a, struct FDD_DATA*data)
{
	return data->fdd_rom[a & (FDD_ROM_SIZE - 1)];
}

