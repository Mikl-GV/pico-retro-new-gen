#ifndef SYSCONF_H
#define SYSCONF_H

#define SYSTEMS_DIR	"systems"
#define SAVES_DIR 	"saves"
#define PRNOUT_DIR 	"print"
#define HELP_DIR 	"help"
#define TAPES_DIR 	"tapes"
#define TEXTS_DIR 	"texts"

#include <windows.h>
#include <tchar.h>


#include "streams.h"
#include "runmgr.h"

#define DEFSYSICON_W 32
#define DEFSYSICON_H 32

enum {
	SYSTEM_7,
	SYSTEM_9,
	SYSTEM_A, // original apple 2
	SYSTEM_P, // apple 2 plus
	SYSTEM_E, // apple 2e
	SYSTEM_1, // apple 1
	SYSTEM_EE, // apple 2e enhanced
	SYSTEM_82, // pravetz 82
	SYSTEM_8A, // pravetz 8A
	SYSTEM_AA, // acorn atom

	NSYSTYPES
};


enum {
	DEV_NULL,
	DEV_MEMORY_PSROM7,
	DEV_MEMORY_XRAM7,
	DEV_MEMORY_XRAM9,
	DEV_MEMORY_XRAMA,
	DEV_FDD_TEAC,
	DEV_FDD_SHUGART,

	DEV_SYSTEM,
	DEV_NOJOY,
	DEV_MOUSE,
	DEV_JOYSTICK,
	DEV_MMSYSTEM,
	DEV_DSOUND,
	DEV_BEEPER,

	DEV_COLOR,
	DEV_MONO,

	DEV_6502,
	DEV_M6502,

	DEV_TAPE_NONE,
	DEV_TAPE_FILE,

	DEV_NOSOUND,

	DEV_SOFTCARD,

	DEV_65C02,
	DEV_VIDEOTERM,
	DEV_THUNDERCLOCK,
	
	DEV_PRINTER9,
	DEV_MOCKINGBOARD,

	DEV_NIPPELCLOCK,
	DEV_PRINTERA,

	DEV_A2RAMCARD,
	DEV_RAMFACTOR,
	DEV_MEMORY_SATURN,

	DEV_MOUSE_PAR,
	DEV_MOUSE_NIPPEL,
	DEV_MOUSE_APPLE,

	DEV_ACI, // cassette interface for Apple I

	DEV_SCSI_CMS,

	DEV_CHARGEN2,

	DEV_FIRMWARE, // firmware card for apple2
	DEV_TTYA1, // terminal for Apple I
	
	DEV_PRINTER_ATOM, // printer interface for Acorn Atom
	DEV_FDD_ATOM, // floppy card for Acorn Atom
	DEV_EXTROM_ATOM, // extension rom
	DEV_EXTRAM_ATOM, // extension ram

	DEV_FDD_LIBERTY, // Liberty drive
	DEV_CLOCK_DALLAS, // Non-slot-clock

	NDEVTYPES
};


enum {
	CONF_LIST_START = 0,
	CONF_SLOT0 = CONF_LIST_START,
	CONF_SLOT1,
	CONF_SLOT2,
	CONF_SLOT3,
	CONF_SLOT4,
	CONF_SLOT5,
	CONF_SLOT6,
	CONF_SLOT7,

	CONF_CLOCK = 8,
	CONF_PRINTER = 9,
	CONF_EXT = 10,
	CONF_CPU = 10,
	CONF_MEMORY,
	CONF_ROM,
	CONF_CHARSET,
	CONF_SOUND,
	CONF_JOYSTICK,
	CONF_MONITOR,
	CONF_TAPE,
	CONF_KEYBOARD,
	CONF_PALETTE,

	NCONFTYPES
};


/* r1297: extern — иначе каждый .c с этим заголовком создаёт common-символ
   (tentative definition) и linkcheck видит межобъектный дубль. */
extern char conf_present[NSYSTYPES][NCONFTYPES];

#define CFGSTRLEN 256
#define CFGNSTRINGS 5
#define CFGNINT 10

struct SLOTCONFIG
{
	int  slot_no;
	int  dev_type;
	char cfgstr[CFGNSTRINGS][CFGSTRLEN];
	int  cfgint[CFGNINT];
};

struct SYSICON
{
	void*image;
	int imglen;
	int w, h;
};

struct SYSCONFIG
{
	int systype;
	struct SLOTCONFIG slots[NCONFTYPES];

	struct SYSICON icon;
};


