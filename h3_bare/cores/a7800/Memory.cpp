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
// Memory.cpp
// ----------------------------------------------------------------------------
#include "Memory.h"

byte *memory_ram = NULL;   /* writable RAM (16K), from the shared pool */
const byte *memory_slot_ptr[MEMORY_SLOTS] = {0};
byte memory_slot_is_rom[MEMORY_SLOTS] = {0};

/* ----------------------------------------------------------------------------
 * SetMemory
 * Point the 64K RAM image at the shared NES-buffer pool. Called once per
 * game launch before prosystem_Reset().
 * ------------------------------------------------------------------------- */
void a7800_set_memory(byte *base) {
  memory_ram = base;
}

// ----------------------------------------------------------------------------
// Reset
// ----------------------------------------------------------------------------
void memory_Reset( ) {
  uint index;
  /* Only the low 16K (0x0000..0x3FFF) is writable RAM here; the 64K
   * memory_ram is a short buffer (see a7800 system layer). ROM above
   * 0x4000 lives in flash (XIP) via the slot map. */
  for(index = 0; index < 0x4000; index++) {
    memory_ram[index] = 0;
  }
  for(index = 0; index < MEMORY_SLOTS; index++) {
    memory_slot_ptr[index] = NULL;
    memory_slot_is_rom[index] = 0;
  }
}

// ----------------------------------------------------------------------------
// Read (implemented as static inline in Memory.h for the hot paths)
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Write
// ----------------------------------------------------------------------------
void memory_Write(word address, byte data) {
  if(address >= 0x4000) {
    cartridge_Write(address, data);
    return;
  }

  switch(address) {
    case WSYNC:
      if(!(cartridge_flags & 128)) {
        memory_ram[WSYNC] = true;
      }
      break;
    case INPTCTRL:
      if(data == 22 && cartridge_IsLoaded( )) { 
        cartridge_Store( ); 
      }
      else if(data == 2 && bios_enabled) {
        bios_Store( );
      }
      break;
    case INPT0:
      break;
    case INPT1:
      break;
    case INPT2:
      break;
    case INPT3:
      break;
    case INPT4:
      break;
    case INPT5:
      break;
    case AUDC0:
      tia_SetRegister(AUDC0, data);
      break;
    case AUDC1:
      tia_SetRegister(AUDC1, data);
      break;
    case AUDF0:
      tia_SetRegister(AUDF0, data);
      break;
    case AUDF1:
      tia_SetRegister(AUDF1, data);
      break;
    case AUDV0:
      tia_SetRegister(AUDV0, data);
      break;
    case AUDV1:
      tia_SetRegister(AUDV1, data);
      break;
    case SWCHB:
      break;
    case CTLSWB:
      break;
    case TIM1T:
    case TIM1T | 0x8:
      riot_SetTimer(TIM1T, data);
      break;
    case TIM8T:
    case TIM8T | 0x8:
      riot_SetTimer(TIM8T, data);
      break;
    case TIM64T:
    case TIM64T | 0x8:
      riot_SetTimer(TIM64T, data);
      break;
    case T1024T:
    case T1024T | 0x8:
      riot_SetTimer(T1024T, data);
      break;
    default:
      memory_ram[address] = data;
      if(address >= 8256 && address <= 8447) {
        memory_ram[address - 8192] = data;
      }
      else if(address >= 8512 && address <= 8702) {
        memory_ram[address - 8192] = data;
      }
      else if(address >= 64 && address <= 255) {
        memory_ram[address + 8192] = data;
      }
      else if(address >= 320 && address <= 511) {
        memory_ram[address + 8192] = data;
      }
      break;
  }
}

// ----------------------------------------------------------------------------
// WriteROM
// Map `size` bytes starting at `data` (in flash XIP) onto the 8KB slot map.
// Each affected 8KB slot keeps a pointer to its own start inside `data`, so
// no copy into RAM is needed. `data` must point into the cartridge image.
// ----------------------------------------------------------------------------
void memory_WriteROM(word address, word size, const byte* data) {
  if(data == NULL || ((uint)address + size) > MEMORY_SIZE) return;
  uint end = (uint)address + size;
  int slot = address >> 13;
  int last = (int)((end - 1) >> 13);
  for(; slot <= last; slot++) {
    uint slot_start = slot * MEMORY_SLOT_SIZE;
    uint slot_end   = slot_start + MEMORY_SLOT_SIZE;
    uint a0 = ((uint)address > slot_start) ? address : slot_start;
    uint a1 = (end < slot_end) ? end : slot_end;
    memory_slot_ptr[slot] = data + (a0 - address);
    memory_slot_is_rom[slot] = 1;
  }
}

// ----------------------------------------------------------------------------
// ClearROM
// ----------------------------------------------------------------------------
void memory_ClearROM(word address, word size) {
  if(((uint)address + size) > MEMORY_SIZE) return;
  uint end = (uint)address + size;
  int slot = address >> 13;
  int last = (int)((end - 1) >> 13);
  for(; slot <= last; slot++) {
    memory_slot_ptr[slot] = NULL;
    memory_slot_is_rom[slot] = 0;
  }
}
