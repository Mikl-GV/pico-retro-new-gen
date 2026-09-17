//---------------------------------------------------------------------------
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version. See also the license.txt file for
//	additional informations.
//---------------------------------------------------------------------------

// sound.cpp for bare-metal H3
//
// All sound entry points (initSound, soundCleanup, soundStep, soundOutput,
// ngpSoundStart/Execute/Off/Interrupt, osd_*) are stubbed in ngp_host.cpp.
// The NGP chip audio logic lives in neopopsound.cpp.

#ifndef __GP32__
#include "StdAfx.h"
#endif
#include "main.h"
#include "sound.h"
#include "memory.h"