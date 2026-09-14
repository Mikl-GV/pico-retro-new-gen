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
// Maria.c
// ----------------------------------------------------------------------------
#include "Maria.h"
#define MARIA_LINERAM_SIZE 160

rect maria_displayArea = {0, 16, 319, 258};
rect maria_visibleArea = {0, 26, 319, 248};
word maria_scanline = 1;

/* pico-retro: these four are exported (not static) so the hand-written ARM
 * thumb assembly in maria_asm.S can touch them directly. The C fallback
 * (host tests / non-ARM) keeps the identical logic in maria_StoreCell(). */
byte maria_lineRAM[MARIA_LINERAM_SIZE];
static uint maria_cycles;
static pair maria_dpp;
static pair maria_dp;
static pair maria_pp;
byte maria_horizontal;
byte maria_palette;
static signed char maria_offset;
static byte maria_h08;
static byte maria_h16;
static byte maria_wmode;
byte maria_kmode;   /* cached CTRL & 4, set once per line in StoreLineRAM */

/* pico-retro: current rendered line, 320 bytes of palette indices. Filled by
 * maria_WriteLineRAM, delivered to the LCD via maria_LineReady. */
static byte maria_current_line[320];
extern void maria_LineReady(const byte *line, int length);

#if defined(__arm__) && !defined(PICO_NO_ASM) && 0
/* Hand-written Thumb for the hottest MARIA primitive (up to ~55k calls/frame
 * for heavy games like Donkey Kong). Host builds keep the C version below. */
extern "C" void maria_store_cell1_asm(byte data);
extern "C" void maria_store_cell2_asm(byte high, byte low);
#endif

// ----------------------------------------------------------------------------
// StoreCell
// ----------------------------------------------------------------------------
static void maria_StoreCell(byte data) {
#if defined(__arm__) && !defined(PICO_NO_ASM) && 0
  maria_store_cell1_asm(data);
#else
  if(maria_horizontal < MARIA_LINERAM_SIZE) {
    if(data) {
      maria_lineRAM[maria_horizontal] = maria_palette | data;
    }
    else if(maria_kmode) {
      maria_lineRAM[maria_horizontal] = 0;
    }
  }
  maria_horizontal++;
#endif
}

// ----------------------------------------------------------------------------
// StoreCell
// ----------------------------------------------------------------------------
static void maria_StoreCell(byte high, byte low) {
#if defined(__arm__) && !defined(PICO_NO_ASM) && 0
  maria_store_cell2_asm(high, low);
#else
  if(maria_horizontal < MARIA_LINERAM_SIZE) {
    if(low || high) {
      maria_lineRAM[maria_horizontal] = maria_palette & 16 | high | low;
    }
    else if(maria_kmode) {
      maria_lineRAM[maria_horizontal] = 0;
    }
  }
  maria_horizontal++;
#endif
}

// ----------------------------------------------------------------------------
// IsHolyDMA
// ----------------------------------------------------------------------------
static bool maria_IsHolyDMA( ) {
  if(maria_pp.w > 32767) {
    if(maria_h16 && (maria_pp.w & 4096)) {
      return true;
    }
    if(maria_h08 && (maria_pp.w & 2048)) {
      return true;
    }
  }
  return false;
}

// ----------------------------------------------------------------------------
// GetColor
// ----------------------------------------------------------------------------
static byte maria_GetColor(byte data) {
  if(data & 3) {
    return memory_Read(BACKGRND + data);
  }
  else {
    return memory_Read(BACKGRND);
  }
}

