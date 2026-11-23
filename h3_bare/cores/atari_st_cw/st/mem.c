/*
 * Castaway
 *  (C) 1994 - 2002 Martin Doering, Joachim Hoenig
 *
 * $File$ - memory read/write
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * revision history
 *  23.05.2002  JH  FAST1.0.1 code import: KR -> ANSI, restructuring
 *  30.05.2002  JH  Discontinued using mmap and mprotect, now using
 *                  Martin's memory access jump table.
 *  12.06.2002  JH  Correct bus error/address error exception stack frame
 *  14.06.2002  JH  LowRamSetX() functions improved.
 *  09.07.2002  JH  Now loads any 192k ROM file
 *  10.07.2002  MAD Now loads any ROM file
 *  16.09.2002  JH  Bugfix: Word access on unmapped I/O address stacked
 *                  two bus error stack frames. Fault address corrected.
 *  08.10.2002  JH  Fixed integer types.
 *  13.05.2003  JH  Make things easier on Debian
 */
static char     sccsid[] = "$Id: mem.c,v 1.17 2003/05/13 20:38:02 jhoenig Exp $";
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>     /* r1566: memcpy для загрузки вшитого TOS */
#include "cw_host.h"    /* r1566: вшитый EmuTOS (tos_data.S) */

#include "68000.h"
#include "st.h"

#ifdef DEBUG
#include <assert.h>
#endif                          /* DEBUG */

int8		*membase, *rombase, *carbase;
char            cartridge[80] = CARTRIDGE, rom[80] = ROM;
uint32		memsize = MEMSIZE;

/*
 * Read/Write memory
 */
#ifdef WORDS_LITTLE_ENDIAN
#define ReadB(address) *((int8 *) (address))
#define ReadW(addr) ((*(uint16 *)(addr) << 8) | (*(uint16 *)(addr) >> 8))
#define ReadL(address) ((uint16) ReadW(address) << 16) | (uint16) ReadW((address) + 2)
#define WriteB(address,value) *((int8 *) (address)) = (value)
#define WriteW(addr,value) *((int16 *)(addr)) = ((((uint16)(value)) << 8) | (((uint16)(value)) >> 8))
#define WriteL(address,value) WriteW((address) + 2, value); WriteW(address, (value) >> 16)

#if defined (__GNUC__) && defined (i386)             /* i386 gcc assembler optimizations */
#undef ReadL
#undef WriteL
#undef ReadW
#undef WriteW
static inline uint16 ReadW(const void *addr)
{
    register uint16 val;
    register const uint16 *a = (const uint16 *)addr;
    __asm__ ( "rolw $8,%0\n"
              : "=r" (val)
              : "0" (*a));
    return val;
}
static inline uint32 ReadL(const void * addr)
{
    register uint32 val;
    register const uint32 *a = (const uint32 *)addr;

    __asm__ ("bswap %0"
             : "=r" (val)
             : "0" (*a));
    return val;
}
static inline void WriteW(const void *addr, const uint16 val)
{
    register uint16 v = val;

    __asm__ ( "rolw $8,%0\n"
             : "=r" (v)
             : "0" (v));
    *(uint16 *)addr = v;
}
static inline void WriteL(const void *addr, const uint32 val)
{
    register uint32 v = val;

    __asm__ ("bswap %0"
             : "=r" (v)
             : "0" (v));
    *(uint32 *)addr = v;
}
#endif
#else                           /* not LITTLE_ENDIAN */
#define ReadB(address) *((int8 *) (address))
#define ReadW(address) *((int16 *) (address))
#define ReadL(address) (*((int16 *) (address)) << 16 | *((uint16 *) (address) + 1))
#define WriteB(address,value) *((int8 *) (address)) = (value)
#define WriteW(address,value) *((int16 *) (address)) = (value)
#define WriteL(address,value) *((int16 *) (address)) = (value) >> 16; *((int16 *) (address) + 1) = (int16) (value);
#endif

/*
 * memory access jump tables
 */
void            (*mem_set_b[MEMTABLESIZE]) (uint32 address, uint8 value);
void            (*mem_set_w[MEMTABLESIZE]) (uint32 address, uint16 value);
void            (*mem_set_l[MEMTABLESIZE]) (uint32 address, uint32 value);
uint8           (*mem_get_b[MEMTABLESIZE]) (uint32 address);
uint16          (*mem_get_w[MEMTABLESIZE]) (uint32 address);
uint32          (*mem_get_l[MEMTABLESIZE]) (uint32 address);

