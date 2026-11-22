/*
 * Castaway
 *  (C) 1994 - 2002 Martin Doering, Joachim Hoenig
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef _68000H
#define _68000H

#include "config.h"     // wg. INLINE define

/*
 * Supported CPU types
 * CPU                  CPU_TYPE
 * MC68000              68000
 * MC68008              68000
 * MC68EC000            not supported (yet)
 * MC68010              68010
 * MC68020              68020
 * MC68EC020            not supported (yet)
 * MC68030              not supported (yet)
 * MC68EC030            not supported (yet)
 * MC68040              not supported (yet)
 * MC68EC040            not supported (yet)
 * CPU32                not supported (yet)
 */
#define CPU_TYPE        68000

/*
 * Exception vector numbers (trap numbers).
 */
#define RESET           0
#define BUSERR          2
#define ADDRESSERR      3
#define ILLINSTR        4
#define DIVZ            5
#define TRAPCHK         6
#define TRAPV           7
#define PRIV            8
#define TRACE           9
#define LINE_A          10
#define LINE_F          11
#if CPU_TYPE != 68000
#define FORMATERR       14
#endif
#define AUTOINT1        25
#define AUTOINT2        26
#define AUTOINT3        27
#define AUTOINT4        28
#define AUTOINT5        29
#define AUTOINT6        30
#define AUTOINT7        31
#define TRAP0           32 + 0
#define TRAP1           32 + 1
#define TRAP2           32 + 2
#define TRAP3           32 + 3
#define TRAP4           32 + 4
#define TRAP5           32 + 5
#define TRAP6           32 + 6
#define TRAP7           32 + 7
#define TRAP8           32 + 8
#define TRAP9           32 + 9
#define TRAP10          32 + 10
#define TRAP11          32 + 11
#define TRAP12          32 + 12
#define TRAP13          32 + 13
#define TRAP14          32 + 14
#define TRAP15          32 + 15

/* Group 0 exceptions (address error / bus error) */
extern void     ExceptionGroup0(
        int number,             /* trap number */
        unsigned long address,  /* fault address */
        int ReadWrite);         /* read = true, write = false */
/* Group 1 exceptions: illegal instruction, privilege violation, interrupts */
extern void     ExceptionGroup1(int number);
extern void     Interrupt();
/* Group 2 exceptions: traps, divide by zero */
extern void     ExceptionGroup2(int number);

extern void     HWReset(void);  /* Reset */
extern void     IllIns(void);   /* Illegal instruction */
extern void     Line_A(void);   /* Axxx instruction opcode  */
extern void     Line_F(void);   /* Fxxx instruction opcode */
extern void     Stop(void);     /* Stop instruction */
extern void     TraceChanged(void); /* Called whenever trace mode changes */

/*
 * CPURun()
 * - execute approx. count 68k instructions and returns afterwards.
 * - return approx. instruction count actually executed
 * - run indefinitely if count = 0
 * CPUEvent()
 * - force CPURun() to return immediately
 */
extern void     CPUEvent(void);
extern unsigned CPURun(unsigned count);

extern void (*jmp_table[8192])(void);

#define GetInst() GetMemW (pc)
/* store CPU state if TRACEBACK defined */
#ifdef TRACEBACK
#define DoInst() \
    inst = GetInst (); \
    SaveState(inst); \
    pc += 2; \
    (*jmp_table[inst >> 3])();
#else
#define DoInst() \
    inst = GetInst (); \
    pc += 2; \
    (*jmp_table[inst >> 3])();
#endif



#define DELAY_QIRQ              /* Interrupt Requests may be delayed for up
                                 * to 5 opcode fetches */
/*
 * Choose status register implementation
 * 0 array of chars (best on i386, gcc -O broken (gcc-2.95.3-5))
 * 1 array of chars + float (second best on i386)
 * 2 short
 */
#define SR_IMPLEMENTATION 0

/*
 * Representation of the CPU
 *
 * WARNING:
 * Never access registers directly from outside the emulator.
 * The internal representation of registers may change.
 *
 * Data and address registers are stored in native byte order.
 * register numbering: 0=d0, 1=d1, ..., 8=a0, 9=a1, ... 15=a7
 */

struct cpu {
    uint32          reg[16];
    uint32	    *areg;      /* areg later points to reg[8] */
    uint32          pc;
    uint32          usp;
    uint32          ssp;
/* 68010 registers */
    uint32          dfc, sfc, vbr;
/* 68020 registers */
    uint32          caar, cacr, isp, msp;

    uint16          inst;
    uint16          intpri;
#if SR_IMPLEMENTATION == 2
    uint16          status;     /* 0=cvxstzn=6 */
#endif
#if SR_IMPLEMENTATION == 1
    int8            status[5];  /* bytes 0=cvxst=4 */
    float           status_NZ;  /* zn */
    uint16          intmask;
#endif
#if SR_IMPLEMENTATION == 0
    int8            status[7];  /* bytes 0=cvxstzn=6 */
    uint16          intmask;
#endif

    int8            state;      /* 0, 1, 2 while processing exceptions, */
                                /* -1 executing, -2 stopped, -3 shutdown */
    uint32          type;       /* 68000, 68010, ... */
    uint32          count;      /* instructions done so far */
};

