/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	sound_none - no sound
*/

#include "soundint.h"

static void*sound_init(struct SOUNDPARAMS*p)
{
	return (void*)1;
}

static void sound_term(void*p)
{
}

static int sound_data(void*p, int d, long t, long f)
{
	return 0;
}

struct SOUNDPROC soundnone={ sound_init, sound_term, sound_data };