/*
 * Functions for unmapped memory areas
 */
/* r1570: диагностика — обращения вне карты. В оригинале Unmapped молча
 * отдаёт 0xFF; EmuTOS на этом может крутиться в ожидании. */
volatile uint32_t cw_unmapped_cnt;
volatile uint32_t cw_unmapped_addr;

static void    UnmappedSetB (const uint32 address, const uint8 value)
{
    ON_WRITE(address, value);
    ON_UNMAPPED(address, value);
    cw_unmapped_cnt++; cw_unmapped_addr = address;
}

static void    UnmappedSetW (const uint32 address, const uint16 value)
{
    ON_WRITE(address, value);
    ON_UNMAPPED(address, value);
    cw_unmapped_cnt++; cw_unmapped_addr = address;
}

static void    UnmappedSetL (const uint32 address, const uint32 value)
{
    ON_WRITE(address, value);
    ON_UNMAPPED(address, value);
    cw_unmapped_cnt++; cw_unmapped_addr = address;
}

static uint8   UnmappedGetB (const uint32 address)
{
    cw_unmapped_cnt++; cw_unmapped_addr = address;
    return -1;
}

static uint16  UnmappedGetW (const uint32 address)
{
    cw_unmapped_cnt++; cw_unmapped_addr = address;
    return -1;
}

static uint32  UnmappedGetL (const uint32 address)
{
    cw_unmapped_cnt++; cw_unmapped_addr = address;
    return -1;
}

/*
 * Set RAM <= 0x800 (no access from user mode)
 */
static void    LowRamSetB (const uint32 address, const uint8 value)
{
    ON_WRITE(address, value);
    if (!GetFC2()) {
        ExceptionGroup0(BUSERR, address, 0);
        /* NOTREACHED */
    }
    WriteB(address + membase, value);
}

static void    LowRamSetW (const uint32 address, const uint16 value)
{
    ON_WRITE(address, value);
    if (!GetFC2()) {
        ExceptionGroup0(BUSERR, address, 0);
        /* NOTREACHED */
    }
    WriteW(address + membase, value);
}

static void    LowRamSetL (const uint32 address, const uint32 value)
{
    ON_WRITE(address, value);
    if (!GetFC2()) {
        ExceptionGroup0(BUSERR, address, 0);
        /* NOTREACHED */
    }
    WriteL(address + membase, value);
}

/*
 * Set/Get RAM value
 */
void    RamSetB (const uint32 address, const uint8 value)
{
    ON_WRITE(address, value);
    WriteB(address + membase, value);
}

void    RamSetW (const uint32 address, const uint16 value)
{
    ON_WRITE(address, value);
    WriteW(address + membase, value);
}

void    RamSetL (const uint32 address, const uint32 value)
{
    ON_WRITE(address, value);
    WriteL(address + membase, value);
}

uint8   RamGetB (const uint32 address)
{
    return ReadB(address + membase);
}

uint16  RamGetW (const uint32 address)
{
    return ReadW(address + membase);
}

uint32  RamGetL(const uint32 address)
{
    return ReadL(address + membase);
}

/*
 * Get ROM value
 */
static uint8    RomGetB (const uint32 address)
{
    return ReadB(address + rombase);
}

static uint16   RomGetW (const uint32 address)
{
    return ReadW(address + rombase);
}

static uint32   RomGetL (const uint32 address)
{
    return ReadL(address + rombase);
}

/*
 * Get cartridge value
 */
static uint8    CarGetB(const uint32 address)
{
    return ReadB(address + carbase - CARBASE);
}

static uint16   CarGetW(const uint32 address)
{
    return ReadW((uint16 *)(address + carbase - CARBASE));
}

static uint32   CarGetL(const uint32 address)
{
    return ReadL((uint32 *)(address + carbase - CARBASE));
}

/*
 * Set/Get I/O register
 */
static void    IOSetB (const uint32 address, const uint8 value)
{
    ON_WRITE(address, value);
    if (DoIOWB(address, value)) ExceptionGroup0(BUSERR, address, 0);
}

static void    IOSetW (const uint32 address, const uint16 value)
{
    ON_WRITE(address, value);
    if (DoIOWW(address, value)) ExceptionGroup0(BUSERR, address, 0);
}

static void    IOSetL (const uint32 address, const uint32 value)
{
    ON_WRITE(address, value);
    if (DoIOWL(address, value)) ExceptionGroup0(BUSERR, address, 0);
}