#define reg       cpu.reg
#define areg      cpu.areg
#define pc        cpu.pc
#define usp       cpu.usp
#define ssp       cpu.ssp

/* 68010 registers */
#define dfc       cpu.dfc
#define sfc       cpu.sfc
#define vbr       cpu.vbr

/* 68020 registers */
#define caar      cpu.caar
#define cacr      cpu.cacr
#define isp       cpu.isp
#define msp       cpu.msp

#define inst      cpu.inst
#define intpri    cpu.intpri
#define intmask   cpu.intmask
#define status    cpu.status
#define status_NZ cpu.status_NZ
#define cpu_state cpu.state
#define cpu_type  cpu.type
#define cpu_count cpu.count

extern struct cpu cpu;

/*
 * Interrupts
 *
 * The state of interrupt lines IPL0-IPL2 is stored in the variable
 * intpri. The emulator acknowledges interrupts by calling QueryIRQ().
 * It expects the interrupt vector number to be returned.
 * WARNING:
 * Never update intpri asynchronously. Use the CPUEvent() function
 * to force CPURun() to return before updating intpri.
 */
extern int      QueryIRQ(int level); /* get interrupt vector number */

#if SR_IMPLEMENTATION == 2
#define GetFC2() (!!(status&0x2000))
#else
#define GetFC2() status[3]
#endif
#define GetFC1() address == pc
#define GetFC0() address != pc
#define GetFC() (((GetFC2() << 1) | GetFC1()) << 1) | GetFC0()

/*
 * memory access jump tables
 */
#define MEMADDRMASK 0x00ffffffl
#define MEMIDXSHIFT 11
#define MEMADDRSIZE (MEMADDRMASK + 1)
#define MEMTABLESIZE (MEMADDRSIZE >> MEMIDXSHIFT)
#define MEMBANKSIZE (MEMADDRSIZE / MEMTABLESIZE)
#define MEMBANKMASK (MEMBANKSIZE - 1)

extern void     (*mem_set_b[MEMTABLESIZE]) (uint32 address, uint8 value);
extern void     (*mem_set_w[MEMTABLESIZE]) (uint32 address, uint16 value);
extern void     (*mem_set_l[MEMTABLESIZE]) (uint32 address, uint32 value);
extern uint8    (*mem_get_b[MEMTABLESIZE]) (uint32 address);
extern uint16   (*mem_get_w[MEMTABLESIZE]) (uint32 address);
extern uint32   (*mem_get_l[MEMTABLESIZE]) (uint32 address);

/* Fetch byte from address */
INLINE int8 GetMemB(const uint32 address)
{
    return (*mem_get_b[(address&MEMADDRMASK) >> MEMIDXSHIFT]) (address&MEMADDRMASK);
}
/* Fetch word, address may not be word-aligned */
INLINE int16 GetMemW(const uint32 address)
{
#ifdef CHKADDRESSERR
    if (address & 0x1) ExceptionGroup0(ADDRESSERR, address, 1);
#endif
    return (*mem_get_w[(address&MEMADDRMASK) >> MEMIDXSHIFT]) (address&MEMADDRMASK);
}
/* Fetch dword, address may not be dword-aligned */
INLINE int32 GetMemL(const uint32 address)
{
#ifdef CHKADDRESSERR
    if (address & 0x1) ExceptionGroup0(ADDRESSERR, address, 1);
#endif
    return (*mem_get_l[(address&MEMADDRMASK) >> MEMIDXSHIFT]) (address&MEMADDRMASK);
}
/* Write byte to address */
INLINE void SetMemB (const uint32 address, const uint8 value) {
    (*mem_set_b[(address&MEMADDRMASK) >> MEMIDXSHIFT]) (address&MEMADDRMASK, value);
}
/* Write word, address may not be word-aligned */
INLINE void SetMemW(const uint32 address, const uint16 value)
{
#ifdef CHKADDRESSERR
    if (address & 0x1) ExceptionGroup0(ADDRESSERR, address, 0);
#endif
    (*mem_set_w[(address&MEMADDRMASK) >> MEMIDXSHIFT]) (address&MEMADDRMASK, value);
}
/* Write dword, address may not be dword-aligned */
INLINE void SetMemL(const uint32 address, const uint32 value)
{
#ifdef CHKADDRESSERR
    if (address & 0x1) ExceptionGroup0(ADDRESSERR, address, 0);
#endif
    (*mem_set_l[(address&MEMADDRMASK) >> MEMIDXSHIFT]) (address&MEMADDRMASK, value);
}

#ifdef DEBUG
extern int             stop_on;
extern int             verb_on;
/* go to sleep (gives debugger a chance to attach) */
void            DebugStop(void);
#ifdef TRACEBACK
/*
 * execution trace
 */
struct state {
    uint32          reg[16];
    uint32          stack[8];
    uint32          pc, usp, ssp;
    uint16          inst[5];
    uint16          sr;
#ifdef INTERNALTRACE
    uint32          address, result, source, target;
#endif
};
extern int             hide_supervisor;
extern unsigned long   instcnt;
extern int             tbi;
extern struct state    traceback[TRACEBACK];
extern int             trace_on;
/* print execution trace of last TRACEBACK instructions and sleep */
void            TraceStop(void);
#endif
#endif
#endif
