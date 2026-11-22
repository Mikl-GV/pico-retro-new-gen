/*
 * Castaway
 *  (C) 1994 - 2002 Martin Doering, Joachim Hoenig
 *
 * 68000.c - 68000 emulator jump table and misc subroutines
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * revision history
 *  23.05.2002  JH  FAST1.0.1 code import: KR -> ANSI, restructuring
 *  09.06.2002  JH  Use of mmap for memory access discontinued
 *  12.06.2002  JH  Correct bus error/address error exception stack frame
 *  13.06.2002  JH  Merged in Martin's BCD implementation (untested),
 *                  completed jump table. STOP is now the only
 *                  unimplemented instruction.
 *  14.06.2002  JH  Implemented STOP, shutdown CPU after multiple bus errors.
 *                  Removed inst parameter from CPU opcode functions.
 *  19.06.2002  JH  CPURun() returns # of executed instructions.
 *  20.06.2002  JH  added yet another SR implementation variant.
 *  02.07.2002  JH  Support different CPU types. Removed MOVE CCR,<EA> from
 *                  68000 jump table.
 *  20.08.2002  JH  Fixed CPU shutdown.
 *  27.08.2002  JH  Bugfix: S and T flag incorrectly reported for group 0 exceptions
 *                  Implemented additional 68010 registers and instructions.
 *  31.08.2002  JH  Implemented M68010 exception stack frames.
 *  08.10.2002  JH  Implemented Trace exception
 *  25.10.2002  JH  CHKTRACE no longer optional, improved implementation.
 *  01.02.2003  JH  HWReset: SSP set to 0 (Thx to Thierry Bazin)
 *  01.02.2003  JH  Fixed JMP $7 address error exception loop.
 */
static char     sccsid[] = "$Id: 68000.c,v 1.24 2003/11/30 17:26:28 mdoering Exp $";
#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <setjmp.h>
#include "68000.h"

#include "op68k.h"
#include "proto.h"

#if (CPU_TYPE == 68000 || CPU_TYPE == 68008)
#include "op68000.c"
#elif (CPU_TYPE == 68010) // broken
#include "op68010.c"
#endif


struct cpu cpu;

static jmp_buf         trap_buf;

#if SR_IMPLEMENTATION == 2

void        	SetSRB(const uint16 sr)
{
    status=(status&0xff00U)|((sr)&0xffU);
}

void        	SetSRW(const uint16 sr)
{
    status=sr;
    ssp = areg[7];
    if (!GetS())
        areg[7] = usp;
}

uint16		GetSRB()
{
    return (status&0xffU);
}

uint16 		GetSRW()
{
    return (status);
}

#else

uint16		GetSRB(void)
{
    int             sr = 0;
    sr |= GetX ();
    sr <<= 1;
    sr |= GetN ();
    sr <<= 1;
    sr |= GetZ ();
    sr <<= 1;
    sr |= GetV ();
    sr <<= 1;
    sr |= GetC ();
    return sr;
}

uint16		GetSRW(void)
{
    int             sr = 0;
    sr |= GetT ();
    sr <<= 2;
    sr |= GetS ();
    sr <<= 5;
    sr |= GetI ();
    sr <<= 4;
    sr |= GetX ();
    sr <<= 1;
    sr |= GetN ();
    sr <<= 1;
    sr |= GetZ ();
    sr <<= 1;
    sr |= GetV ();
    sr <<= 1;
    sr |= GetC ();
    return sr;
}

void            SetSRB(const uint16 sr)
{
    SetC ((sr & 0x1) != 0);
    SetV ((sr & 0x2) != 0);
    SetZ ((sr & 0x4) != 0);
    SetN ((sr & 0x8) != 0);
    SetX ((sr & 0x10) != 0);
}


void            SetSRW(const uint16 sr)
{
    SetC ((sr & 0x1) != 0);
    SetV ((sr & 0x2) != 0);
    SetZ ((sr & 0x4) != 0);
    SetN ((sr & 0x8) != 0);
    SetX ((sr & 0x10) != 0);
    SetI ((sr & 0x700) >> 8);
    SetS ((sr & 0x2000) != 0);
    SetT ((sr & 0x8000) != 0);
}
#endif

void            ExceptionGroup0(
        int number,
        unsigned long address,
        int ReadWrite)
{
    short sr = GetSRW(), context = 0;
#ifdef DEBUG
    ON_TRAP(number);
    assert(cpu_state != -3);
#endif
    if (cpu_state == 0) {
        cpu_state = -3;
        longjmp(trap_buf, 1);
    }
#if CPU_TYPE == 68000
    else if (cpu_state > 0) {
        context |= 0x8;
    }
    if (ReadWrite) context |= 0x10;
#else
    if (ReadWrite) context |= 0x100;
#endif
    if (GetS()) context |= 0x4;
    if (ReadWrite && address == pc) context |= 0x2;
    else context |= 0x1;
    cpu_state = 0; /* begin group 0 exception processing */
    SetS (1);
    SetT (0);
#if CPU_TYPE == 68010
    areg[7] -= 44; /* Rerun info */
    areg[7] -= 4; SetMemL(areg[7], address); /* fault address */
    areg[7] -= 2; SetMemW(areg[7], context);
    areg[7] -= 2; SetMemW(areg[7], 0x8000 | (number * 4));
    areg[7] -= 4; SetMemL(areg[7], GetPC());
    areg[7] -= 2; SetMemW(areg[7], sr);
#else
    areg[7] = areg[7] - 14;
    SetMemW(areg[7], context);
    SetMemL(areg[7] + 2, address);
    SetMemW(areg[7] + 6, inst);
    SetMemW(areg[7] + 8, sr);
    SetMemL(areg[7] + 10, GetPC());
#endif
    SetPC (GetMemL ((long) number * 4) + vbr);
    /* end exception processing */
    cpu_state = -1;
    longjmp(trap_buf, 1);
}

