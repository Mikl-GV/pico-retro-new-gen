/*
	Agat Emulator version 1.19
	Copyright (c) NOP, nnop@newmail.ru
*/

#include "runmgr.h"
#include "runmgrint.h"
#include "runstate.h"
#include "cpu/cpuint.h"
#include "sound/soundint.h"

/* r1297: объявление живёт здесь — в sound_none.c то же имя занято
   статической функцией-членом таблицы звуковых драйверов. */
int sound_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*sc);

#include "localize.h"
#include "common.h"

#include "resource.h"

//#define RUNMGR_DEBUG

#ifdef RUNMGR_DEBUG
#define _RMSG(s) puts(__FUNCTION__ ": " s)
#else
#define _RMSG(s)
#endif

extern int init_system_1(struct SYS_RUN_STATE*sr);
extern int init_system_2e(struct SYS_RUN_STATE*sr);
extern int init_system_aa(struct SYS_RUN_STATE*sr);
extern int init_systems(struct SYS_RUN_STATE*sr); // other systems

int  chargen_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*cf);


int init_slot_state(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*sc)
{
//	printf("********** slot:dev =  %i:%i\n", st->sc->slot_no, st->sc->dev_type);
	switch (st->sc->slot_no) {
	case CONF_ROM:
		puts("rom_install");
		return rom_install(sr, st, sc);
	case CONF_MEMORY:
		puts("ram_install");
		return ram_install(sr, st, sc);
	case CONF_CPU:
		puts("cpu_init");
		return cpu_init(sr, st, sc);
	case CONF_SOUND:
		puts("sound_init");
		return sound_init(sr, st, sc);
	case CONF_TAPE:
		puts("tape_init");
		return tape_init(sr, st, sc);
	case CONF_JOYSTICK:
		puts("joystick_init");
		return joystick_init(sr, st, sc);
	}
	switch (st->sc->dev_type) {
	case DEV_FDD_SHUGART:
		puts("fdd1_init");
		return fdd1_init(sr, st, sc);
	case DEV_FDD_TEAC:
		puts("fdd_init");
		return fdd_init(sr, st, sc);
	case DEV_FDD_ATOM:
		puts("fddaa_init");
		return fddaa_init(sr, st, sc);
	case DEV_FDD_LIBERTY:
		puts("fddliberty_init");
		return fddliberty_init(sr, st, sc);
	case DEV_EXTROM_ATOM:
		puts("extromaa_init");
		return extromaa_init(sr, st, sc);
	case DEV_EXTRAM_ATOM:
		puts("extramaa_init");
		return extramaa_init(sr, st, sc);
	case DEV_VIDEOTERM:
		puts("videoterm_init");
		return videoterm_init(sr, st, sc);
	case DEV_MEMORY_XRAM7:
		puts("xram7_init");
		return xram7_init(sr, st, sc);
	case DEV_MEMORY_PSROM7:
		puts("psrom7_init");
		return psrom7_init(sr, st, sc);
	case DEV_MEMORY_XRAM9:
		puts("xram9_init");
		return xram9_init(sr, st, sc);
	case DEV_SOFTCARD:
		puts("softcard_init");
		return softcard_init(sr, st, sc);
	case DEV_THUNDERCLOCK:
		puts("thunderclock_init");
		return thunderclock_init(sr, st, sc);
	case DEV_PRINTER9:
		puts("printer9_init");
		return printer9_init(sr, st, sc);
	case DEV_MOCKINGBOARD:
		puts("mockingboard_init");
		return mockingboard_init(sr, st, sc);
	case DEV_NIPPELCLOCK:
		puts("nippelclock_init");
		return nippelclock_init(sr, st, sc);
	case DEV_CLOCK_DALLAS:
		puts("noslotclock_init");
		return noslotclock_init(sr, st, sc);
	case DEV_PRINTERA:
		puts("printera_init");
		return printera_init(sr, st, sc);
	case DEV_PRINTER_ATOM:
		puts("printeraa_init");
		return printeraa_init(sr, st, sc);
	case DEV_A2RAMCARD:
	case DEV_RAMFACTOR:
		puts("a2ramcard_init");
		return a2ramcard_init(sr, st, sc);
	case DEV_MEMORY_SATURN:
		puts("saturnmem_init");
		return saturnmem_init(sr, st, sc);
	case DEV_MOUSE_PAR:
		puts("mouse9_init");
		return mouse9_init(sr, st, sc);
	case DEV_MOUSE_APPLE:
		puts("applemouse_init");
		return applemouse_init(sr, st, sc);
	case DEV_MOUSE_NIPPEL:
		puts("nippelmouse_init");
		return nippelmouse_init(sr, st, sc);
	case DEV_ACI:
		puts("aci_init");
		return aci_init(sr, st, sc);
	case DEV_SCSI_CMS:
		puts("cms_init");
		return cms_init(sr, st, sc);
	case DEV_CHARGEN2:
		puts("chargen_init");
		return chargen_init(sr, st, sc);
	case DEV_FIRMWARE:
		puts("firmware_init");
		return firmware_init(sr, st, sc);
	}
	return 0;
}

