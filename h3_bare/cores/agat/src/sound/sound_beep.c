/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	sound_beep - direct access to beeper
*/

#include "soundint.h"

struct BEEP_DATA
{
	long last_tick;
	LARGE_INTEGER last_perfcnt;
	LARGE_INTEGER perffreq;
	int	enabled;
};

static void*sound_init(struct SOUNDPARAMS*par)
{
	struct BEEP_DATA*p;
	p = calloc(sizeof(*p), 1);
	if (!p) return NULL;
	QueryPerformanceFrequency(&p->perffreq);
	p->enabled = 1;

	_try {
		__asm in  al,0x61
	} _except (EXCEPTION_EXECUTE_HANDLER) {
		HANDLE h;
		h = CreateFile("\\\\.\\giveio", GENERIC_READ, 0, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if(h == INVALID_HANDLE_VALUE) { p->enabled = 0; return p; }
		CloseHandle(h);
	}
	_try {
		__asm in  al,0x61
	} _except (EXCEPTION_EXECUTE_HANDLER) {
		p->enabled = 0;
		return p;
	}
	return p;
}

static void sound_term(void*p)
{
	free(p);
}

static int sound_data(struct BEEP_DATA*p, int d, long t, long f)
{
	LARGE_INTEGER perfcnt;

	if (!p->enabled) return -1;

	QueryPerformanceCounter(&perfcnt);

	if (p->last_tick) {
		while ((t - p->last_tick) / 1000.0 / f > 
			(perfcnt.QuadPart - p->last_perfcnt.QuadPart) / (double) p->perffreq.QuadPart) {
			QueryPerformanceCounter(&perfcnt);
		}
	}

	p->last_perfcnt = perfcnt;
	p->last_tick = t;

	if (d == SOUND_TOGGLE) {
		_try {
			__asm {
				in   al,0x61
				xor  al,2
				out  0x61,al
			}
		}
		_except (EXCEPTION_EXECUTE_HANDLER) {
			return -1;
		}
	} else {
		return -1;
	}
	return 0;
}

struct SOUNDPROC soundbeep={ sound_init, sound_term, sound_data };
