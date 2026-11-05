/*
  Fake86: A portable, open-source 8086 PC emulator.
  Copyright (C)2010-2013 Mike Chambers
            (C)2020      Gabor Lenart "LGB"

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/* i8237.c: functions to emulate the Intel 8237 DMA controller.
   the Sound Blaster emulation functions rely on this! */

#include "config.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>   /* MS1504: <memory.h> нет в newlib */

#include "i8237.h"

#include "blaster.h"
#include "ports.h"
#include "cpu.h"


static struct dmachan_s dmachan[4];
static uint8_t flipflop = 0;

uint8_t read8237 (uint8_t channel) {
	uint8_t ret;
	if (dmachan[channel].masked) return 128;
	if (dmachan[channel].autoinit && (dmachan[channel].count > dmachan[channel].reload) ) dmachan[channel].count = 0;
	if (dmachan[channel].count > dmachan[channel].reload) return 128;
	//if (dmachan[channel].direction) ret = RAM[dmachan[channel].page + dmachan[channel].addr + dmachan[channel].count];
	//	else ret = RAM[dmachan[channel].page + dmachan[channel].addr - dmachan[channel].count];
	if (dmachan[channel].direction == 0) ret = RAM[dmachan[channel].page + dmachan[channel].addr + dmachan[channel].count];
	else ret = RAM[dmachan[channel].page + dmachan[channel].addr - dmachan[channel].count];
	dmachan[channel].count++;
	return ret;
}

static void out8237 (uint16_t addr, uint8_t value) {
	uint8_t channel;
#ifdef DEBUG_DMA
	printf ("out8237(0x%X, %X);\n", addr, value);
#endif
	/* r1019: полные регистры 8237 — BIOS PK300 (DMA-тест F13E-F159) пишет
	   0xFF в КАЖДЫЙ порт 0x00..0x07 (addr/count каналов 0..3) и читает назад.
	   Раньше обрабатывались только ch1 addr/count (0x02/0x03) → IN 0x00/0x01
	   возвращал 0, CMP FF не сходился → вечный цикл BL=01. */
	switch (addr) {
			case 0x0: //channel 0 address register
			case 0x2: //channel 1 address register
			case 0x4: //channel 2 address register
			case 0x6: //channel 3 address register
				channel = (addr >> 1) & 3;
				if (flipflop == 1)
					dmachan[channel].addr = (dmachan[channel].addr & 0x00FF) | ( (uint32_t) value << 8);
				else
					dmachan[channel].addr = (dmachan[channel].addr & 0xFF00) | value;
				flipflop = ~flipflop & 1;
				break;
			case 0x1: //channel 0 count register
			case 0x3: //channel 1 count register
			case 0x5: //channel 2 count register
			case 0x7: //channel 3 count register
				channel = (addr >> 1) & 3;
				if (flipflop == 1)
					dmachan[channel].reload = (dmachan[channel].reload & 0x00FF) | ( (uint32_t) value << 8);
				else
					dmachan[channel].reload = (dmachan[channel].reload & 0xFF00) | value;
				if (flipflop == 1) {
						if (dmachan[channel].reload == 0) dmachan[channel].reload = 65536;
						dmachan[channel].count = 0;
					}
				flipflop = ~flipflop & 1;
				break;
			case 0xA: //write single mask register
				channel = value & 3;
				dmachan[channel].masked = (value >> 2) & 1;
#ifdef DEBUG_DMA
				printf ("[NOTICE] DMA channel %u masking = %u\n", channel, dmachan[channel].masked);
#endif
				break;
			case 0xB: //write mode register
				channel = value & 3;
				dmachan[channel].direction = (value >> 5) & 1;
				dmachan[channel].autoinit = (value >> 4) & 1;
				dmachan[channel].writemode = (value >> 2) & 1; //not quite accurate
#ifdef DEBUG_DMA
				printf ("[NOTICE] DMA channel %u write mode reg: direction = %u, autoinit = %u, write mode = %u\n",
				        channel, dmachan[channel].direction, dmachan[channel].autoinit, dmachan[channel].writemode);
#endif
				break;
			case 0xC: //clear byte pointer flip-flop
#ifdef DEBUG_DMA
				printf ("[NOTICE] DMA cleared byte pointer flip-flop\n");
#endif
				flipflop = 0;
				break;
			case 0x81: //DMA channel 2 page register
				dmachan[2].page = (uint32_t) value << 16;
				break;
			case 0x82: //DMA channel 3 page register
				dmachan[3].page = (uint32_t) value << 16;
				break;
			case 0x83: //DMA channel 1 page register
				dmachan[1].page = (uint32_t) value << 16;
				break;
case 0x87: //DMA channel 0 page register
				dmachan[0].page = (uint32_t) value << 16;
				break;
		}
}

static uint8_t in8237 (uint16_t addr) {
#ifdef DEBUG_DMA
	printf ("in8237(0x%X);\n", addr);
#endif
	switch (addr) {
		case 0: //channel 0 address register (read back)
		case 1: //channel 0 count register
		case 2: //channel 1 address register
		case 3: //channel 1 count register
		case 4: //channel 2 address register
		case 5: //channel 2 count register
		case 6: //channel 3 address register
		case 7: //channel 3 count register
		{
			int chan = (addr >> 1) & 3;      /* 0/2/4/6 -> chan address; 1/3/5/7 -> count */
			int is_count = addr & 1;
			uint16_t v;
			if (is_count)
				v = (uint16_t)dmachan[chan].reload;
			else
				v = (uint16_t)dmachan[chan].addr;
			if (flipflop == 1)
				return (uint8_t)(v >> 8);
			return (uint8_t)(v & 0xFF);
			flipflop = ~flipflop & 1;	// this seems to be invalid, control never gets here ... :-O
			break;
		}
	}
	return 0;
}

void init8237(void) {
	memset (dmachan, 0, sizeof (dmachan) );

	set_port_write_redirector (0x00, 0x0F, &out8237);
	set_port_read_redirector (0x00, 0x0F, &in8237);

	set_port_write_redirector (0x80, 0x8F, &out8237);
	set_port_read_redirector (0x80, 0x8F, &in8237);
}