int slot_command(struct SLOT_RUN_STATE*st, int id, int data, long param)
{
	if (st->command) return st->command(st, id, data, param);
	else return 0;
}

int save_slot_state(struct SLOT_RUN_STATE*st, OSTREAM*out)
{
	if (st->save) return st->save(st, out);
	else return 0;
}

int load_slot_state(struct SLOT_RUN_STATE*st, ISTREAM*in)
{
	if (st->load) return st->load(st, in);
	else return 0;
}

int free_slot_state(struct SLOT_RUN_STATE*st)
{
	if (st->free) return st->free(st);
	else return -1;
}



struct SYS_RUN_STATE *init_system_state(struct SYSCONFIG*c, HWND hmain, LPCTSTR name)
{
	struct SYS_RUN_STATE*sr;
	int i, j, r;
	static const int sys_types[NSYSTYPES] = {
		SYSTEM_7,
		SYSTEM_9,
		SYSTEM_A, // original apple 2
		SYSTEM_A, // apple 2 plus -> apple 2
		SYSTEM_E, // apple 2e
		SYSTEM_1, // apple 1
		SYSTEM_E, // apple 2ee -> apple 2e
		SYSTEM_A, // pravetz 82 -> apple 2
		SYSTEM_E,  // pravetz 8A -> apple 2e
		SYSTEM_AA  // acorn atom
	};

	_RMSG("entry");

	sr = calloc(1, sizeof(*sr));
	if (!sr) {
		_RMSG("memory allocation error");
		return NULL;
	}
	sr->name = name?_tcsdup(name):NULL;
	sr->config = c;
	sr->cursystype = sys_types[c->systype]; // translate compatible system type
	sr->base_w = hmain;
	sr->gconfig = &g_config;

	sr->keyreg = is_keyb_english(sr)?0xFF:0x7F;
	sr->keymask = (sr->cursystype == SYSTEM_7)?0xC0: 0xFF;

	set_run_state_ptr(sr->name, sr);

	for (i = 0; i < NCONFTYPES; i++ ) {
		sr->slots[i].sc = c->slots + i;
		sr->slots[i].sr = sr;
		if (i <= CONF_SLOT7) {
			sr->slots[i].baseio_sel = sr->baseio_sel + i + 8;
			sr->slots[i].io_sel = sr->io_sel + i;
		}
	}

	_RMSG("init_system");
	switch (sr->cursystype) {
	case SYSTEM_1:
		r = init_system_1(sr);
		break;
	case SYSTEM_E:
		r = init_system_2e(sr);
		break;
	case SYSTEM_AA:
		r = init_system_aa(sr);
		break;
	default:
		r = init_systems(sr);
		break;
	}
	if (r < 0) {
		_RMSG("init_system failed");
		goto fail2;
	}

	_RMSG("init_video_window");
	r = init_video_window(sr);
	if (r < 0) {
		_RMSG("init_video_window failed");
		goto fail1;
	}
	if (sr->name) {
		GetWindowText(sr->video_w, sr->title, 1024);
		lstrcat(sr->title, TEXT(" � "));
		lstrcat(sr->title, sr->name);
		SetWindowText(sr->video_w, sr->title);
	}
	_RMSG("video title set");

	for (i = 0; i < NCONFTYPES; i++ ) {
		_RMSG("calling init_slot_state");
		r = init_slot_state(sr, sr->slots + i, c->slots + i);
		if (r < 0) {
			_RMSG("init_slot_state failed");
			goto fail;
		}
	}

	_RMSG("calling video_init");
	r = video_init(sr);
	if (r < 0) {
		_RMSG("video_init failed");
		goto fail;
	}

	_RMSG("calling update_xio_status");
	update_xio_status(sr);

	system_command(sr, SYS_COMMAND_INITMENU, 0, (long)sr->popup_menu);
	system_command(sr, SYS_COMMAND_INIT_DONE, 0, 0);

