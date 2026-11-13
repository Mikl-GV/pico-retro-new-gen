int	localize_init();
int	localize_set_lang(LPCTSTR lang);
int	localize_set_lib(HMODULE lib);
void	localize_term();

LPCTSTR localize_str(int modid, int strid, LPTSTR buf, int bufsize);

HMODULE localize_get_lib();
HMODULE localize_get_def_lib();

enum {
	LOC_GENERIC,	// 3000
	LOC_CPU,	// 4000
	LOC_FDD,	// 5000
	LOC_JOYSTICK,	// 6000
	LOC_SOUND,	// 7000
	LOC_TAPE,	// 8000
	LOC_VIDEO,	// 9000
	LOC_CFG,	// 10000
	LOC_MAIN,	// 11000
	LOC_SYSCONF,	// 12000
	LOC_PRINTER,	// 13000
	LOC_MOUSE,	// 14000
	LOC_SCSI,	// 15000

	LOC_NMODULES
};

#define BASE_LANG_STR_ID	3000
#define N_MODULE_LANG_STR_ID	1000
