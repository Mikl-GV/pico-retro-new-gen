/*
	Agat Emulator version 1.19
	Copyright (c) NOP, nnop@newmail.ru
*/

#include <windows.h>
#include <string.h>
#include <tchar.h>
#include "resource.h"
#include "sysconf.h"

#include "localize.h"

char conf_present[NSYSTYPES][NCONFTYPES] = {
	{0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
	{0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1},
	{1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1},
};


const unsigned memsizes_b[NMEMSIZES] = {
	0,			// 0, 1
	4 * 1024,		// 1, 2
	8 * 1024,		// 2, 4
	16 * 1024,		// 3, 8
	32 * 1024,		// 4, 16
	48 * 1024,		// 5, 32
	64 * 1024,		// 6, 64
	128 * 1024,		// 7, 128
	12 * 1024,		// 8, 256
	20 * 1024,		// 9, 512
	24 * 1024,		// 10, 1024
	36 * 1024,		// 11, 2048
	256 * 1024,		// 12, 4096
	512 * 1024,		// 13, 8192
	768 * 1024,		// 14, 16384
	1024 * 1024,		// 15, 32768
	2 * 1024 * 1024,	// 16, 65536
	4 * 1024 * 1024,	// 17, 131072
	1 * 1024,		// 18, 262144
	2 * 1024,		// 19, 524288
	40 * 1024		// 20, 1048576
};



int clear_config(struct SYSCONFIG*c)
{
	ZeroMemory(c, sizeof(*c));
	return 0;
}

/* r1297: forward-объявление — в оригинале висело на неявном int. */
static int reset_sysicon(struct SYSCONFIG*c, int systype);

int reset_config(struct SYSCONFIG*c, int systype)
{
	int i;
	if (systype == -1) systype = c->systype;

	memset(c, 0, sizeof(*c));
	c->systype = systype;
	for (i = 0; i < NCONFTYPES; i++) {
		c->slots[i].slot_no = i;
	}

	switch (systype) {
	case SYSTEM_7:
		reset_slot_config(c->slots+CONF_SLOT2, DEV_MEMORY_PSROM7, systype);
		reset_slot_config(c->slots+CONF_SLOT3, DEV_FDD_TEAC, systype);
		reset_slot_config(c->slots+CONF_SLOT4, DEV_MEMORY_XRAM7, systype);
		reset_slot_config(c->slots+CONF_SLOT6, DEV_FDD_SHUGART, systype);
		break;
	case SYSTEM_9:
		reset_slot_config(c->slots+CONF_SLOT2, DEV_MEMORY_XRAM9, systype);
		reset_slot_config(c->slots+CONF_SLOT4, DEV_PRINTER9, systype);
		reset_slot_config(c->slots+CONF_SLOT5, DEV_FDD_TEAC, systype);
		reset_slot_config(c->slots+CONF_SLOT6, DEV_FDD_SHUGART, systype);
		break;
	case SYSTEM_1:
		reset_slot_config(c->slots+CONF_SLOT1, DEV_ACI, systype);
		break;
	case SYSTEM_A:
		reset_slot_config(c->slots+CONF_SLOT6, DEV_FDD_SHUGART, systype);
		break;
	case SYSTEM_P:
	case SYSTEM_82:
		reset_slot_config(c->slots+CONF_SLOT1, DEV_PRINTERA, systype);
		reset_slot_config(c->slots+CONF_SLOT0, DEV_MEMORY_XRAMA, systype);
		reset_slot_config(c->slots+CONF_SLOT6, DEV_FDD_SHUGART, systype);
		break;
	case SYSTEM_E:
	case SYSTEM_EE:
	case SYSTEM_8A:
		reset_slot_config(c->slots+CONF_SLOT1, DEV_PRINTERA, systype);
		reset_slot_config(c->slots+CONF_SLOT0, DEV_MEMORY_XRAMA, systype);
		reset_slot_config(c->slots+CONF_SLOT6, DEV_FDD_SHUGART, systype);
		break;
	case SYSTEM_AA:
		reset_slot_config(c->slots+CONF_SLOT0, DEV_EXTROM_ATOM, systype);
		reset_slot_config(c->slots+CONF_SLOT1, DEV_FDD_ATOM, systype);
		break;
	}
	for (i = CONF_EXT; i < NCONFTYPES; i++) {
		reset_slot_config(c->slots+i, DEV_SYSTEM, systype);
	}

	reset_sysicon(c, systype);
	return 0;
}

int reset_sysicon(struct SYSCONFIG*c, int systype)
{
	HBITMAP bmp;
	int codes[]={IDB_AGAT7_LOGO, IDB_AGAT9_LOGO, IDB_APPLE2_LOGO, 
		IDB_APPLE2P_LOGO, IDB_APPLE2E_LOGO, IDB_APPLE1_LOGO, 
		IDB_APPLE2EE_LOGO, IDB_PRAVETZ82_LOGO, IDB_PRAVETZ8A_LOGO,
		IDB_ATOM_LOGO};
	HDC dc;
	int r;
	bmp = LoadBitmap(localize_get_def_lib(), MAKEINTRESOURCE(codes[systype]));
	if (!bmp) return -1;
	free_sysicon(&c->icon);
	dc = CreateCompatibleDC(NULL);
	r = bitmap_to_sysicon(dc, bmp, &c->icon);
	DeleteDC(dc);
	DeleteObject(bmp);
	return r;
}

int free_config(struct SYSCONFIG*c)
{
	free_sysicon(&c->icon);
	return 0;
}

int reset_slot_config(struct SLOTCONFIG*c, int devtype, int systype)
{
	c->dev_type = devtype;
	switch (c->dev_type) {
	case DEV_MEMORY_PSROM7:
		c->cfgint[CFG_INT_MEM_SIZE] = 4;
		c->cfgint[CFG_INT_MEM_MASK] = 8 | 16 | 32 | 64 | 128;
		return 0;
	case DEV_MEMORY_XRAM7:
		c->cfgint[CFG_INT_MEM_SIZE] = 4;
		c->cfgint[CFG_INT_MEM_MASK] = 8 | 16 | 32 | 64 | 128;
		return 0;
	case DEV_MEMORY_XRAM9:
		c->cfgint[CFG_INT_MEM_SIZE] = 7;
		c->cfgint[CFG_INT_MEM_MASK] = 128;
		return 0;
	case DEV_MEMORY_XRAMA:
		c->cfgint[CFG_INT_MEM_SIZE] = 3;
		c->cfgint[CFG_INT_MEM_MASK] = 8;
		return 0;
	case DEV_FDD_TEAC:
		c->cfgint[CFG_INT_DRV_TYPE1] = DRV_TYPE_2S2D;
		c->cfgint[CFG_INT_DRV_TYPE2] = DRV_TYPE_NONE;
		c->cfgint[CFG_INT_DRV_COUNT] = 1;
		c->cfgint[CFG_INT_DRV_RO_FLAGS] = 3;
		c->cfgint[CFG_INT_DRV_FAST] = 1;
		_tcscpy(c->cfgstr[CFG_STR_DRV_ROM], TEXT("ROMS\\TEAC.ROM"));
		c->cfgint[CFG_INT_DRV_ROM_RES] = 21;
		_tcscpy(c->cfgstr[CFG_STR_DRV_IMAGE1], TEXT("1.DSK"));
		_tcscpy(c->cfgstr[CFG_STR_DRV_IMAGE2], TEXT("2.DSK"));
		return 0;
	case DEV_FDD_LIBERTY:
		c->cfgint[CFG_INT_DRV_TYPE1] = DRV_TYPE_2S1D;
		c->cfgint[CFG_INT_DRV_TYPE2] = DRV_TYPE_NONE;
		c->cfgint[CFG_INT_DRV_COUNT] = 1;
		c->cfgint[CFG_INT_DRV_RO_FLAGS] = 3;
		c->cfgint[CFG_INT_DRV_FAST] = 1;
		_tcscpy(c->cfgstr[CFG_STR_DRV_ROM], TEXT("ROMS\\LIBERTY.ROM"));
		_tcscpy(c->cfgstr[CFG_STR_DRV_IMAGE1], TEXT("LIBERTY.DSK"));
		return 0;
	case DEV_MOUSE_PAR:
		c->cfgint[CFG_INT_MOUSE_TYPE] = MOUSE_MARS;
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\CM6337.ROM"));
		return 0;
	case DEV_MOUSE_APPLE:
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\AMOUSE.ROM"));
		return 0;
	case DEV_PRINTER9:
		c->cfgint[CFG_INT_PRINT_MODE] = PRINT_TEXT;
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\CM6337.ROM"));
		return 0;
	case DEV_PRINTERA:
		c->cfgint[CFG_INT_PRINT_MODE] = PRINT_TEXT;
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\CENTRONI.ROM"));
		return 0;
	case DEV_PRINTER_ATOM:
		c->cfgint[CFG_INT_PRINT_MODE] = PRINT_TEXT;
		return 0;
	case DEV_A2RAMCARD:
		c->cfgint[CFG_INT_MEM_SIZE] = 15;
		c->cfgint[CFG_INT_MEM_MASK] = 4096 | 8192 | 16384 | 32768;
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\aii_memory_expansion\\AII Memory Expansion.bin"));
		c->cfgstr[CFG_STR_RAM][0] = 0;
		return 0;
	case DEV_RAMFACTOR:
		c->cfgint[CFG_INT_MEM_SIZE] = 15;
		c->cfgint[CFG_INT_MEM_MASK] = 4096 | 8192 | 16384 | 32768 | 65536 | 131072;
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\ramfactor\\RAMFactor v1.4.bin"));
		c->cfgstr[CFG_STR_RAM][0] = 0;
		return 0;
	case DEV_MEMORY_SATURN:
		c->cfgint[CFG_INT_MEM_SIZE] = 7;
		c->cfgint[CFG_INT_MEM_MASK] = 8 | 16 | 64 | 128;
		return 0;
	case DEV_MOCKINGBOARD:
		return 0;
	case DEV_NIPPELCLOCK:
		return 0;
	case DEV_MOUSE_NIPPEL:
		return 0;
	case DEV_SCSI_CMS:
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\SCSI_CMS.ROM"));
		return 0;
	case DEV_FDD_ATOM:
		c->cfgint[CFG_INT_DRV_TYPE1] = DRV_TYPE_SHUGART;
		c->cfgint[CFG_INT_DRV_TYPE2] = DRV_TYPE_NONE;
		c->cfgint[CFG_INT_DRV_COUNT] = 1;
		c->cfgint[CFG_INT_DRV_RO_FLAGS] = 3;
		c->cfgint[CFG_INT_DRV_FAST] = 1;
		_tcscpy(c->cfgstr[CFG_STR_DRV_ROM], TEXT("ROMS\\DOSROM.ROM"));
		_tcscpy(c->cfgstr[CFG_STR_DRV_IMAGE1], TEXT("ACORN.DSK"));
		return 0;
	case DEV_EXTROM_ATOM:
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\ATOM\\AXR1.ROM"));
		return 0;

	case DEV_FDD_SHUGART:
		c->cfgint[CFG_INT_DRV_TYPE1] = DRV_TYPE_SHUGART;
		c->cfgint[CFG_INT_DRV_TYPE2] = DRV_TYPE_NONE;
		c->cfgint[CFG_INT_DRV_COUNT] = 1;
		c->cfgint[CFG_INT_DRV_RO_FLAGS] = 3;
		c->cfgint[CFG_INT_DRV_FAST] = 1;
		switch (systype) {
		case SYSTEM_7:
			_tcscpy(c->cfgstr[CFG_STR_DRV_ROM], TEXT("ROMS\\SHUGART7.ROM"));
			c->cfgint[CFG_INT_DRV_ROM_RES] = 22;
			break;
		case SYSTEM_9:
			_tcscpy(c->cfgstr[CFG_STR_DRV_ROM], TEXT("ROMS\\SHUGART9.ROM"));
			c->cfgint[CFG_INT_DRV_ROM_RES] = 23;
			break;
		case SYSTEM_A:
			_tcscpy(c->cfgstr[CFG_STR_DRV_ROM], TEXT("ROMS\\SHUGART13.ROM"));
			break;
		case SYSTEM_P:
		case SYSTEM_E:
		case SYSTEM_EE:
		case SYSTEM_82:
		case SYSTEM_8A:
			_tcscpy(c->cfgstr[CFG_STR_DRV_ROM], TEXT("ROMS\\SHUGARTA.ROM"));
			c->cfgint[CFG_INT_DRV_ROM_RES] = 24;
			break;
		}
		_tcscpy(c->cfgstr[CFG_STR_DRV_IMAGE1], TEXT("3.DSK"));
		_tcscpy(c->cfgstr[CFG_STR_DRV_IMAGE2], TEXT("4.DSK"));
		return 0;
	case DEV_VIDEOTERM:
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\VIDEOTERM.ROM"));
		_tcscpy(c->cfgstr[CFG_STR_ROM2], TEXT("FNTS\\VIDEOTERM_NORM.FNT"));
		_tcscpy(c->cfgstr[CFG_STR_ROM3], TEXT("FNTS\\VIDEOTERM_INV.FNT"));
		c->cfgint[CFG_INT_ROM_RES] = 200;
		return 0;
	case DEV_THUNDERCLOCK:
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\THUNDERCLOCK.ROM"));
		c->cfgint[CFG_INT_ROM_RES] = 300;
		return 0;
	case DEV_ACI:
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\ACI.ROM"));
		return 0;
	case DEV_FIRMWARE:
		_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\APPLE2O.ROM"));
		c->cfgint[CFG_INT_ROM_RES] = 13;
		c->cfgint[CFG_INT_ROM_SIZE] = 0x3000;
		c->cfgint[CFG_INT_ROM_MASK] = 0x3FFF;
		c->cfgint[CFG_INT_ROM_OFS] = 0x1000;
		return 0;
	case DEV_SYSTEM:
		switch (c->slot_no) {
		case CONF_MEMORY:
			switch (systype) {
			case SYSTEM_7:
				c->cfgint[CFG_INT_MEM_SIZE] = 4;
				c->cfgint[CFG_INT_MEM_MASK] = 16 | 64 | 128;
				return 0;
			case SYSTEM_9:
				c->cfgint[CFG_INT_MEM_SIZE] = 7;
				c->cfgint[CFG_INT_MEM_MASK] = 128;
				return 0;
			case SYSTEM_1:
				c->cfgint[CFG_INT_MEM_SIZE] = 2;
				c->cfgint[CFG_INT_MEM_MASK] = 2 | 4 | 16 | 2048;
				return 0;
			case SYSTEM_A:
				c->cfgint[CFG_INT_MEM_SIZE] = 5;
				c->cfgint[CFG_INT_MEM_MASK] = 2 | 4 | 8 | 16 | 32 | 256 | 512 | 1024 | 2048;
				return 0;
			case SYSTEM_P:
			case SYSTEM_82:
				c->cfgint[CFG_INT_MEM_SIZE] = 5;
				c->cfgint[CFG_INT_MEM_MASK] = 32;
				return 0;
			case SYSTEM_E:
			case SYSTEM_8A:
				c->cfgint[CFG_INT_MEM_SIZE] = 7;
				c->cfgint[CFG_INT_MEM_MASK] = 64 | 128;
				return 0;
			case SYSTEM_EE:
				c->cfgint[CFG_INT_MEM_SIZE] = 7;
				c->cfgint[CFG_INT_MEM_MASK] = 64 | 128;
				return 0;
			case SYSTEM_AA:
				c->cfgint[CFG_INT_MEM_SIZE] = 8;
				c->cfgint[CFG_INT_MEM_MASK] = 256 | 524288 | 1048576;
				return 0;
			}
			break;
		case CONF_MONITOR:
			switch (systype) {
			case SYSTEM_1:
				c->dev_type = DEV_TTYA1;
				c->cfgint[CFG_INT_TTY_SPEED] = 100;
				return 0;
			case SYSTEM_7:
			case SYSTEM_9:
			case SYSTEM_AA:
				c->dev_type = DEV_NULL;
				return 0;
			case SYSTEM_A:
			case SYSTEM_P:
			case SYSTEM_E:
			case SYSTEM_EE:
			case SYSTEM_82:
			case SYSTEM_8A:
				c->dev_type = DEV_COLOR;
				return 0;
			}
			break;
		case CONF_CPU:
			switch (systype) {
			case SYSTEM_EE:
				c->dev_type = DEV_65C02;
				break;
			default:
				c->dev_type = DEV_6502;
				break;
			}
			c->cfgint[CFG_INT_CPU_SPEED] = 100;
			c->cfgint[CFG_INT_CPU_EXT] = 1;
			return 0;
		case CONF_SOUND:
			c->dev_type = DEV_MMSYSTEM;
			c->cfgint[CFG_INT_SOUND_FREQ] = 22050;
			c->cfgint[CFG_INT_SOUND_BUFSIZE] = 8192;
			return 0;
		case CONF_TAPE:
			switch (systype) {
			case SYSTEM_7:
			case SYSTEM_A:
			case SYSTEM_P:
			case SYSTEM_E:
			case SYSTEM_EE:
			case SYSTEM_1:
			case SYSTEM_82:
				c->dev_type = DEV_TAPE_FILE;
				break;
			case SYSTEM_9:
			case SYSTEM_8A:
			case SYSTEM_AA:
				c->dev_type = DEV_TAPE_NONE;
				break;
			}
			_tcscpy(c->cfgstr[CFG_STR_TAPE], TEXT("tape.wav"));
			c->cfgint[CFG_INT_TAPE_FREQ] = 8000;
			return 0;
		case CONF_JOYSTICK:
			switch (systype) {
			case SYSTEM_AA:
				c->dev_type = DEV_NULL;
				break;
			default:
				c->dev_type = DEV_MOUSE;
			}
			return 0;
		case CONF_ROM:
			c->cfgint[CFG_INT_ROM_FLAGS] = 0;
			switch (systype) {
			case SYSTEM_7:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\MONITOR7.ROM"));
				c->cfgint[CFG_INT_ROM_RES] = 11;
				c->cfgint[CFG_INT_ROM_SIZE] = 0x800;
				c->cfgint[CFG_INT_ROM_MASK] = 0x7FF;
				c->cfgint[CFG_INT_ROM_OFS] = 0;
				return 0;
			case SYSTEM_1:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\APPLE1.ROM"));
				c->cfgint[CFG_INT_ROM_RES] = 11;
				c->cfgint[CFG_INT_ROM_SIZE] = 0x100;
				c->cfgint[CFG_INT_ROM_MASK] = 0x0FF;
				c->cfgint[CFG_INT_ROM_OFS] = 0;
				c->cfgint[CFG_INT_ROM_FLAGS] |= CFG_INT_ROM_FLAG_A1;
				return 0;
			case SYSTEM_9:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\MONITOR9.ROM"));
				c->cfgint[CFG_INT_ROM_RES] = 12;
				c->cfgint[CFG_INT_ROM_SIZE] = 0x800;
				c->cfgint[CFG_INT_ROM_MASK] = 0x7FF;
				c->cfgint[CFG_INT_ROM_OFS] = 0;
				return 0;
			case SYSTEM_A:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\APPLE2O.ROM"));
				c->cfgint[CFG_INT_ROM_RES] = 13;
				c->cfgint[CFG_INT_ROM_SIZE] = 0x3000;
				c->cfgint[CFG_INT_ROM_MASK] = 0x3FFF;
				c->cfgint[CFG_INT_ROM_OFS] = 0x1000;
				return 0;
			case SYSTEM_P:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\APPLE2.ROM"));
				c->cfgint[CFG_INT_ROM_SIZE] = 0x3000;
				c->cfgint[CFG_INT_ROM_MASK] = 0x3FFF;
				c->cfgint[CFG_INT_ROM_OFS] = 0x1000;
				return 0;
			case SYSTEM_82:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\PRAVETZ82.ROM"));
				c->cfgint[CFG_INT_ROM_SIZE] = 0x3000;
				c->cfgint[CFG_INT_ROM_MASK] = 0x3FFF;
				c->cfgint[CFG_INT_ROM_OFS] = 0x1000;
				return 0;
			case SYSTEM_E:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\APPLE2E.ROM"));
				c->cfgint[CFG_INT_ROM_SIZE] = 0x4000;
				c->cfgint[CFG_INT_ROM_MASK] = 0x3FFF;
				c->cfgint[CFG_INT_ROM_OFS] = 0;
				return 0;
			case SYSTEM_EE:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\APPLE2EE.ROM"));
				c->cfgint[CFG_INT_ROM_SIZE] = 0x4000;
				c->cfgint[CFG_INT_ROM_MASK] = 0x3FFF;
				c->cfgint[CFG_INT_ROM_OFS] = 0;
				return 0;
			case SYSTEM_8A:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\PRAVETZ8C.ROM"));
				c->cfgint[CFG_INT_ROM_SIZE] = 0x4000;
				c->cfgint[CFG_INT_ROM_MASK] = 0x3FFF;
				c->cfgint[CFG_INT_ROM_OFS] = 0;
				return 0;
			case SYSTEM_AA:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("ROMS\\ACORN_ATOM_FP.ROM"));
				c->cfgint[CFG_INT_ROM_SIZE] = 0x4000;
				c->cfgint[CFG_INT_ROM_MASK] = 0x3FFF;
				c->cfgint[CFG_INT_ROM_OFS] = 0;
				c->cfgint[CFG_INT_ROM_FLAGS] |= CFG_INT_ROM_FLAG_AATOM;
				return 0;
			}
			break;
		case CONF_CHARSET:
			c->cfgint[CFG_INT_ROM_SIZE] = 0x800;
			switch (systype) {
			case SYSTEM_7:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("FNTS\\AGATHE7.FNT"));
				c->cfgint[CFG_INT_ROM_RES] = 1;
				return 0;
			case SYSTEM_9:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("FNTS\\AGATHE9.FNT"));
				c->cfgint[CFG_INT_ROM_RES] = 2;
				return 0;
			case SYSTEM_1:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("FNTS\\APPLE1.FNT"));
				c->cfgint[CFG_INT_ROM_RES] = 3;
				return 0;
			case SYSTEM_A:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("FNTS\\APPLE2.FNT"));
				c->cfgint[CFG_INT_ROM_RES] = 3;
				return 0;
			case SYSTEM_P:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("FNTS\\APPLE2.FNT"));
				c->cfgint[CFG_INT_ROM_RES] = 3;
				return 0;
			case SYSTEM_82:	
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("FNTS\\PRAVETZ82.FNT"));
				c->cfgint[CFG_INT_ROM_RES] = 3;
				return 0;
			case SYSTEM_E:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("FNTS\\APPLE2E.FNT"));
				c->cfgint[CFG_INT_ROM_RES] = 3;
				c->cfgint[CFG_INT_ROM_SIZE] = 0x1000;
				return 0;
			case SYSTEM_EE:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("FNTS\\APPLE2EE.FNT"));
				c->cfgint[CFG_INT_ROM_RES] = 3;
				c->cfgint[CFG_INT_ROM_SIZE] = 0x1000;
				return 0;
			case SYSTEM_8A:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("FNTS\\PRAVETZ8A8C_BG.FNT"));
				c->cfgint[CFG_INT_ROM_RES] = 3;
				c->cfgint[CFG_INT_ROM_SIZE] = 0x1000;
				return 0;
			case SYSTEM_AA:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("FNTS\\ACORN.FNT"));
				c->cfgint[CFG_INT_ROM_RES] = 3;
				c->cfgint[CFG_INT_ROM_SIZE] = 0xC00;
				return 0;
			}
			break;
		case CONF_KEYBOARD:
			switch (systype) {
			case SYSTEM_1:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("KEYB\\APPLE1.BIN"));
				break;
			case SYSTEM_A:
			case SYSTEM_P:
			case SYSTEM_E:
			case SYSTEM_EE:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("KEYB\\APPLE2.BIN"));
				return 0;
			case SYSTEM_82:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("KEYB\\PRAVETZ82.BIN"));
				return 0;
			case SYSTEM_8A:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("KEYB\\PRAVETZ8A.BIN"));
				return 0;
			default:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("KEYB\\DEFAULT.BIN"));
				break;
			}
			return 0;
		case CONF_PALETTE:
			switch (systype) {
//			case SYSTEM_7:
//			case SYSTEM_9:
//			case SYSTEM_1:
			case SYSTEM_A:
			case SYSTEM_P:
			case SYSTEM_E:
			case SYSTEM_EE:
			case SYSTEM_82:
			case SYSTEM_8A:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("PALETTE\\APPLE2.PAL"));
				return 0;
			default:
				_tcscpy(c->cfgstr[CFG_STR_ROM], TEXT("PALETTE\\DEFAULT.PAL"));
				return 0;
			}
			return 0;
		}
	}
	return 0;
}


