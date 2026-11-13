#include "memory.h"
#include "keyb.h"


#define MEM_1K_SIZE		0x400

#define BASEMEM_BLOCK_SIZE	0x800
#define BASEMEM_BLOCK_SHIFT	11
#define BASEMEM_NBLOCKS		0x20

struct SYS_CONTROL_INFO
{
	void*ptr; // pointer to external information
	int (*free_system)(struct SYS_RUN_STATE*sr);
	int (*restart_system)(struct SYS_RUN_STATE*sr); // hardware restart
	int (*save_system)(struct SYS_RUN_STATE*sr, OSTREAM*out);
	int (*load_system)(struct SYS_RUN_STATE*sr, ISTREAM*in);
	int (*xio_control)(struct SYS_RUN_STATE*sr, int req); 	// req=1 - request if it is possible to acquire ROM, 
								// req=0 - restore ROM at C800.CFFF
};

struct SYS_RUN_STATE
{
	LPCTSTR name;
	int   cursystype;
	struct SYSCONFIG*config;
	struct SLOT_RUN_STATE slots[NCONFTYPES];
	struct MEM_PROC base_mem[BASEMEM_NBLOCKS];
	struct MEM_PROC baseio_sel[16];
	struct MEM_PROC io_sel[8];
	struct MEM_PROC io6_sel[0x10];

	struct MEM_PROC rom_c800;


	HWND base_w;
	SIZE v_size;
	struct KEYMAP keymap;
	int  fullscreen;
	ATOM video_at;
	HWND video_w;
	HICON video_icon;
	TCHAR title[1024];
	WINDOWPLACEMENT old_pl;
	LONG old_style;
	HMENU popup_menu;
	DWORD th;
	HANDLE h;
	HBITMAP old_bmp, mem_bmp;
	HDC mem_dc;
	int bmp_pitch;
	LPVOID bmp_bits;
	int mousechanged; // bit0 - btn1, bit1 - btn2, bit2 - btn3, bit 7 - pos
	int xmousepos, ymousepos; // restricted position
	int xmouse, ymouse; // unrestricted position
	int dxmouse, dymouse;
	int mouselock;
	int mouselocked;
	int mousebtn;
	int keyreg;
	int cur_key;
	int key_down;
	int caps_lock;
	int input_8bit, input_hbit;

	int ints_enabled;

	int pause_inactive; // pause execution when window is inactive
	int apple_emu;

	struct SYS_CONTROL_INFO sys;

	ISTREAM*input_data;
	int input_recode;
	int input_size, input_pos, input_cntr;

	void*debug_ptr;
	struct GLOBAL_CONFIG * gconfig;
	int sync_update;
	int cur_lang; // 0 - english, 1 - russian
	int key_rept;
	int in_debug;
	int keymask;
};

void io6_write(word adr, byte data, struct MEM_PROC*io6_sel); // c060-c06f
byte io6_read(word adr, struct MEM_PROC*io6_sel); // c060-c06f
void io_write(word adr, byte data, struct MEM_PROC*io_sel); // C000-C7FF
byte io_read(word adr, struct MEM_PROC*io_sel); // C000-C7FF
void baseio_write(word adr, byte data, struct MEM_PROC*baseio_sel); // C000-C0FF
byte baseio_read(word adr, struct MEM_PROC*baseio_sel);	// C000-C0FF

byte keyb_read(word adr, struct SYS_RUN_STATE*sr);	// C000-C00F
byte keyb_reg_read(word adr, struct SYS_RUN_STATE*sr);	// C063
byte keyb_apple_state(struct SYS_RUN_STATE*sr, int lr); // left or right
byte keyb_is_pressed(struct SYS_RUN_STATE*sr, int vk);
void keyb_clear(struct SYS_RUN_STATE*sr);	// C010-C01F