// ----------------------------------------------------------------------------
// StoreGraphic
// ----------------------------------------------------------------------------
static void maria_StoreGraphic( ) {
  byte data = memory_Read(maria_pp.w);
  if(maria_wmode) {
    if(maria_IsHolyDMA( )) {
      maria_StoreCell(0, 0);
      maria_StoreCell(0, 0);
    }
    else {
      maria_StoreCell((data & 12), (data & 192) >> 6);
      maria_StoreCell((data & 48) >> 4, (data & 3) << 2);
    }
  }
  else {
    if(maria_IsHolyDMA( )) {
      maria_StoreCell(0);
      maria_StoreCell(0);
      maria_StoreCell(0);
      maria_StoreCell(0);
    }
    else {
      maria_StoreCell((data & 192) >> 6);
      maria_StoreCell((data & 48) >> 4);
      maria_StoreCell((data & 12) >> 2);
      maria_StoreCell(data & 3);
    }
  }
  maria_pp.w++;
}

// ----------------------------------------------------------------------------
// WriteLineRAM
// ----------------------------------------------------------------------------
static void maria_WriteLineRAM( ) {
  byte* buffer = maria_current_line;
  byte rmode = memory_Read(CTRL) & 3;
  if(rmode == 0) {
    int pixel = 0;
    for(int index = 0; index < MARIA_LINERAM_SIZE; index += 4) {
      byte color;
      color = maria_GetColor(maria_lineRAM[index + 0]);
      buffer[pixel++] = color;
      buffer[pixel++] = color;
      color = maria_GetColor(maria_lineRAM[index + 1]);
      buffer[pixel++] = color;
      buffer[pixel++] = color;
      color = maria_GetColor(maria_lineRAM[index + 2]);
      buffer[pixel++] = color;
      buffer[pixel++] = color;
      color = maria_GetColor(maria_lineRAM[index + 3]);
      buffer[pixel++] = color;
      buffer[pixel++] = color;
    }
  }
  else if(rmode == 2) { 
    int pixel = 0;
    for(int index = 0; index < MARIA_LINERAM_SIZE; index += 4) {
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 0] & 16) | ((maria_lineRAM[index + 0] & 8) >> 3) | ((maria_lineRAM[index + 0] & 2)));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 0] & 16) | ((maria_lineRAM[index + 0] & 4) >> 2) | ((maria_lineRAM[index + 0] & 1) << 1));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 1] & 16) | ((maria_lineRAM[index + 1] & 8) >> 3) | ((maria_lineRAM[index + 1] & 2)));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 1] & 16) | ((maria_lineRAM[index + 1] & 4) >> 2) | ((maria_lineRAM[index + 1] & 1) << 1));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 2] & 16) | ((maria_lineRAM[index + 2] & 8) >> 3) | ((maria_lineRAM[index + 2] & 2)));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 2] & 16) | ((maria_lineRAM[index + 2] & 4) >> 2) | ((maria_lineRAM[index + 2] & 1) << 1));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 3] & 16) | ((maria_lineRAM[index + 3] & 8) >> 3) | ((maria_lineRAM[index + 3] & 2)));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 3] & 16) | ((maria_lineRAM[index + 3] & 4) >> 2) | ((maria_lineRAM[index + 3] & 1) << 1));
    }
  }
  else if(rmode == 3) {
    int pixel = 0;
    for(int index = 0; index < MARIA_LINERAM_SIZE; index += 4) {
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 0] & 30));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 0] & 28) | ((maria_lineRAM[index + 0] & 1) << 1));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 1] & 30));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 1] & 28) | ((maria_lineRAM[index + 1] & 1) << 1));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 2] & 30));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 2] & 28) | ((maria_lineRAM[index + 2] & 1) << 1));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 3] & 30));
      buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 3] & 28) | ((maria_lineRAM[index + 3] & 1) << 1));
    }
  }
}