	PostMessage(sr->base_w, WM_COMMAND, MAKEWPARAM(IDC_UPDATE,0), 0);
	_RMSG("exit");
	return sr;
fail:
	_RMSG("failure");
	if (sr->popup_menu)
		system_command(sr, SYS_COMMAND_FREEMENU, 0, (long)sr->popup_menu);
	set_run_state_ptr(sr->name, NULL);
	PostMessage(sr->base_w, WM_COMMAND, MAKEWPARAM(IDC_UPDATE, 0), 0);
	for (j = 0; j < i ; j ++ ) {
		free_slot_state(sr->slots + j);
	}
	term_video_window(sr);
fail1:
	if (sr->sys.free_system) sr->sys.free_system(sr);
fail2:
	if (sr->name) free((void*)sr->name);
	free(sr);
	return NULL;
}

int free_system_state(struct SYS_RUN_STATE*sr)
{
	int i;

	if (!sr) return -1;

	_RMSG("entry");

	if (sr->popup_menu)
		system_command(sr, SYS_COMMAND_FREEMENU, 0, (long)sr->popup_menu);

	_RMSG("stopping system");
	system_command(sr, SYS_COMMAND_STOP, 0, 0);
	_RMSG("updating base window");
	PostMessage(sr->base_w, WM_COMMAND, MAKEWPARAM(IDC_UPDATE,0), 0);

	and_run_state_flags(sr->name, ~(RUNSTATE_RUNNING|RUNSTATE_PAUSED));
	set_run_state_ptr(sr->name, NULL);

	_RMSG("starting video window termination");
	term_video_window(sr);
	_RMSG("end of video window termination");

	_RMSG("freeing slots configurations");
	for (i = 0; i < NCONFTYPES; i++) {
		free_slot_state(sr->slots + i);
	}
	_RMSG("end of freeing slots configurations");
	_RMSG("freeing system");
	if (sr->sys.free_system) sr->sys.free_system(sr);
	if (sr->name) free((void*)sr->name);
	free(sr);
	_RMSG("exit");
	return 0;
}

int system_command(struct SYS_RUN_STATE*sr, int id, int data, long param)
{
	int i, r = 0;
	if (!sr) return -1;
//	printf("system_command(%i, %i, %i)\n", id, data, param);
	for (i = 0; i < NCONFTYPES ; i++) {
		int r0;
		r0 = slot_command(sr->slots + i, id, data, param);
//		printf("slot_command(%i, %i, %i, %i) = %i\n", i, id, data, param, r0);
		if (r0 < 0) r = r0;
		if (r0 > 0) { r = r0; break; }
	}
	switch (id) {
	case SYS_COMMAND_START:
		and_run_state_flags(sr->name, ~RUNSTATE_PAUSED);
		or_run_state_flags(sr->name, RUNSTATE_RUNNING);
		PostMessage(sr->base_w, WM_COMMAND, MAKEWPARAM(IDC_UPDATE,0), 0);
		break;
	case SYS_COMMAND_STOP:
		or_run_state_flags(sr->name, RUNSTATE_PAUSED);
		PostMessage(sr->base_w, WM_COMMAND, MAKEWPARAM(IDC_UPDATE,0), 0);
		break;
	case SYS_COMMAND_HRESET:
		if (sr->sys.restart_system)
			sr->sys.restart_system(sr);
		disable_all_xio(sr);
		system_command(sr, SYS_COMMAND_PSROM_RELEASE, 8, 0);
		system_command(sr, SYS_COMMAND_XRAM_RELEASE, 8, 0);
		break;
	case SYS_COMMAND_ACTIVATE:
		ShowWindow(sr->video_w, SW_SHOWNORMAL);
		SetActiveWindow(sr->video_w);
		break;
	case SYS_COMMAND_SET_PARENT:
		if (sr->video_w) SetParent(sr->video_w, (HWND)param);
		else r = -1;
		break;
	case SYS_COMMAND_SET_STATUS_TEXT:
		if (sr->video_w) {
			if (param) {
				TCHAR title[1024];
				lstrcpy(title, sr->title);
				lstrcat(title, TEXT(": "));
				lstrcat(title, (LPCTSTR)param);
				SetWindowText(sr->video_w, title);
			} else {
				SetWindowText(sr->video_w, sr->title);
			}
		}
		break;
	}
	return r;
}

#define MAKEDWORD(a,b) (((a)&0xFFFF) | (((b) & 0xFFFF)<<16))

int save_system_state(struct SYS_RUN_STATE*sr, OSTREAM*out)
{
	int i, r = 0;
	if (!sr) return -1;
	for (i = 0; i < NCONFTYPES; i++) {
		int r0;
		DWORD bb = MAKEDWORD(i, ~i);
		r0 = oswrite(out, &bb, sizeof(bb));
		if (r0 != sizeof(bb)) return -1;
		r0 = save_slot_state(sr->slots + i, out);
		if (r0 < 0) return r;
		WRITE_FIELD(out, sr->slots[i].xio_en);
	}
	WRITE_FIELD(out, sr->ints_enabled);
	return r;
}

