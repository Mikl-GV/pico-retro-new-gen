// ----------------------------------------------------------------------------
//   ___  ___  ___  ___       ___  ____  ___  _  _
//  /__/ /__/ /  / /__  /__/ /__    /   /_   / |/ /
// /    / \  /__/ ___/ ___/ ___/   /   /__  /    /  emulator
//
// ----------------------------------------------------------------------------
// Copyright 2005 Greg Stanton
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
// ----------------------------------------------------------------------------
// Maria.h
// ----------------------------------------------------------------------------
#ifndef MARIA_H
#define MARIA_H

#include "Equates.h"
#include "Pair.h"
#include "Memory.h"
#include "Rect.h"
#include "Sally.h"

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned int uint;

extern void maria_Reset( );
extern uint maria_RenderScanline( );
extern void maria_Clear( );
extern rect maria_displayArea;
extern rect maria_visibleArea;
/* pico-retro: no full MARIA surface (would cost 91KB of RAM). Each rendered
 * line is pushed straight to the LCD by the render callback below. */
extern void maria_LineReady(const byte *line, int length);
extern word maria_scanline;

#endif