int format_scsi_comment(TCHAR*buf, int len, struct SLOTCONFIG*c)
{
	int nd = 0;
	TCHAR lbuf[256];
	if (c->cfgint[CFG_INT_SCSI_NO1]) ++nd;
	if (c->cfgint[CFG_INT_SCSI_NO2]) ++nd;
	if (c->cfgint[CFG_INT_SCSI_NO3]) ++nd;
	localize_str(LOC_SCSI, 20 + nd, buf, len * sizeof(buf[0]));
	localize_str(LOC_SCSI, 30, lbuf, sizeof(lbuf));
	lstrcat(buf, lbuf);
	lstrcat(buf, c->cfgstr[CFG_STR_DRV_ROM]);
	return 0;
}

int get_slot_comment(struct SLOTCONFIG*c, TCHAR*buf)
{
	TCHAR lbuf[256];
	buf[0] = 0;

	switch (c->slot_no) {
	case CONF_SOUND:
		if (c->dev_type == DEV_MMSYSTEM || c->dev_type == DEV_DSOUND) {
			wsprintf(buf, 
				localize_str(LOC_SYSCONF, 500, lbuf, sizeof(lbuf)), //TEXT("%i ��; %i �������"), 
				c->cfgint[CFG_INT_SOUND_FREQ], c->cfgint[CFG_INT_SOUND_BUFSIZE]);
		}
		return 0;
	}

	switch (c->dev_type) {
	case DEV_MEMORY_PSROM7:
	case DEV_MEMORY_XRAM7:
	case DEV_MEMORY_XRAM9:
	case DEV_MEMORY_XRAMA:
	case DEV_MEMORY_SATURN:
		_tcscpy(buf, get_memsizes_s(c->cfgint[CFG_INT_MEM_SIZE]));
		return 0;
	case DEV_A2RAMCARD:
	case DEV_RAMFACTOR:
		_tcscpy(buf, get_memsizes_s(c->cfgint[CFG_INT_MEM_SIZE]));
		_tcscat(buf, TEXT("; "));
		_tcscat(buf, c->cfgstr[CFG_STR_ROM]);
		if (c->cfgstr[CFG_STR_RAM][0]) {
			_tcscat(buf, TEXT("; "));
			_tcscat(buf, c->cfgstr[CFG_STR_RAM]);
		}
		return 0;
	case DEV_6502:
	case DEV_65C02:
	case DEV_M6502:
		wsprintf(buf, 
			localize_str(LOC_SYSCONF, 501, lbuf, sizeof(lbuf)), //TEXT("������� %i%%"), 
			c->cfgint[CFG_INT_CPU_SPEED]);
		return 0;
	case DEV_TAPE_FILE:
		if (c->cfgstr[CFG_STR_TAPE][0])
			_tcscpy(buf, c->cfgstr[CFG_STR_TAPE]);
		else
			localize_str(LOC_SYSCONF, 502, buf, sizeof(buf));
		wsprintf(buf + _tcslen(buf), 
			localize_str(LOC_SYSCONF, 503, lbuf, sizeof(lbuf)), //TEXT(", %i ��"), 
			c->cfgint[CFG_INT_TAPE_FREQ]);
		return 0;
	case DEV_SYSTEM:
		switch (c->slot_no) {
		case CONF_MEMORY:
			_tcscpy(buf, get_memsizes_s(c->cfgint[CFG_INT_MEM_SIZE]));
			return 0;
		case CONF_ROM:
			_tcscpy(buf, c->cfgstr[CFG_STR_ROM]);
			return 0;
		case CONF_CHARSET:
			_tcscpy(buf, c->cfgstr[CFG_STR_ROM]);
			return 0;
		case CONF_KEYBOARD:
			_tcscpy(buf, c->cfgstr[CFG_STR_ROM]);
			return 0;
		case CONF_PALETTE:
			_tcscpy(buf, c->cfgstr[CFG_STR_ROM]);
			return 0;
		}
		break;
	case DEV_VIDEOTERM:
		_tcscpy(buf, c->cfgstr[CFG_STR_ROM]);
		_tcscat(buf, TEXT("; "));
		_tcscat(buf, c->cfgstr[CFG_STR_ROM2]);
		if (c->cfgstr[CFG_STR_ROM3]) {
			_tcscat(buf, TEXT("; "));
			_tcscat(buf, c->cfgstr[CFG_STR_ROM3]);
		}
		return 0;
	case DEV_THUNDERCLOCK:
		_tcscpy(buf, c->cfgstr[CFG_STR_ROM]);
		return 0;
	case DEV_ACI:
		_tcscpy(buf, c->cfgstr[CFG_STR_ROM]);
		return 0;
	case DEV_FIRMWARE:
		_tcscpy(buf, c->cfgstr[CFG_STR_ROM]);
		if (c->cfgint[CFG_INT_ROM_FLAGS] & CFG_INT_ROM_FLAG_ACTIVE) {
			_tcscat(buf, TEXT("; ACTIVE"));
		}
		if (c->cfgint[CFG_INT_ROM_FLAGS] & CFG_INT_ROM_FLAG_F8MOD) {
			_tcscat(buf, TEXT("; F8MOD"));
			if (c->cfgint[CFG_INT_ROM_FLAGS] & CFG_INT_ROM_FLAG_F8ACTIVE) {
				_tcscat(buf, TEXT("; F8ACTIVE"));
			}
		}
		return 0;
	case DEV_MOCKINGBOARD:
		buf[0] = '\x97';
		buf[1] = 0;
		return 0;
	case DEV_MOUSE_PAR:
		localize_str(LOC_MOUSE, c->cfgint[CFG_INT_MOUSE_TYPE] + 10, buf, 1024);
		_tcscat(buf, TEXT("; "));
		_tcscat(buf, c->cfgstr[CFG_STR_ROM]);
		return 0;
	case DEV_MOUSE_NIPPEL: // no parameters
		return 0;
	case DEV_MOUSE_APPLE:
		_tcscpy(buf, c->cfgstr[CFG_STR_ROM]);
		return 0;
	case DEV_PRINTER9:
		localize_str(LOC_PRINTER, c->cfgint[CFG_INT_PRINT_MODE], buf, 1024);
		_tcscat(buf, TEXT("; "));
		_tcscat(buf, c->cfgstr[CFG_STR_ROM]);
		return 0;
	case DEV_PRINTERA:
		localize_str(LOC_PRINTER, c->cfgint[CFG_INT_PRINT_MODE], buf, 1024);
		_tcscat(buf, TEXT("; "));
		_tcscat(buf, c->cfgstr[CFG_STR_ROM]);
		return 0;
	case DEV_PRINTER_ATOM:
		localize_str(LOC_PRINTER, c->cfgint[CFG_INT_PRINT_MODE], buf, 1024);
		return 0;
	case DEV_NIPPELCLOCK:
		buf[0] = '\x97';
		buf[1] = 0;
		return 0;
	case DEV_FDD_ATOM:
	case DEV_FDD_LIBERTY:
		wsprintf(buf,
			TEXT("%s; %s"),
			c->cfgstr[CFG_STR_DRV_IMAGE1],
			c->cfgstr[CFG_STR_DRV_ROM]);
		return 0;
	case DEV_EXTROM_ATOM:
		_tcscpy(buf, c->cfgstr[CFG_STR_ROM]);
		return 0;
	case DEV_FDD_TEAC:
	case DEV_FDD_SHUGART:
		switch (c->cfgint[CFG_INT_DRV_COUNT]) {
		case 0:
			localize_str(LOC_SYSCONF, 510, buf, sizeof(lbuf));
//			_tcscpy(buf, TEXT("���������� �����������"));
			break;
		case 1:
			if (c->cfgint[CFG_INT_DRV_TYPE1]!=DRV_TYPE_NONE) 
				wsprintf(buf, 
					localize_str(LOC_SYSCONF, 511, lbuf, sizeof(lbuf)), //TEXT("%s: %s"), 
					get_drvtypenames(c->cfgint[CFG_INT_DRV_TYPE1]),
					c->cfgstr[CFG_STR_DRV_IMAGE1]);
			else
				wsprintf(buf,
					localize_str(LOC_SYSCONF, 512, lbuf, sizeof(lbuf)), //TEXT("���; %s: %s"), 
					get_drvtypenames(c->cfgint[CFG_INT_DRV_TYPE2]),
					c->cfgstr[CFG_STR_DRV_IMAGE2]);
			break;
		case 2:
			wsprintf(buf,
				localize_str(LOC_SYSCONF, 513, lbuf, sizeof(lbuf)), //TEXT("%s: %s; %s: %s"),
				get_drvtypenames(c->cfgint[CFG_INT_DRV_TYPE1]),
				c->cfgstr[CFG_STR_DRV_IMAGE1],
				get_drvtypenames(c->cfgint[CFG_INT_DRV_TYPE2]),
				c->cfgstr[CFG_STR_DRV_IMAGE2]);
			break;
		}
		return 0;
	case DEV_SCSI_CMS:
		return format_scsi_comment(buf, 256, c);
	case DEV_TTYA1:
		wsprintf(buf, 
			localize_str(LOC_VIDEO, (c->cfgint[CFG_INT_TTY_SPEED]==500)?402:400, lbuf, sizeof(lbuf)),
			c->cfgint[CFG_INT_TTY_SPEED]);
		if (c->cfgint[CFG_INT_TTY_FLAGS] & CFG_INT_TTY_FLAG_CLEAR) {
			_tcscat(buf, localize_str(LOC_VIDEO, 401, lbuf, sizeof(lbuf)));
		}
		return 0;
	}
	return 0;
}

