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
// Memory.h
// ----------------------------------------------------------------------------
#ifndef MEMORY_H
#define MEMORY_H
#define MEMORY_SIZE 65536

#include "Equates.h"
#include "Bios.h"
#include "Cartridge.h"
#include "Tia.h"
#include "Riot.h"

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned int uint;

/* pico-retro adaptation:
 * - memory_ram[64K] is the working RAM of the 7800, taken from the shared
 *   NES-buffer pool in system_a7800.cpp.
 * - The 64K `memory_rom` flag array of the original is replaced by a compact
 *   slot map: 8 slots of 8KB each covering 0x0000..0xFFFF. A slot marked as
 *   ROM holds a pointer to the cartridge image in flash (XIP) — no copy in
 *   RAM, so 64KB of RAM are saved.
 */
#define MEMORY_SLOTS 8
#define MEMORY_SLOT_SIZE 8192
extern byte *memory_ram;
extern const byte *memory_slot_ptr[MEMORY_SLOTS];
extern byte memory_slot_is_rom[MEMORY_SLOTS];
extern void a7800_set_memory(byte *base);

extern void memory_Reset( );

/* pico-retro: inlined for the hot MARIA/6502 paths (StoreGraphic reads ROM
 * thousands of times per frame; an out-of-line call costs real time on the
 * RP2040). The < 0x4000 branch covers RAM + hardware registers, >= 0x4000
 * reads the cartridge image straight from flash (XIP) via the slot map. */
static inline byte memory_Read(word address) {
  if(address >= 0x4000) {
    const byte *p = memory_slot_ptr[address >> 13];
    if(p != NULL) return p[address & (MEMORY_SLOT_SIZE - 1)];
    return 0xFF;
  }
  switch ( address ) {
  case INTIM:
  case INTIM | 0x2:
	memory_ram[INTFLG] &= 0x7f;
    return memory_ram[INTIM];
  case INTFLG:
  case INTFLG | 0x2:
	 {
	   byte tmp_byte = memory_ram[INTFLG];
	   memory_ram[INTFLG] &= 0x7f;
	   return tmp_byte;
	 }
  default:
    return memory_ram[address];
  }
}

extern void memory_Write(word address, byte data);
extern void memory_WriteROM(word address, word size, const byte* data);
extern void memory_ClearROM(word address, word size);

#endif