#define NMEMSIZES 21
LPCTSTR get_memsizes_s(int n);
/* r1327: было `const unsigned memsizes_b[];` — это ОПРЕДЕЛЕНИЕ, а не
   объявление: каждый .c, включающий sysconf.h, создавал свой пустой
   массив (tentative definition), и при линковке с
   --allow-multiple-definition побеждала 4-байтовая копия из baseram.o.
   Итог: memsizes_b[7] читал мусор → ram_size=0 у Агат-9 → запись
   «в RAM» затирала соседний буфер ПЗУ. Нужен extern. */
extern const unsigned memsizes_b[];

LPCTSTR get_devnames(int n);
LPCTSTR get_confnames(int n);
LPCTSTR get_sysnames(int n);
LPCTSTR get_drvtypenames(int n);

int clear_config(struct SYSCONFIG*c);
int reset_config(struct SYSCONFIG*c, int systype);
int reset_slot_config(struct SLOTCONFIG*c, int devtype, int systype);
int free_config(struct SYSCONFIG*c);


int get_slot_comment(struct SLOTCONFIG*c, TCHAR*buf);

int save_config(struct SYSCONFIG*c, OSTREAM*out);
int load_config(struct SYSCONFIG*c, ISTREAM*in);

HBITMAP sysicon_to_bitmap(HDC dc, struct SYSICON*icon);
HICON sysicon_to_icon(HDC dc, struct SYSICON*icon);
int bitmap_to_sysicon(HDC dc, HBITMAP hbm, struct SYSICON*icon);
int free_sysicon(struct SYSICON*icon);

enum {
	CFG_INT_MEM_SIZE,
	CFG_INT_MEM_MASK
};

enum {
	DRV_TYPE_NONE,
	DRV_TYPE_SHUGART,
	DRV_TYPE_1S1D,
	DRV_TYPE_1S2D,
	DRV_TYPE_2S1D,
	DRV_TYPE_2S2D,

	DRV_N_TYPES
};

enum {
	PRINT_RAW,
	PRINT_TEXT,
	PRINT_TIFF,
	PRINT_PRINT,
	PRINT_LPT,
};

enum {
	MOUSE_NONE,
	MOUSE_MM8031,
	MOUSE_MARS
};

enum {
	CFG_INT_DRV_TYPE1,
	CFG_INT_DRV_TYPE2,
	CFG_INT_DRV_COUNT,
	CFG_INT_DRV_RO_FLAGS,
	CFG_INT_DRV_ROM_RES,
	CFG_INT_DRV_FAST,
};

enum {
	CFG_STR_DRV_ROM,
	CFG_STR_DRV_IMAGE1,
	CFG_STR_DRV_IMAGE2,
};

enum {
	CFG_INT_CPU_SPEED,
	CFG_INT_CPU_EXT
};

enum {
	CFG_STR_ROM,
	CFG_STR_ROM2,
	CFG_STR_ROM3,
	CFG_STR_RAM
};

enum {
	CFG_INT_ROM_RES,
	CFG_INT_ROM_SIZE,
	CFG_INT_ROM_MASK,
	CFG_INT_ROM_OFS,
	CFG_INT_ROM_FLAGS,
};

#define CFG_INT_ROM_FLAG_ACTIVE		1
#define CFG_INT_ROM_FLAG_F8ACTIVE	2
#define CFG_INT_ROM_FLAG_F8MOD		0x20
#define CFG_INT_ROM_FLAG_A1		0x80
#define CFG_INT_ROM_FLAG_AATOM		0x100

enum {
	CFG_INT_TAPE_FREQ,
	CFG_INT_TAPE_FAST,
};


enum {
	CFG_INT_SOUND_FREQ,
	CFG_INT_SOUND_BUFSIZE,
};

enum {
	CFG_STR_TAPE,
};


enum {
	CFG_INT_PRINT_MODE,
};

enum {
	CFG_INT_MOUSE_TYPE,
};

enum {
	CFG_INT_TTY_SPEED,
	CFG_INT_TTY_FLAGS,
};

#define CFG_INT_TTY_FLAG_CLEAR		1 // enable "clear screen" command emulation

enum {
	CFG_INT_SCSI_NO1 = 3, // device number of drive #1
	CFG_INT_SCSI_NO2, // device number of drive #2
	CFG_INT_SCSI_NO3, // device number of drive #3
	CFG_INT_SCSI_SZ1, // number of blocks for drive #1
	CFG_INT_SCSI_SZ2, // number of blocks for drive #2
	CFG_INT_SCSI_SZ3, // number of blocks for drive #3
	CFG_INT_SCSI_FAST
};

enum {
	CFG_STR_SCSI_NAME1 = 2, // image file name of drive #1
	CFG_STR_SCSI_NAME2, // image file name of drive #2
	CFG_STR_SCSI_NAME3 // image file name of drive #3
};

#endif //SYSCONF_H