int save_config(struct SYSCONFIG*c, OSTREAM*out)
{
	int r;
	DWORD h = MAKELONG(sizeof(*c), MAKEWORD(1,0));
	r = oswrite(out, &h, sizeof(h));
	if (r != sizeof(h)) return -1;
	r = oswrite(out, c, sizeof(*c));
	if (r!=sizeof(*c)) return -1;
	if (c->icon.image && c->icon.imglen) {
		r = oswrite(out, c->icon.image, c->icon.imglen);
		if (r!=c->icon.imglen) return -1;
	}
	return 0;
}


int load_config(struct SYSCONFIG*c, ISTREAM*in)
{
	int r, nconf = NCONFTYPES;
	DWORD h = MAKELONG(sizeof(*c), MAKEWORD(1,0)), h1;
	r = isread(in, &h1, sizeof(h1));
	if (r != sizeof(h1)) return -1;
	if (h != h1) {
		if (HIWORD(h1) == MAKEWORD(0,8)) { // previous version
			nconf = CONF_KEYBOARD;
		} else return -2; // invalid version
	}
	if (LOWORD(h1) > sizeof(*c)) return -2; // invalid config size
	isread(in, &c->systype, sizeof(c->systype));
	if (r != sizeof(c->systype)) return -1;
	r = isread(in, c->slots, LOWORD(h1) - sizeof(c->systype) - sizeof(c->icon));
	if (r != LOWORD(h1) - sizeof(c->systype) - sizeof(c->icon)) return -1;
	r = isread(in, &c->icon, sizeof(c->icon));
	if (r != sizeof(c->icon)) return -1;
	if (nconf != NCONFTYPES) {
		int i;
		for (i = nconf; i < NCONFTYPES; ++i) {
			c->slots[i].slot_no = i;
			reset_slot_config(c->slots + i, DEV_SYSTEM, c->systype);
		}
	}
	if (c->icon.image && c->icon.imglen) {
		c->icon.image = malloc(c->icon.imglen);
		if (!c->icon.image) return -1;
		r = isread(in, c->icon.image, c->icon.imglen);
		if (r!=c->icon.imglen) return -1;
	}
	return 0;
}


