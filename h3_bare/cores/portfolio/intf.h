// intf.h — Atari Portfolio interface definitions for pico-retro.
// Memory map, CPU register macros, and cross-file function declarations.
#ifndef INTF_H
#define INTF_H

#include <stdint.h>

// Atari Portfolio memory map (from MAME pofo_asic):
//  0x00000-0x1EFFF  internal RAM (128K minus 4K VRAM tail)
//  0x1F000-0x1FFFF  VRAM (last 4K of the 128K RAM image)
//  0xB0000-0xBFFFF  mirrored VRAM window (repeats 0xB0000-0xB0FFF every 0xF000)
//  0xC0000-0xDFFFF  ROM cartridge (bank view), from embedded 128K cart image
//  0xE0000-0xFFFFF  BIOS (last 128K of the 256K ROM image)
#define RAM_SIZE   0x20000   // 128K internal RAM (incl. 4K VRAM tail)
#define NATIVE_RAM 0x20000
#define NATIVE_START 0UL

#define ROM_READ(a,b) a[b]

// CPU register indexes used by cpu.cpp (Fake86-style x86 emulation).
// These are offsets into the 'wordregs[8]' and 'segregs[4]' arrays.
// Generic registers
#define regax 0
#define regcx 1
#define regdx 2
#define regbx 3
#define regsp 4
#define regbp 5
#define regsi 6
#define regdi 7
// Segment registers
#define reges 0
#define regcs 1
#define regss 2
#define regds 3
// Byte register halves (for 8-bit operations)
#define regal 0
#define regah 1
#define regcl 2
#define regch 3
#define regdl 4
#define regdh 5
#define regbl 6
#define regbh 7

// Instruction pointer step
#define StepIP(x) ip+=x
// Memory access via segment base + offset
#define getmem8(x,y) read86(segbase(x)+(uint32_t)y)
#define getmem16(x,y) readw86(segbase(x)+(uint32_t)y)
#define putmem8(x,y,z) write86(segbase(x)+(uint32_t)y, z)
#define putmem16(x,y,z) writew86(segbase(x)+(uint32_t)y, z)
// Sign extension helpers
#define signext(value) ((((uint16_t)value&0x80)*0x1FE)|(uint16_t)value)
#define signext32(value) ((((uint32_t)value&0x8000)*0x1FFFE)|(uint32_t)value)
// Register read/write macros
#define getreg16(regid) regs.wordregs[regid]
#define getreg8(regid) regs.byteregs[byteregtable[regid]]
#define putreg16(regid, writeval) regs.wordregs[regid] = writeval
#define putreg8(regid, writeval) regs.byteregs[byteregtable[regid]] = writeval
#define getsegreg(regid) segregs[regid]
#define putsegreg(regid, writeval) segregs[regid] = writeval
#define segbase(x) ((uint32_t)x<<4)

// Pack individual flag bits into the FLAGS register word.
#define makeflagsword() (2 | (uint16_t)cf | ((uint16_t)pf << 2) | ((uint16_t)af << 4) | ((uint16_t)zf << 6) \
        | ((uint16_t)sf << 7) | ((uint16_t)tf << 8) | ((uint16_t)ifl << 9) | ((uint16_t)df << 10) | ((uint16_t)of << 11))

// Unpack FLAGS register word into individual flag variables.
#define decodeflagsword(x) {\
        temp16 = x;\
        cf = temp16 & 1;\
        pf = (temp16 >> 2) & 1;\
        af = (temp16 >> 4) & 1;\
        zf = (temp16 >> 6) & 1;\
        sf = (temp16 >> 7) & 1;\
        tf = (temp16 >> 8) & 1;\
        ifl = (temp16 >> 9) & 1;\
        df = (temp16 >> 10) & 1;\
        of = (temp16 >> 11) & 1;\
}

// ------------------------------------------------------------------
// C-linkage functions shared across cpu.cpp and system_portfolio.cpp.
// These are defined with extern "C" in their respective .cpp files.
// ------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif
uint8_t read_ram(int address);
void write_ram(int address, unsigned char val);
uint8_t VRAM_read(uint32_t addr32);
void VRAM_write(uint32_t addr32, uint8_t value);
uint8_t pofo_rom_read(uint32_t off);
uint16_t pofo_get_ip(void);
uint16_t pofo_get_cs(void);
#ifdef __cplusplus
}
#endif

// ------------------------------------------------------------------
// C++ linkage: cpu.cpp / i8253.cpp / i8259.cpp define these as C++.
// ------------------------------------------------------------------
void reset86(void);
void exec86(uint32_t execloops);
uint8_t read86(uint32_t addr32);
void write86(uint32_t addr32, uint8_t value);
void doirq(uint8_t irqnum);
uint8_t nextintr(void);
void init8253(void);
void init8259(void);
void out8253(uint16_t portnum, uint8_t value);
uint8_t in8253(uint16_t portnum);
void out8259(uint16_t portnum, uint8_t value);
uint8_t in8259(uint16_t portnum);

extern volatile uint8_t timerTick;

// Union for x86 general-purpose registers (word and byte views).
union _bytewordregs_{
  uint16_t wordregs[8];
  uint8_t byteregs[8];
};

#endif