static uint8    IOGetB (const uint32 address)
{
    uint8 ret;
    if (DoIORB(address, &ret)) ExceptionGroup0(BUSERR, address, 1);
    return ret;
}

static uint16   IOGetW (const uint32 address)
{
    uint16 ret;
    if (DoIORW(address, &ret)) ExceptionGroup0(BUSERR, address, 1);
    return ret;
}

static uint32   IOGetL (const uint32 address)
{
    uint32 ret;
    if (DoIORL(address, &ret)) ExceptionGroup0(BUSERR, address, 1);
    return ret;
}

/*
 * MemTableSet - Add memory bank handler to memory regions
 */
void    MemTableSet(uint32 base, uint32 size,
                void (*setbyte)(const uint32, const uint8),
                void (*setword)(const uint32, const uint16),
                void (*setlong)(const uint32, const uint32 ),
                uint8  (*getbyte)(const uint32),
                uint16 (*getword)(const uint32),
                uint32  (*getlong)(const uint32) )
{
    unsigned index;
#ifdef DEBUG
    assert((base & MEMBANKMASK) == 0);
    assert(((base + size) & MEMBANKMASK) == 0);
#endif
    /* Loop through all the banks */
    for (index = base >> MEMIDXSHIFT; index < (base + size) >> MEMIDXSHIFT; index++) {
#ifdef DEBUG
        assert(index < MEMTABLESIZE);
#endif
        mem_set_b[index] = setbyte;
        mem_set_w[index] = setword;
        mem_set_l[index] = setlong;
        mem_get_b[index] = getbyte;
        mem_get_w[index] = getword;
        mem_get_l[index] = getlong;
    }
}

/*
 * Return size of file, -1 if error
*/
int TosSize(char *filename)
{
    /* r1566: файлов больше нет — EmuTOS вшит в прошивку (tos_data.S). */
    (void)filename;
    return (int)(cw_tos_emutos_end - cw_tos_emutos);
}


void            MemInit(void)
{
    unsigned        romsize;
    uint32   romstart = 0;       /* 68k start address of ROM */

    membase = malloc(memsize);

    /* Find length of ROM file and allocate memory for it */
    /* r1566: EmuTOS вшит в прошивку (tos_data.S) — с SD не грузим. */
    romsize = TosSize(rom);
    romstart = 0xfc0000L;               /* 192K TOS */
    rombase = malloc(romsize);
    if (rombase == NULL) {
        return;             /* r1566: exit() в bare-metal не годится */
    }

#if 0
    carbase = malloc(CARSIZE);
#endif
    /* Set default memory access handlers */
    MemTableSet(0L, MEMADDRSIZE,
        UnmappedSetB, UnmappedSetW, UnmappedSetL,
        UnmappedGetB, UnmappedGetW, UnmappedGetL);
    /* Set handlers for Low-RAM region */
    MemTableSet(MEMBASE, SVADDR,
        LowRamSetB, LowRamSetW, LowRamSetL,
        RamGetB, RamGetW, RamGetL);
    /* Set handlers for RAM region */
    MemTableSet(MEMBASE + SVADDR, memsize - SVADDR,
        RamSetB, RamSetW, RamSetL,
        RamGetB, RamGetW, RamGetL);
#if 0
    /* Set handlers for cartridge region */
    MemTableSet(CARBASE, CARSIZE,
        UnmappedSetB, UnmappedSetW, UnmappedSetL,
        RamGetB, RamGetW, RamGetL);
#endif
    /* Set handlers for ROM region */
    MemTableSet(romstart, romsize,
        UnmappedSetB, UnmappedSetW, UnmappedSetL,
        RomGetB, RomGetW, RomGetL);
    /* Set handlers for IO region */
    MemTableSet(IOBASE, IOSIZE,
        IOSetB, IOSetW, IOSetL,
        IOGetB, IOGetW, IOGetL);

    /* r1566: копируем вшитый EmuTOS в буфер ROM (файлов нет). */
    (void) memcpy (rombase, cw_tos_emutos, romsize);

    /* Copy first 8 bytes of ROM to RAM */
    (void) memcpy (membase, rombase, 8);

    /* Identify ROM and init 68k start address */
    (void) memcpy (membase, rombase, 8);
    /* r1566: версия ROM не логируется (в релизе UART молчит). */

        /* precalculate rombase - now just address-adding happens later */
    rombase -= romstart;
}