/* r1297: иконки систем в прошивке не нужны — GDI (BITMAPINFO/CreateDIBitmap/
   GetDIBits) отсутствует, эти три функции вызывает только GUI. */
#if 0
HBITMAP sysicon_to_bitmap(HDC dc, struct SYSICON*icon)
{
	BITMAPINFOHEADER hdr;
	BITMAPINFO bi;

	if (!icon->image || !icon->imglen) return NULL;

	ZeroMemory(&hdr, sizeof(hdr));
	hdr.biSize = sizeof(hdr);
	hdr.biWidth = icon->w;
	hdr.biHeight = icon->h;
	hdr.biPlanes = 1;
	hdr.biBitCount = 24;
	hdr.biCompression = BI_RGB;

	bi.bmiHeader = hdr;
	return CreateDIBitmap(dc, &hdr, CBM_INIT, icon->image, &bi, DIB_RGB_COLORS);
}


HICON sysicon_to_icon(HDC dc, struct SYSICON*icon)
{
	static const BYTE zmem[64*64/8];
	ICONINFO iinf = { TRUE, 0, 0, CreateBitmap(icon->w, icon->h, 1, 1, zmem), sysicon_to_bitmap(dc, icon) };
	HICON res;
	res = CreateIconIndirect(&iinf);
	DeleteObject(iinf.hbmMask);
	DeleteObject(iinf.hbmColor);
	return res;
}

