/*
	Agat Emulator
	Copyright (c) NOP, nnop@newmail.ru
	fddaa - emulation of Acorn atom floppy controller
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

#define FDD_TRACK_COUNT 40
#define FDD_SECTOR_COUNT 10
#define FDD_SECTOR_DATA_SIZE 256

#define FDD_ROM_SIZE 0x1000
#define FDD_RAM_SIZE 0xC00

#define MAX_PARAMS	5
#define MAX_RESULTS	32768
#define FDD_N_REGS	256


#define MAX_DRIVES	4

#define FDD_TIMER_DELAY 50

struct FDD_DRIVE_DATA
{
	int	error;
	int	readonly;
	int	present;
	int	track, changed;

	HMENU	submenu;

	char_t	disk_name[1024];
	IOSTREAM *disk;

	byte	regs[FDD_N_REGS];
};

struct FDD_DATA
{
	struct FDD_DRIVE_DATA	drives[MAX_DRIVES];
	int	initialized;
	byte	fdd_rom[FDD_ROM_SIZE];
	byte	fdd_ram[FDD_RAM_SIZE];
	struct SYS_RUN_STATE	*sr;
	struct SLOT_RUN_STATE	*st;
	int	in_reset;
	byte	status, result, cmd, cmd_id;
	byte	params[MAX_PARAMS];
	byte	results[MAX_RESULTS];
	int	n_params, param_ind;
	int	n_results, result_ind;
};


#define STATUS_NON_DMA	0x04
#define STATUS_IRQ	0x08
#define STATUS_RES_FULL	0x10
#define STATUS_PAR_FULL	0x20
#define STATUS_CMD_FULL	0x40
#define STATUS_CMD_BUSY	0x80

#define RESULT_COMPL_MASK	0x1E
#define RESULT_COMPL_OK		0x00
#define RESULT_COMPL_SCAN_EQ	0x02
#define RESULT_COMPL_SCAN_NEQ	0x04
#define RESULT_COMPL_CLOCK_ERR	0x08
#define RESULT_COMPL_LATE_DMA	0x0A
#define RESULT_COMPL_IC_CRC	0x0C
#define RESULT_COMPL_DATA_CRC	0x0E
#define RESULT_COMPL_NOT_READY	0x10
#define RESULT_COMPL_WR_PROT	0x12
#define RESULT_COMPL_NO_T0	0x14
#define RESULT_COMPL_WR_FAULT	0x16
#define RESULT_COMPL_NO_SEC	0x18

#define RESULT_DELETED_DATA	0x20

struct FDD_CMD_INFO
{
	int  cmd;
	int  n_params;
	void (*op)(struct FDD_DATA*d);
};

static void fdd_start(struct FDD_DATA*d);
static void fdd_stop(struct FDD_DATA*d);
static void fdd_int(struct FDD_DATA*d);



static void fdd_cmd_write_data_128(struct FDD_DATA*d)
{
}

static void fdd_cmd_write_del_data_128(struct FDD_DATA*d)
{
}

static void fdd_cmd_read_data_128(struct FDD_DATA*d)
{
}

static void fdd_cmd_read_del_data_128(struct FDD_DATA*d)
{
}

static void fdd_cmd_verify_data_128(struct FDD_DATA*d)
{
}

static void fdd_cmd_scan_data(struct FDD_DATA*d)
{
}

static void fdd_cmd_scan_del_data(struct FDD_DATA*d)
{
}

static void fdd_cmd_write_data_finish(struct FDD_DATA*d)
{
	int sel = (d->cmd>>6) & 3;
	struct FDD_DRIVE_DATA *drv= d->drives + sel;
	if (drv->present && drv->disk) {
		int trk, sec, nsec, lid, nb, wr;
		d->drives[sel].track = trk = d->params[0];
		sec = d->params[1];
		nsec = d->params[2] & 0x1F;
		lid = d->params[2] >> 5;
		nb = 128 << lid;
		printf("finish write[%i] track %i sec %i nsec %i len %i\n", sel, trk, sec, nsec, nb);
		iosseek(drv->disk, (trk * FDD_SECTOR_COUNT + sec) * nb, SSEEK_SET);
		wr = ioswrite(drv->disk, d->results, d->n_results);
		if (wr != d->n_results) {
			d->result = RESULT_COMPL_WR_FAULT;
		}
	} else {
		d->result = RESULT_COMPL_NOT_READY;
	}

}

static void fdd_cmd_write_data(struct FDD_DATA*d)
{
	int sel = (d->cmd>>6) & 3;
	struct FDD_DRIVE_DATA *drv= d->drives + sel;
	if (d->result_ind) {
		fdd_cmd_write_data_finish(d);
		return;
	}
	if (drv->present && drv->disk) {
		int trk, sec, nsec, lid, nb;
		extern int cpu_debug;
		d->drives[sel].track = trk = d->params[0];
		sec = d->params[1];
		nsec = d->params[2] & 0x1F;
		lid = d->params[2] >> 5;
		nb = 128 << lid;
		printf("write[%i] track %i sec %i nsec %i len %i\n", sel, trk, sec, nsec, nb);
//		system_command(d->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
//		cpu_debug = 1;
		if (trk > FDD_TRACK_COUNT || sec > FDD_SECTOR_COUNT || nb != FDD_SECTOR_DATA_SIZE) {
			d->status = STATUS_RES_FULL;
			d->result = RESULT_COMPL_NO_SEC;
			fdd_int(d);
			return;
		}
		if (drv->readonly) {
			d->status = STATUS_RES_FULL;
			d->result = RESULT_COMPL_WR_PROT;
			printf("disk is write protected!\n");
			fdd_int(d);
			return;
		}
		d->n_results = nb * nsec;

		if (d->n_results > sizeof(d->results)) {
			d->n_results = sizeof(d->results);
		}
		d->status |= STATUS_NON_DMA;
	} else {
		d->status = STATUS_RES_FULL;
		d->result = RESULT_COMPL_NOT_READY;
		fdd_int(d);
	}
}

static void fdd_cmd_write_del_data(struct FDD_DATA*d)
{
}

static void fdd_cmd_read_data(struct FDD_DATA*d)
{
	int sel = (d->cmd>>6) & 3;
	struct FDD_DRIVE_DATA *drv= d->drives + sel;
	if (d->result_ind) {
		puts("fdd_read finished");
		return; // no action after read
	}
	if (drv->present && drv->disk) {
		int trk, sec, nsec, lid, nb, rd;
		extern int cpu_debug;
		d->drives[sel].track = trk = d->params[0];
		sec = d->params[1];
		nsec = d->params[2] & 0x1F;
		lid = d->params[2] >> 5;
		nb = 128 << lid;
		printf("read[%i] track %i sec %i nsec %i len %i\n", sel, trk, sec, nsec, nb);
//		system_command(d->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
//		cpu_debug = 1;
		if (trk > FDD_TRACK_COUNT || sec > FDD_SECTOR_COUNT || nb != FDD_SECTOR_DATA_SIZE) {
			d->status = STATUS_RES_FULL;
			d->result = RESULT_COMPL_NO_SEC;
			fdd_int(d);
			return;
		}
		iosseek(drv->disk, (trk * FDD_SECTOR_COUNT + sec) * nb, SSEEK_SET);
		d->n_results = nb * nsec;

		if (d->n_results > sizeof(d->results)) {
			d->n_results = sizeof(d->results);
		}

		rd = iosread(drv->disk, d->results, d->n_results);
		if (rd != d->n_results) {
			d->n_results = 0;
			d->status = STATUS_RES_FULL;
			d->result = RESULT_COMPL_DATA_CRC;
			fdd_int(d);
			return;
		}

		d->status |= STATUS_NON_DMA;
	} else {
		d->status = STATUS_RES_FULL;
		d->result = RESULT_COMPL_NOT_READY;
		fdd_int(d);
	}
}

static void fdd_cmd_read_del_data(struct FDD_DATA*d)
{
}

static void fdd_cmd_verify_data(struct FDD_DATA*d)
{
}

static void fdd_cmd_readid(struct FDD_DATA*d)
{
}

static void fdd_cmd_format(struct FDD_DATA*d)
{
}

static void fdd_cmd_seek(struct FDD_DATA*d)
{
	int sel = (d->cmd>>6) & 3;
	if (d->drives[sel].present) {
		d->drives[sel].track = d->params[0];
		printf("seek[%i] to track %i\n", sel, d->drives[sel].track);
		fdd_int(d);
		d->status = STATUS_RES_FULL;
		d->result = RESULT_COMPL_OK;
	}
}

static void fdd_cmd_read_status(struct FDD_DATA*d)
{
	int sel = (d->cmd>>6) & 3;
	struct FDD_DRIVE_DATA *drv= d->drives + sel;
	byte r = 0;
	if (drv->present) {
		static int ind = 0;
		if (drv->disk) {
			r |= 0x40; // ready
			if (!drv->changed) r |= 0x04;
			drv->changed = 0;

		}
		if (drv->readonly) {
			r |= 0x08; // readonly
		}
		if (!drv->track) {
			r |= 0x02; // track 0
		}
		ind = !ind;
		if (ind) {
			r |= 0x10; // index
		}
	}
//	printf("fdd_read_result[%i] = %02X\n", sel, r);
	d->result = r;
}

static void fdd_cmd_specify(struct FDD_DATA*d)
{
}

static void fdd_cmd_r_reg(struct FDD_DATA*d)
{
	int sel = (d->cmd>>6) & 3;
	struct FDD_DRIVE_DATA *drv= d->drives + sel;
	d->result = drv->regs[d->params[0]];
}

static void fdd_cmd_w_reg(struct FDD_DATA*d)
{
	int sel = (d->cmd>>6) & 3;
	struct FDD_DRIVE_DATA *drv= d->drives + sel;
	drv->regs[d->params[0]] = d->params[1];
	d->result = RESULT_COMPL_OK;
}












static struct FDD_CMD_INFO cmds[] = {
	{0x0A, 2, fdd_cmd_write_data_128},
	{0x0E, 2, fdd_cmd_write_del_data_128},
	{0x12, 2, fdd_cmd_read_data_128},
	{0x16, 2, fdd_cmd_read_del_data_128},
	{0x1E, 2, fdd_cmd_verify_data_128},
	{0x00, 3, fdd_cmd_scan_data},
	{0x04, 3, fdd_cmd_scan_del_data},
	{0x0B, 3, fdd_cmd_write_data},
	{0x0F, 3, fdd_cmd_write_del_data},
	{0x13, 3, fdd_cmd_read_data},
	{0x17, 3, fdd_cmd_read_del_data},
	{0x1F, 3, fdd_cmd_verify_data},
	{0x1B, 3, fdd_cmd_readid},
	{0x23, 5, fdd_cmd_format},
	{0x29, 1, fdd_cmd_seek},
	{0x2C, 0, fdd_cmd_read_status},
	{0x35, 4, fdd_cmd_specify},
	{0x3D, 1, fdd_cmd_r_reg},
	{0x3A, 2, fdd_cmd_w_reg}
};



static void fdd_io_write(unsigned short a,unsigned char d, struct FDD_DATA*xd);
static byte fdd_io_read(unsigned short a, struct FDD_DATA*xd);
static void fdd_ram_write(unsigned short a,unsigned char d, struct FDD_DATA*xd);
static byte fdd_ram_read(unsigned short a, struct FDD_DATA*xd);
static byte fdd_rom_read(unsigned short a, struct FDD_DATA*xd);
static byte get_cs(byte*data,int n);


static void fill_fdd_drive(struct FDD_DRIVE_DATA*data)
{
	data->disk=0;
	memset(data->regs, 0, sizeof(data->regs));
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
//		puts("set ro to 1");
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
//	printf("ro: %i\n", ro);
	drv->readonly=ro;
//	printf("readonly: %i\n", drv->readonly);
	drv->error = 0;
	return 0;
}


static int remove_fdd1(struct SLOT_RUN_STATE*sr)
{
	struct FDD_DATA*data = sr->data;
	int i;
	fdd_stop(data);
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

		//TEXT("Дисковод 140Кб (S%i,D%i)"),
		wsprintf(buf, localize_str(LOC_FDD, 4, buf1, sizeof(buf1)), s, d + 1);
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
	case SYS_COMMAND_RESET:
	case SYS_COMMAND_HRESET:
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
	case SYS_COMMAND_INIT_DONE:
		puts("fdd install procs");
		fill_read_proc(st->sr->base_mem + (0xE000 >> BASEMEM_BLOCK_SHIFT), 
			FDD_ROM_SIZE >> BASEMEM_BLOCK_SHIFT, fdd_rom_read, data);
		fill_rw_proc(st->sr->base_mem + (0x800 >> BASEMEM_BLOCK_SHIFT),
			1, fdd_io_read, fdd_io_write, data);
		fill_rw_proc(st->sr->base_mem + (0x2000 >> BASEMEM_BLOCK_SHIFT),
			1, fdd_io_read, fdd_io_write, data);
		fill_rw_proc(st->sr->slots[CONF_MEMORY].service_procs + 1,
			1, fdd_io_read, fdd_io_write, data);
		break;
	case SYS_COMMAND_CPUTIMER:
		if (param == DEF_CPU_TIMER_ID(data->st)) {
//			puts("fdd_irq");
			fdd_int(data);
			if (!(data->status & STATUS_NON_DMA)) {
				fdd_stop(data);
			}
			return 1;
		}
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
		WRITE_ARRAY(out, data->drives[i].regs);
	}
	WRITE_ARRAY(out, data->fdd_ram);
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
		READ_ARRAY(in, data->drives[i].regs);
	}
	READ_ARRAY(in, data->fdd_ram);

	for (i = 0; i < MAX_DRIVES; ++i) {
		if (data->drives[i].present) {
			open_fdd1(data->drives, data->drives[i].disk_name, data->drives[i].readonly & 1, i);
		}
	}
	data->initialized = 1;
	return 0;
}


int  fddaa_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*cf)
{
	struct FDD_DATA*data;
	ISTREAM*rom;
	const char_t*name;
	char_t buf[32];

	puts("in fddaa_init");
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

	fill_fdd1(data);
	if (cf->cfgint[CFG_INT_DRV_TYPE1] == DRV_TYPE_SHUGART) {
		data->drives[1].present = 1;
		sprintf(buf, "fdd1");
		name = sys_get_parameter(buf);
		if (!name) name = cf->cfgstr[CFG_STR_DRV_IMAGE1];
		strcpy(data->drives[1].disk_name, name);
		open_fdd1(data->drives + 1, name, cf->cfgint[CFG_INT_DRV_RO_FLAGS] & 1, 1);
	}
	if (cf->cfgint[CFG_INT_DRV_TYPE2] == DRV_TYPE_SHUGART) {
		data->drives[2].present = 1;
		sprintf(buf, "fdd2");
		name = sys_get_parameter(buf);
		if (!name) name = cf->cfgstr[CFG_STR_DRV_IMAGE2];
		strcpy(data->drives[2].disk_name, name);
		open_fdd1(data->drives + 2, name, cf->cfgint[CFG_INT_DRV_RO_FLAGS] & 2, 2);
	}
	data->initialized = 1;

	st->data = data;
	st->free = remove_fdd1;
	st->command = fdd1_command;
	st->save = fdd1_save;
	st->load = fdd1_load;

	return 0;
}


static void fdd_ram_write(unsigned short a,unsigned char d,struct FDD_DATA*data)
{
//	printf("fdd_ram_write(%X, %X)\n", a, d);
	a -= 0x2000;
	if (a >= 0x800) a -= 0x1400;
//	printf("a = %X\n", a);
	assert(a < sizeof(data->fdd_ram));
	data->fdd_ram[a] = d;
}

static byte fdd_ram_read(unsigned short a,struct FDD_DATA*data)
{
//	printf("fdd_ram_read(%X)\n", a);
	a -= 0x2000;
	if (a >= 0x800) a -= 0x1400;
//	printf("a = %X\n", a);
	assert(a < sizeof(data->fdd_ram));
	return data->fdd_ram[a];
}

static void fdd_reset(struct FDD_DATA*data, int reset)
{
//	printf("fdd_reset: %i\n", reset);
	data->in_reset = reset;
	if (reset) {
		data->status = data->result = 0;
		data->n_params = data->param_ind = 0;
		data->n_results = data->result_ind = 0;
	}
}

static void exec_fdd(struct FDD_DATA*data)
{
	if (data->cmd_id == -1) return;
	data->status |= STATUS_PAR_FULL;
//	printf("executing command: %02X\n", cmds[data->cmd_id].cmd);
	data->n_results = 0;
	data->result_ind = 0;
	cmds[data->cmd_id].op(data);
	if (data->n_results) {
		data->status |= STATUS_NON_DMA | STATUS_CMD_BUSY;
		fdd_start(data);
	} else {
		data->status = STATUS_RES_FULL;
	}
}

static void fdd_command(struct FDD_DATA*data, int cmd)
{
	int opc = cmd & 0x3F;
	int sel = (cmd >> 6) & 3;
	int i;
//	printf("fdd_command: %02X\n", cmd);
	if (data->status & STATUS_CMD_BUSY) {
		fprintf(stderr, "error: fdc is busy on command %02X\n", cmd);
		return;
	}
	if (data->status & STATUS_CMD_FULL) {
		fprintf(stderr, "error: fdc is already got command on command %02X\n", cmd);
		return;
	}
	data->cmd_id = -1;
	for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); ++i) {
		if (cmds[i].cmd == opc) {
			data->cmd_id = i;
		}
	}
	if (data->cmd_id == -1) {
		fprintf(stderr, "error: unknown fdc command %02X\n", cmd);
		return;
	}
	data->status |= STATUS_CMD_FULL;
	data->param_ind = 0;
	data->cmd = cmd;
	data->n_params = cmds[data->cmd_id].n_params;
	if (!data->n_params) exec_fdd(data);
}

static void fdd_parameter(struct FDD_DATA*data, int par)
{
//	printf("fdd_parameter: %02X\n", par);
	if (data->status & STATUS_CMD_BUSY) {
		fprintf(stderr, "error: fdc is busy on parameter %02X\n", par);
		return;
	}
	if (!(data->status & STATUS_CMD_FULL)) {
		fprintf(stderr, "error: fdc have not command on parameter %02X\n", par);
		return;
	}
	data->params[data->param_ind] = par;
	++ data->param_ind;
	if (data->param_ind == data->n_params) exec_fdd(data);
}

static byte fdd_get_status(struct FDD_DATA*data)
{
	byte r = data->status;
//	printf("fdd_get_status: %02X\n", r);
	return r;
}

static byte fdd_get_data(struct FDD_DATA*data)
{
	int ind = data->result_ind;
	byte r = 0;
	data->status &= ~STATUS_IRQ;
	if (data->status & STATUS_NON_DMA) {
		r = data->results[data->result_ind];
		if (!data->sr->in_debug) ++ data->result_ind;
		if (data->result_ind >= data->n_results) {
			data->status = STATUS_RES_FULL;
			data->result = RESULT_COMPL_OK;
			if (data->cmd_id != -1) cmds[data->cmd_id].op(data);
		}
	}
//	printf("fdd_get_data[%i]: %02X\n", ind, r);
	return r;
}

static void fdd_set_data(struct FDD_DATA*data, byte d)
{
//	printf("fdd_set_data[%i]: %02X\n", data->result_ind, d);
	data->status &= ~STATUS_IRQ;
	if (data->status & STATUS_NON_DMA) {
		data->results[data->result_ind] = d;
		if (!data->sr->in_debug) ++ data->result_ind;
		if (data->result_ind >= data->n_results) {
			data->status = STATUS_RES_FULL;
			data->result = RESULT_COMPL_OK;
			if (data->cmd_id != -1) cmds[data->cmd_id].op(data);
		}
	}
}

static byte fdd_get_result(struct FDD_DATA*data)
{
	byte r = data->result;
	if (data->sr->in_debug) return r;
	data->status &= ~STATUS_IRQ;
	if (data->status & STATUS_CMD_BUSY) {
		fprintf(stderr, "error: fdc is busy while reading results\n");
		return r;
	}
	if (!(data->status & STATUS_RES_FULL)) {
		fprintf(stderr, "error: fdc have no result while reading results\n");
		return r;
	}
	data->status = 0;
//	printf("fdd_get_result: %02X\n", r);
	data->result = RESULT_COMPL_OK;
	return r;
}



static void fdd_start(struct FDD_DATA*d)
{
	system_command(d->sr, SYS_COMMAND_SET_CPUTIMER, FDD_TIMER_DELAY, DEF_CPU_TIMER_ID(d->st));
}

static void fdd_stop(struct FDD_DATA*d)
{
	system_command(d->sr, SYS_COMMAND_SET_CPUTIMER, 0, DEF_CPU_TIMER_ID(d->st));
}

static void fdd_int(struct FDD_DATA*d)
{
	d->status |= STATUS_IRQ;
	system_command(d->sr, SYS_COMMAND_NMI, 0, 0);
}


static void fdd_io_write(unsigned short a,unsigned char d,struct FDD_DATA*data)
{
//	printf("fdd_write(%X, %X)\n", a, d);
	if (a >= 0x2000) { fdd_ram_write(a, d, data); return; }
	if (a < 0xA00 || a > 0xA7F) { empty_write(a, d, data); return; }
//	printf("io_write(%X, %X)\n", a, d);
	a &= 7;
	switch (a) {
	case 0: // command
		fdd_command(data, d);
		break;
	case 1: // parameter
		fdd_parameter(data, d);
		break;
	case 2: // reset
		fdd_reset(data, d & 1);
		break;
	case 4: // data
		fdd_set_data(data, d);
		break;
	}
}

static byte fdd_io_read(unsigned short a,struct FDD_DATA*data)
{
	byte r = 0x00;
//	printf("fdd_read(%X) = %X\n", a, r);
	if (a >= 0x2000) { return fdd_ram_read(a, data);}
	if (a < 0xA00 || a > 0xA7F) { return empty_read_zero(a, data); }
//	printf("io_read(%X) = %X\n", a, r);
	if (!data->initialized) return r;
	a &= 7;
	switch (a) {
	case 0: // status
		r = fdd_get_status(data);
		break;
	case 1: // result
		r = fdd_get_result(data);
		break;
	case 4: // data
		r = fdd_get_data(data);
		break;
	}
	return r;
}





static byte fdd_rom_read(unsigned short a, struct FDD_DATA*xd)
{
//	printf("rom_read: %X\n", a);
	return xd->fdd_rom[a & (FDD_ROM_SIZE - 1)];
}

