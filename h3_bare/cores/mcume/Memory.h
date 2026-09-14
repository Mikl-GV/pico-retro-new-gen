/*****************************************************************************

   This file is part of x2600, the Atari 2600 Emulator
   ===================================================
   
   Copyright 1996 Alex Hornby. For contributions see the file CREDITS.

   This software is distributed under the terms of the GNU General Public
   License. This is free software with ABSOLUTELY NO WARRANTY.
   
   See the file COPYING for details.
   
   $Id: memory.h,v 1.5 1996/11/24 16:55:40 ahornby Exp $
******************************************************************************/

/*
  Prototypes for the memory interface.
  */

#ifndef VCSMEMORY_H
#define VCSMEMORY_H

/* pico-retro: undecRead() is the opcode fetch on every 6507 instruction —
 * out-of-line it costs real time on the RP2040. Inlined here (Cpu.c includes
 * types.h + vmachine.h before memory.h, so BYTE/ADDRESS/theRom/theRam are in
 * scope). Behaviour identical to the former Memory.c implementation. */
extern BYTE *theRom;
extern BYTE theRam[];

static __inline BYTE
undecRead (ADDRESS a)
{
  if (a & 0x1000)
    return theRom[a & 0xfff];
  else
    return theRam[a & 0x7f];
}

void 
decWrite ( ADDRESS a, BYTE b);

BYTE 
decRead (ADDRESS a);

#endif
