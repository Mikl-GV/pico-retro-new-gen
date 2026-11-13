#ifndef SOUNDINT_H
#define SOUNDINT_H

#include "types.h"
#include "memory.h"
#include "streams.h"
#include "debug.h"
#include "sysconf.h"
#include "runmgr.h"
#include "runmgrint.h"


struct SOUNDPARAMS
{
	struct SYS_RUN_STATE*sr;
	HWND	w;
	int 	freq;
	int	buflen;
};


#define SOUND_TOGGLE (-1)
struct SOUNDPROC
{
	void* (*sound_init)(struct SOUNDPARAMS*par);
	void  (*sound_term)(void*p);
	int   (*sound_data)(void*p, int d, long t, long f);
	int   (*sound_event)(void*p, void*d, void*par);
};



#endif //SOUNDINT_H

