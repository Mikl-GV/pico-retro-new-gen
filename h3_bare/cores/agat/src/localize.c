/*
	Agat Emulator version 1.19
	Copyright (c) NOP, nnop@newmail.ru
*/

#include <windows.h>
#include "localize.h"

#define LANG_DIR 	"lang"

static HMODULE lang, def_lang;
static int need_free;

int	localize_init()
{
	lang = def_lang = GetModuleHandle(NULL);
	need_free = 0;
	return 0;
}

static void cleanup_lib()
{
	if (lang && need_free) {
		FreeLibrary(lang);
	}
}

int	localize_set_lang(LPCTSTR l)
{
	TCHAR buf[_MAX_PATH];

	if (!l || !l[0]) {
		return localize_set_lib(GetModuleHandle(NULL));
	}

	cleanup_lib();

	wsprintf(buf, TEXT(LANG_DIR)TEXT("\\%s.dll"), l);
	lang = LoadLibrary(buf);
	if (!lang) {
		localize_set_lib(GetModuleHandle(NULL));
		return -1;
	}	

	return 0;
}

int	localize_set_lib(HMODULE lib)
{
	cleanup_lib();
	lang = lib;
	need_free = 0;
	return 0;
}

void	localize_term()
{
	cleanup_lib();
}


static LPCTSTR get_string(HMODULE lang, int modid, int strid, LPTSTR buf, int bufsize)
{
	int r;
	if (!lang) return NULL;
	memset(buf, 0, bufsize);
	r = LoadString(lang, strid + modid * N_MODULE_LANG_STR_ID + BASE_LANG_STR_ID, buf, bufsize / sizeof(buf[0]));
	return r?buf:NULL;
}

LPCTSTR localize_str(int modid, int strid, LPTSTR buf, int bufsize)
{
	LPCTSTR res;
	res = get_string(lang, modid, strid, buf, bufsize);
	if (!res) res = get_string(def_lang, modid, strid, buf, bufsize);
	return res;
}

HMODULE localize_get_lib()
{
	return lang;
}

HMODULE localize_get_def_lib()
{
	return def_lang;
}