// ----------------------------------------------------------------------------
// StoreLineRAM
// ----------------------------------------------------------------------------
static void maria_StoreLineRAM( ) {
  for(int index = 0; index < MARIA_LINERAM_SIZE; index++) {
    maria_lineRAM[index] = 0;
  }
  
  byte mode = memory_Read(maria_dp.w + 1);
  maria_kmode = memory_Read(CTRL) & 4;   /* constant for the whole line */
  while(mode & 0x5f) {
    byte width;
    byte indirect = 0;
 
    maria_pp.b.l = memory_Read(maria_dp.w);
    maria_pp.b.h = memory_Read(maria_dp.w + 2);
    
    if(mode & 31) { 
      maria_cycles += 8;
      maria_palette = (memory_Read(maria_dp.w + 1) & 224) >> 3;
      maria_horizontal = memory_Read(maria_dp.w + 3);
      width = memory_Read(maria_dp.w + 1) & 31;
      width = ((~width) & 31) + 1;
      maria_dp.w += 4;
    }
    else { 
      maria_cycles += 10;
      maria_palette = (memory_Read(maria_dp.w + 3) & 224) >> 3;
      maria_horizontal = memory_Read(maria_dp.w + 4);
      indirect = memory_Read(maria_dp.w + 1) & 32;
      maria_wmode = memory_Read(maria_dp.w + 1) & 128;
      width = memory_Read(maria_dp.w + 3) & 31;
      width = (width == 0)? 32: ((~width) & 31) + 1;
      maria_dp.w += 5;
    }

    if(!indirect) {
      maria_pp.b.h += maria_offset;
      for(int index = 0; index < width; index++) {
        maria_cycles += 3;
        maria_StoreGraphic( );
      }
    }
    else {
      byte cwidth = memory_Read(CTRL) & 16;
      pair basePP = maria_pp;
      for(int index = 0; index < width; index++) {
        maria_cycles += 3;
        maria_pp.b.l = memory_Read(basePP.w++);
        maria_pp.b.h = memory_Read(CHARBASE) + maria_offset;
        
        maria_cycles += 6;
        maria_StoreGraphic( );
        if(cwidth) {
          maria_cycles += 3;
          maria_StoreGraphic( );
        }
      }
    }
    mode = memory_Read(maria_dp.w + 1);
  }
}

// ----------------------------------------------------------------------------
// Reset
// ----------------------------------------------------------------------------
void maria_Reset( ) {
  maria_scanline = 1;
}

// ----------------------------------------------------------------------------
// RenderScanline
// ----------------------------------------------------------------------------
uint maria_RenderScanline( ) {
  maria_cycles = 0;
  if((memory_Read(CTRL) & 96) == 64 && maria_scanline >= maria_displayArea.top && maria_scanline <= maria_displayArea.bottom) {
    maria_cycles += 31;
    if(maria_scanline == maria_displayArea.top) {
      maria_cycles += 7;
      maria_dpp.b.l = memory_Read(DPPL);
      maria_dpp.b.h = memory_Read(DPPH);
      maria_h08 = memory_Read(maria_dpp.w) & 32;
      maria_h16 = memory_Read(maria_dpp.w) & 64;
      maria_offset = memory_Read(maria_dpp.w) & 15;
      maria_dp.b.l = memory_Read(maria_dpp.w + 2);
      maria_dp.b.h = memory_Read(maria_dpp.w + 1);
      if(memory_Read(maria_dpp.w) & 128) {
        sally_ExecuteNMI( );
      }
    }
    else if(maria_scanline >= maria_visibleArea.top && maria_scanline <= maria_visibleArea.bottom) {
      maria_WriteLineRAM( );
      maria_LineReady(maria_current_line, (int)maria_visibleArea.GetLength( ));
    }
    if(maria_scanline != maria_displayArea.bottom) {
      maria_dp.b.l = memory_Read(maria_dpp.w + 2);
      maria_dp.b.h = memory_Read(maria_dpp.w + 1);
      maria_StoreLineRAM( );
      maria_offset--;
      if(maria_offset < 0) {
        maria_dpp.w += 3;
        maria_h08 = memory_Read(maria_dpp.w) & 32;
        maria_h16 = memory_Read(maria_dpp.w) & 64;
        maria_offset = memory_Read(maria_dpp.w) & 15;
        if(memory_Read(maria_dpp.w) & 128) {
          sally_ExecuteNMI( );
        }
      }
    }    
  }
  return maria_cycles;
}

// ----------------------------------------------------------------------------
// Clear
// ----------------------------------------------------------------------------
void maria_Clear( ) {
}