void            ExceptionGroup1(int number)
{
    short sr = GetSRW();
#ifdef DEBUG
    ON_TRAP(number);
#endif
    cpu_state = 1; /* begin group 1 exception processing */
    switch(number) {
    case LINE_A:
    case LINE_F:
    case PRIV:
    case ILLINSTR:
        SetPC(GetPC() - 2);
        break;
    }
    SetS (1);
    SetT (0);
#if CPU_TYPE == 68010
    areg[7] -= 2; SetMemW(areg[7], number * 4);
#endif
    areg[7] -= 4; SetMemL(areg[7], GetPC());
    areg[7] -= 2; SetMemW(areg[7], sr);
    SetPC (GetMemL ((long) number * 4) + vbr);
    /* end exception processing */
    cpu_state = -1;
}

void            Interrupt()
{
    short sr = GetSRW();
    int number = QueryIRQ(intpri);
#ifdef DEBUG
    ON_TRAP(number);
#endif
    cpu_state = 1; /* begin group 1 exception processing */
    SetI(intpri);
    SetS(1);
    SetT(0);
#if CPU_TYPE == 68010
    areg[7] -= 2; SetMemW(areg[7], number * 4);
#endif
    areg[7] -= 4; SetMemL(areg[7], GetPC());
    areg[7] -= 2; SetMemW(areg[7], sr);
    SetPC (GetMemL ((long) number * 4) + vbr);
    /* end exception processing */
    cpu_state = -1;
}

void            ExceptionGroup2(int number)
{
    short sr = GetSRW();
#ifdef DEBUG
    ON_TRAP(number);
#endif
    cpu_state = 2; /* begin group 2 exception processing */
    SetS (1);
    SetT (0);
#if CPU_TYPE == 68010
    areg[7] -= 2; SetMemW(areg[7], number * 4);
#endif
    areg[7] -= 4; SetMemL(areg[7], GetPC());
    areg[7] -= 2; SetMemW(areg[7], sr);
    SetPC (GetMemL ((long) number * 4) + vbr);
    /* end exception processing */
    cpu_state = -1;
}

void            Trace()
{
    int number = TRACE;
    short sr = GetSRW();
#ifdef DEBUG
    ON_TRAP(number);
#endif
    cpu_state = 1; /* begin group 1 exception processing */
    SetS(1);
    SetT(0);
#if CPU_TYPE == 68010
    areg[7] -= 2; SetMemW(areg[7], number * 4);
#endif
    areg[7] -= 4; SetMemL(areg[7], GetPC());
    areg[7] -= 2; SetMemW(areg[7], sr);
    SetPC (GetMemL ((long) number * 4) + vbr);
    /* end exception processing */
    cpu_state = -1;
}

void            IllIns(void)
{
    ExceptionGroup1(ILLINSTR);
}

void            Line_A(void)
{
    ExceptionGroup1(LINE_A);
}

void            Line_F(void)
{
    ExceptionGroup1(LINE_F);
}

void            Stop(void)
{
    if (!GetS())
        ExceptionGroup1(PRIV);
    SetSRW(GetMPCW());
    pc += 2;
    cpu_state = -2; /* stopped */
    longjmp(trap_buf, 1);
}

void            TraceChanged(void)
{
    /* pick a different CPURun() loop if trace mode changed */
    longjmp(trap_buf, 1);
}

void            HWReset(void)
{
    areg = &reg[8];    /* let areg point to reg[8]*/
    ssp = GetMemL (0l);
    SetPC(GetMemL (4l));
    SetI(7);
    SetT(0);
    SetS(1);
    cpu_state = -1; /* running */
    cpu_type = CPU_TYPE;
}

void            CPUEvent()
{
    // force the CPURun loop to end
    cpu_count = 1;
}

unsigned        CPURun(unsigned count)
{
    static unsigned executed;
    cpu_count = count;
    if (!setjmp (trap_buf)) {
        executed = 0;
    } else {
        executed++;
    }
    if (cpu_state == -3) {
        /* CPU shutdown (multiple group 0 exceptions) */
        return executed;
    } else if (cpu_state == -2) {
        /* CPU stopped (STOP instruction) */
        if (intpri <= GetI()) return executed;
    }
    if (GetT()) {
        /* check for interrupts,
         * execute 1 instruction,
         * do trace exception */
        do {
            if (intpri > GetI()) Interrupt();
            DoInst();
            Trace();
        } while (executed++ < cpu_count || cpu_count == 0);
    } else {
#if 1 || (!defined (DELAY_QIRQ) || defined (TRACEBACK))
        /* check for interrupts,
         * execute 1 instruction */
        do {
            if (intpri > GetI()) Interrupt();
            DoInst();
        } while (executed++ < cpu_count || cpu_count == 0);
#else
        /* check for interrupts,
         * execute 8 instructions */
        do {
            if (intpri > GetI()) Interrupt();
            DoInst();
            DoInst();
            DoInst();
            DoInst();
            DoInst();
            DoInst();
            DoInst();
            DoInst();
        } while ((executed += 8) < cpu_count || cpu_count == 0);
#endif
    }
    return executed;
}