int load_system_state(struct SYS_RUN_STATE*sr, ISTREAM*in)
{
	int i, r = 0;
	if (!sr) return -1;
	system_command(sr, SYS_COMMAND_FREEMENU, 0, (long)sr->popup_menu);
	for (i = 0; i < NCONFTYPES; i++) {
		int r0;
		DWORD bb = MAKEDWORD(i, ~i), bb1;
//		printf("loading conf %i\n", i);
		r0 = isread(in, &bb1, sizeof(bb1));
		if (r0 != sizeof(bb1)) {
			return -1;
		}	
		if (bb1 != bb) {
			printf("conf %i prefix failed\n", i);
			return -2;
		}
		r0 = load_slot_state(sr->slots + i, in);
		if (r0 < 0) return r;
		READ_FIELD(in, sr->slots[i].xio_en);
	}
	READ_FIELD(in, sr->ints_enabled);
	switch (sr->cursystype) {
	case SYSTEM_7:
		system_command(sr, SYS_COMMAND_XRAM_RELEASE, CONF_SLOT6 + 1, 0);
		break;
	case SYSTEM_9:
		if (sr->apple_emu) {
			int fl = 3;
			system_command(sr, SYS_COMMAND_APPLE9_RESTORE, CONF_SLOT6 + 1, (long)&fl);
		} else {
			for (i = 0; i < 8; ++i)
				system_command(sr, SYS_COMMAND_BASEMEM9_RESTORE, CONF_SLOT6 + 1, i);
		}
		break;
	}
	update_xio_status(sr);
	system_command(sr, SYS_COMMAND_INITMENU, 0, (long)sr->popup_menu);
	system_command(sr, SYS_COMMAND_LOAD_DONE, 0, 0);
	return r;
}


// ======================================================

void mem_write(word adr, byte data, struct SYS_RUN_STATE*sr)
{
	int ind=adr>>0x0B;
//	if (adr == 0x87) {
//		logprint(0, "mem_write(%04X, %02X)", adr, data);
//		system_command(sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
//		if (access("memdump1.bin", 0)) dump_mem(sr, 0, 0x10000, "memdump1.bin");
//	}
	mem_proc_write(adr, data, sr->base_mem + ind);
}

byte mem_read(word adr, struct SYS_RUN_STATE*sr)
{
	int ind=adr>>0x0B;
	byte r;
	r = mem_proc_read(adr, sr->base_mem + ind);
	video_mem_access(sr);
//	printf("mem_read(%04X) = %02X\n", adr, r);
	return r;
}

int disable_all_xio(struct SYS_RUN_STATE*sr)
{
	int i;
	for (i = 0; i < NCONFTYPES; ++i) {
		sr->slots[i].xio_en = 0;
	}
	return update_xio_status(sr);
}

int update_xio_status(struct SYS_RUN_STATE*sr)
{
	int i;
	if (sr->sys.xio_control) {
		if (!sr->sys.xio_control(sr, 1)) {
			sr->sys.xio_control(sr, 0);
			return -1;
		}
	}
	for (i = 0; i < NCONFTYPES; ++i) {
//		printf("slot %i: xio_en = %i\n", i, sr->slots[i].xio_en);
		if (sr->slots[i].xio_en) {
			sr->base_mem[0xC800 >> BASEMEM_BLOCK_SHIFT] = sr->slots[i].xio_sel;
//			printf("selected xrom#%i\n", i);
			return i;
		}
	}
//	printf("disabled all xrom\n");
	if (sr->sys.xio_control) sr->sys.xio_control(sr, 0);
	else {
		sr->base_mem[0xC800 >> BASEMEM_BLOCK_SHIFT].read = empty_read;
		sr->base_mem[0xC800 >> BASEMEM_BLOCK_SHIFT].write = empty_write;
	}	
	return -1;
}

int enable_slot_xio(struct SLOT_RUN_STATE*ss, int en)
{
	if (!en) {
		int i;
//		printf("disabling xrom from slot%i\n", ss->sc->slot_no);
		for (i = 0; i < NCONFTYPES; ++i) ss->sr->slots[i].xio_en = 0;
	} else {
		if (ss->xio_en == en) return 0;
		ss->xio_en = en;
	}
//	printf("%sabled xrom#%i\n", en?"en":"dis", ss->sc->slot_no);
//	system_command(ss->sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
	return update_xio_status(ss->sr);
}