int bitmap_to_sysicon(HDC dc, HBITMAP hbm, struct SYSICON*icon)
{
	BITMAPINFO bi;
	BITMAP bmp;
	int r;

	r = GetObject(hbm, sizeof(bmp), &bmp);

	if (r!=sizeof(bmp)) return -1;
	icon->w = bmp.bmWidth;
	icon->h = bmp.bmHeight;
	icon->imglen = icon->h * ((icon->w * 3 + 3) &~3);
	icon->image = malloc(icon->imglen);
	if (!icon->image) return -1;

	ZeroMemory(&bi, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = icon->w;
	bi.bmiHeader.biHeight = icon->h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 24;
	bi.bmiHeader.biCompression = BI_RGB;

	r = GetDIBits(dc, hbm, 0, icon->h, icon->image, &bi, DIB_RGB_COLORS);
	if (!r) {
		free(icon->image);
		icon->image = NULL;
		return -1;
	}

	return 0;
}
#endif /* r1297: GDI-иконки */

int free_sysicon(struct SYSICON*icon)
{
	if (icon->image) {
		free(icon->image);
		icon->image = NULL;
	}
	return 0;
}

LPCTSTR get_memsizes_s(int n)
{
	static TCHAR bufs[NMEMSIZES][256];
	if (n < 0 || n >= NMEMSIZES) return NULL;
	return localize_str(LOC_SYSCONF, n, bufs[n], sizeof(bufs[n]));
}


LPCTSTR get_devnames(int n)
{
	static TCHAR bufs[NDEVTYPES][256];
	if (n < 0 || n >= NDEVTYPES) return NULL;
	return localize_str(LOC_SYSCONF, n + 100, bufs[n], sizeof(bufs[n]));
}



LPCTSTR get_confnames(int n)
{
	static TCHAR bufs[NCONFTYPES][256];
	if (n < 0 || n >= NCONFTYPES) return NULL;
	return localize_str(LOC_SYSCONF, n + 200, bufs[n], sizeof(bufs[n]));
}



LPCTSTR get_sysnames(int n)
{
	static TCHAR bufs[NSYSTYPES][256];
	if (n < 0 || n >= NSYSTYPES) return NULL;
	return localize_str(LOC_SYSCONF, n + 300, bufs[n], sizeof(bufs[n]));
}


LPCTSTR get_drvtypenames(int n)
{
	static TCHAR bufs[DRV_N_TYPES][256];
	if (n < 0 || n >= DRV_N_TYPES) return NULL;
	return localize_str(LOC_SYSCONF, n + 400, bufs[n], sizeof(bufs[n]));
}

