/*
 * Castaway
 *  (C) 1994 - 2002 Martin Doering, Joachim Hoenig
 *
 * IO.c - ST hardware emulation
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * revision history
 *  23.05.2002  JH  FAST1.0.1 code import: KR -> ANSI, restructuring
 *  09.06.2002  JH  Renamed io.c to st.c again (io.h conflicts with system headers)
 *  12.06.2002  JH  Correct bus error/address error exception stack frame
 *  14.06.2002  JH  Implemented STOP, shutdown CPU after multiple bus errors.
 *                  Removed inst parameter from CPU opcode functions.
 *  20.08.2002  JH  Fixed sign bug in DoIORW() and DoIORL()
 *  10.09.2002  JH  Bugfix: MOVE.L 0xfffa00,d0 and the like should not raise bus error
 *  16.09.2002  JH  Bugfix: Word access on unmapped I/O address stacked
 *                  two bus error stack frames. Fault address corrected.
 *                  Merged some code from JH_TOS206_patches branch.
 *  02.10.2002  JH  Eliminated a lot of silly bugs introduced recently. Shame on me.
                    No more CPU bus errors from blitter.c::bitblt().
 *  10.10.2002  JH  Compatibility improvements.
 *  25.10.2002  JH  Added compatibility improvements, including MFP auto
                    end-of-interrupt mode. Thx to olivencia@@wanadoo.fr.
 */
static char     sccsid[] = "$Id: st.c,v 1.16 2003/01/29 21:13:09 mdoering Exp $";
#include "config.h"
#include <stdio.h>
#include "68000.h"
#include "st.h"
#include "cw_sound.h"   /* r1566: YM2149 — запись PSG-регистров */

#ifdef DEBUG
#include <assert.h>
#endif

#define VALUE_OPEN  0xff
/*
 * startup display mode
 */
int             display_mode = MONO;

/*
 * I/O Registers
 */
uint8           memconf;

uint8           vid_baseh, vid_basem;
uint8           vid_adrh, vid_adrm, vid_adrl, vid_syncmode, vid_shiftmode;
int16           vid_col[16];
int             vid_flag;

uint16          dma_car, dma_scr, dma_sr, dma_mode;
uint8           dma_adrh, dma_adrm, dma_adrl;

uint8           snd_rs, snd_regs[16];

uint8           mfp_gpip, mfp_aer, mfp_ddr, mfp_iera, mfp_ierb, mfp_ipra,
                mfp_iprb, mfp_isra, mfp_isrb, mfp_imra, mfp_imrb, mfp_ivr,
                mfp_tacr, mfp_tbcr, mfp_tcdcr, mfp_tadr, mfp_tbdr, mfp_tcdr,
                mfp_tddr, mfp_scr, mfp_ucr, mfp_rsr, mfp_tsr, mfp_udr;

uint8           acia1_cr, acia1_sr, acia1_dr, acia2_cr, acia2_sr, acia2_dr;

uint16          blt_halftone[16];
int16           blt_src_x_inc, blt_src_y_inc;
uint32          blt_src_addr;
int16           blt_end_1, blt_end_2, blt_end_3;
int16           blt_dst_x_inc, blt_dst_y_inc;
uint32          blt_dst_addr;
uint16          blt_x_cnt, blt_y_cnt;
int8            blt_hop, blt_op, blt_status, blt_skew;
int8            blt_ready;

/*
 * Interrupt pending status flags
 */
char            vblank_pending, hblank_pending, mfp_pending;
char            timer_tick;

void            IOTimer(void)
{                       /* 200 Hz timer interrupt */
    timer_tick = 1;
}

void            SetInterruptPriority(void)
{
    uint16          imr, ipr, isr, irr;
    int             in_request, in_service;
    intpri = 0;
    if (timer_tick) {            /* timer was here */
        timer_tick = 0;
        mfp_iprb |= 0x20;
        mfp_iprb &= mfp_ierb;
        /* generate a timer A interrupt, if enabled.
         * prevents applications using timer A from
         * waiting for interrupts indefinitely */
        /* no good idea! */
        /* if (mfp_tacr != 0) {
            mfp_ipra |= 0x20;
            mfp_ipra &= mfp_iera;
        } */
    }
    if (!(mfp_gpip & 0x20)) {   /* FDC-Interrupt */
        mfp_iprb |= 0x80;
        mfp_iprb &= mfp_ierb;
    }
    if (!(mfp_gpip & 0x10)) {   /* ACIA-Interrupt */
        mfp_iprb |= 0x40;
        mfp_iprb &= mfp_ierb;
    }
    if (hblank_pending)         /* these are quite simple */
        intpri = 2;
    if (vblank_pending)
        intpri = 4;
    /*
     * MFP in SW-EOI-mode
     */
    imr = (mfp_imra << 8) + mfp_imrb;
    ipr = (mfp_ipra << 8) + mfp_iprb;
    irr = imr & ipr;
    isr = (mfp_isra << 8) + mfp_isrb;
    for (in_service = 15; in_service > 0; in_service--) {
        if (isr & 0x8000)
            break;
        isr <<= 1;
    }
    for (in_request = 15; in_request > 0; in_request--) {
        if (irr & 0x8000)
            break;
        irr <<= 1;
    }
    if (in_request > in_service) {
        intpri = 6;
        mfp_pending = in_request;
    } else {
        mfp_pending = 0;
    }
}

int             QueryIRQ (intlev)
    int             intlev;
{
    int             isr, number;
#if (VERBOSE & 0x2)
    DBG_OUT ("IRQ 0x%01x\n", intlev);
#endif
    number = 0;         /* MAD: because of "uninitialized" cc warning */
    switch (intlev & 0x7) {
    case 6:                     /* MFP in software EOI-mode */
#ifdef DEBUG
        assert (mfp_pending);
#endif
        isr = 1 << mfp_pending;
        if (mfp_ivr & 0x8) {
            /* set interrupt in service bits */
            mfp_isra |= isr >> 8;
            mfp_isrb |= isr;
        }
        /* clear interrupt pending bits */
        mfp_ipra &= ~(isr >> 8);
        mfp_iprb &= ~isr;
        number = mfp_pending | (mfp_ivr & 0xf0);
        break;
    case 4:                     /* VBLANK */
#ifdef DEBUG
        assert (vblank_pending);
#endif
        vblank_pending = 0;
        number = AUTOINT4;
        break;
    case 2:                     /* HBLANK */
#ifdef DEBUG
        assert (hblank_pending);
#endif
        hblank_pending = 0;
        number = AUTOINT2;
        break;
    default:
#ifdef DEBUG
        assert(0);
#endif
        break;
    }
    SetInterruptPriority();
    return number;
}

void            HBlank(void)
{
    /* horizontal blanking interrupt */
    hblank_pending = 1;
}

void            VBlank(void)
{
    /* vertical blanking interrupt */
    vblank_pending = 1;
}

void            IOInit(void)
{
    vid_baseh = 0;
    vid_basem = 0;
    vid_shiftmode = display_mode;
    dma_sr = 1;                 /* DMA status ready */
    if (display_mode == MONO) {
        mfp_gpip = 0x39;        /* no lpr, no blt, no interrupt, monochrome */
    } else {
        mfp_gpip = 0xb9;        /* no lpr, no blt, no interrupt, color */
    }
#ifdef sun
    act.sa_handler = Sigbus;
    (void) sigaction (SIGBUS, &act, &oldsigbus);
#endif
#ifdef USE_MMAP
    act.sa_handler = Sigsegv;
    (void) sigaction (SIGSEGV, &act, &oldsigsegv);
#endif
}

int             DoIOWB(uint32 address, uint8 value)
{
#if (VERBOSE & 0x1)
    DBG_OUT ("IOWB 0x%06lx = 0x%02x\n", address, value);
#endif
    if (address >= RTC_SECL && address <= RTC_RES3) {
        return 0;               /* got no RTC code, do nothing */
    }
    switch (address) {
    case MEM_CONF:
        memconf = value;
        break;
    case VID_BASEH:
        vid_baseh = vid_adrh = value;
        break;
    case VID_BASEM:
        vid_basem = vid_adrm = value;
        break;
    case VID_SYNCMODE:
        vid_syncmode = value;
        break;
    case VID_SHIFTMODE:
        vid_shiftmode = value;
        break;
    case DMA_ADRH:
        dma_adrh = value;
        break;
    case DMA_ADRM:
        dma_adrm = value;
        break;
    case DMA_ADRL:
        dma_adrl = value;
        break;
    case SND_RS:
        snd_rs = value & 0xf;
        cw_psg_ctrl((uint8_t)(value & 0xff));   /* r1566: latch YM2149 */
        break;
    case SND_WD:
        snd_regs[snd_rs] = value;
        cw_psg_data((uint8_t)(value & 0xff));   /* r1566: данные YM2149 */
        break;
    case MFP_GPIP:
        break;
    case MFP_AER:
        mfp_aer = value;
        break;
    case MFP_DDR:
        mfp_ddr = value;
        break;
    case MFP_IERA:
        mfp_iera = value;
        mfp_ipra &= mfp_iera;
        SetInterruptPriority ();
        break;
    case MFP_IERB:
        mfp_ierb = value;
        mfp_iprb &= mfp_ierb;
        SetInterruptPriority ();
        break;
    case MFP_IPRA:
        mfp_ipra &= value;
        SetInterruptPriority ();
        break;
    case MFP_IPRB:
        mfp_iprb &= value;
        SetInterruptPriority ();
        break;
    case MFP_ISRA:
        mfp_isra &= value;
        SetInterruptPriority ();
        break;
    case MFP_ISRB:
        mfp_isrb &= value;
        SetInterruptPriority ();
        break;
    case MFP_IMRA:
        mfp_imra = value;
        SetInterruptPriority ();
        break;
    case MFP_IMRB:
        mfp_imrb = value;
        SetInterruptPriority ();
        break;
    case MFP_IVR:
        mfp_ivr = value;
        break;
    case MFP_TACR:
        mfp_tacr = value;
        break;
    case MFP_TBCR:
        mfp_tbcr = value;
        break;
    case MFP_TCDCR:
        mfp_tcdcr = value;
        break;
    case MFP_TADR:
        mfp_tadr = value;
        break;
    case MFP_TBDR:
        mfp_tbdr = value;
        break;
    case MFP_TCDR:
        mfp_tcdr = value;
        break;
    case MFP_TDDR:
        mfp_tddr = value;
        break;
    case MFP_SCR:
        mfp_scr = value;
        break;
    case MFP_UCR:
        mfp_ucr = value;
        break;
    case MFP_RSR:
        mfp_rsr = value;
        break;
    case MFP_TSR:
        mfp_tsr = value;
        break;
    case MFP_UDR:
        mfp_udr = value;
        break;
    case ACIA1_SR:
        acia1_cr = value;
        break;
    case ACIA1_DR:
        /* acia1_sr &= 0xfd; */
        IkbdRecv (value);
        break;
    case ACIA2_SR:
        acia2_cr = value;
        break;
    case ACIA2_DR:
        break;
#ifndef NO_BLITTER
    case BLT_HOP:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%02x\n", address, (uint8) value);
#endif
        blt_hop = value;
        blt_ready = 0;
        break;
    case BLT_OP:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%02x\n", address, (uint8) value);
#endif
        blt_op = value;
        blt_ready = 0;
        break;
    case BLT_STAT:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%02x\n", address, (uint8) value);
#endif
        blt_status = value;
        if ((blt_status & 0x80) && !(blt_ready)) {
            bitblt();
            blt_ready= 1;
        }
        blt_status &= 0x7f;
        break;
    case BLT_SKEW:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%02x\n", address, (uint8) value);
#endif
        blt_skew = value;
        blt_ready = 0;
        break;
#endif
    default:
        ON_NOIO(address, value);
        if ((address >= 0xff8200 && address <= 0xff820f)
            || (address >= 0xff8600 && address <= 0xff860f)
            || (address >= 0xff8800 && address <= 0xff8807)
            || (address >= 0xfffa00 && address <= 0xfffa2f)
            || (address >= 0xfffc00 && address <= 0xfffc07)
            || (address >= 0xfffc20 && address <= 0xfffc3f)) {
            return 0;
#ifndef NO_BLITTER
        } else if ((blt_status & 0x80) && !(blt_ready)) {
            /* the blitter's fault */
#if (VERBOSE & 0x8)
            DBG_OUT("Blitter write fault at 0x%08lx, 0x%02x\n", address, (uint8) value);
#endif
            return 0; /* no bus error */
#endif
        } else {
            return 1;
        }
        break;
    }
    return 0;
}

int             DoIOWW(uint32 address, uint16 value)
{
    int ret = 0;
#if (VERBOSE & 0x1)
    DBG_OUT ("IOWW 0x%06lx = 0x%04x\n", address, value);
#endif
    if (address >= VID_COL0 && address <= VID_COL15) {
        vid_col[(address & 0x1f) >> 1] = value;
        vid_flag = 1;
#ifndef NO_BLITTER
    } else if (address >= BLT_HFT && address < BLT_SXINC) {
        blt_halftone[(address & 0x1f) >> 1] = value;
        blt_ready = 0;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
#endif
    } else
        switch (address) {
        case DMA_MODE:
            dma_mode = value;
            break;
        case DMA_CAR:
            if (dma_mode & 0x10) {
                dma_scr = value;
            } else {
                if (dma_mode & 0x8) {
                    dma_car = value;
                } else {
                    switch (dma_mode & 0x6) {
                    case 0:
                        fdc_command = value;
                        FDCCommand ();
                        break;
                    case 2:
                        fdc_track = value;
                        break;
                    case 4:
                        fdc_sector = value;
                        break;
                    case 6:
                        fdc_data = value;
                        break;
                    }
                }
            }
            break;
#ifndef NO_BLITTER
        case BLT_SXINC:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
            blt_src_x_inc = value & -2;
            blt_ready = 0;
            break;
        case BLT_SYINC:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
            blt_src_y_inc = value & -2;
            blt_ready = 0;
            break;
        case BLT_END1:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
            blt_end_1 = value;
            blt_ready = 0;
            break;
        case BLT_END2:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
            blt_end_2 = value;
            blt_ready = 0;
            break;
        case BLT_END3:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
            blt_end_3 = value;
            blt_ready = 0;
            break;
        case BLT_DXINC:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
            blt_dst_x_inc = value & -2;
            blt_ready = 0;
            break;
        case BLT_DYINC:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
            blt_dst_y_inc = value & -2;
            blt_ready = 0;
            break;
        case BLT_XCNT:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
            blt_x_cnt = value;
            blt_ready = 0;
            break;
        case BLT_YCNT:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
            blt_y_cnt = value;
            blt_ready = 0;
            break;
        case BLT_STAT:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%04x\n", address, (uint16) value);
#endif
            ret = DoIOWB(address + 1, value)
                  || DoIOWB(address, value >> 8);
            break;
#endif
        default:
            ret = DoIOWB(address, value >> 8)
                  || DoIOWB(address + 1, value);
            break;
        }
    return ret;
}

int             DoIOWL(uint32 address, uint32 value)
{
    int ret = 0;
    switch (address) {
#ifndef NO_BLITTER
    case BLT_SADR:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%08lx\n", address, (uint32) value);
#endif
        blt_src_addr = value & 0xfffffe;
        blt_ready = 0;
        break;
    case BLT_DADR:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterW 0x%08lx, 0x%08lx\n", address, (uint32) value);
#endif
        blt_dst_addr = value & 0xfffffe;
        blt_ready = 0;
        break;
#endif
    default:
        ret = DoIOWW(address, value >> 16)
              || DoIOWW(address + 2, value);
        break;
    }
    return ret;
}

int             DoIORB(uint32 address, uint8 *result)
{
    if (address >= RTC_SECL && address <= RTC_RES3) {
        *result = VALUE_OPEN;
        return 0;              /* got no RTC code, do nothing */
    }
    switch (address) {
    case MEM_CONF:
        *result = memconf;
        break;
    case VID_BASEH:
        *result = vid_baseh;
        break;
    case VID_ADRH:
        *result = vid_adrh;
        break;
    case VID_BASEM:
        *result = vid_basem;
        break;
    case VID_ADRM:
        *result = vid_adrm++;
        break;
    case VID_ADRL:
        *result = vid_adrl++;
        break;
    case VID_SYNCMODE:
        *result = vid_syncmode;
        break;
    case VID_SHIFTMODE:
        *result = vid_shiftmode;
        break;
    case DMA_ADRH:
        *result = dma_adrh;
        break;
    case DMA_ADRM:
        *result = dma_adrm;
        break;
    case DMA_ADRL:
        *result = dma_adrl;
        break;
    case SND_RS:
        *result = snd_regs[snd_rs];
        break;
    case MFP_GPIP:
        *result = mfp_gpip;
        break;
    case MFP_AER:
        *result = mfp_aer;
        break;
    case MFP_DDR:
        *result = mfp_ddr;
        break;
    case MFP_IERA:
        *result = mfp_iera;
        break;
    case MFP_IERB:
        *result = mfp_ierb;
        break;
    case MFP_IPRA:
        *result = mfp_ipra;
        break;
    case MFP_IPRB:
        *result = mfp_iprb;
        break;
    case MFP_ISRA:
        *result = mfp_isra;
        break;
    case MFP_ISRB:
        *result = mfp_isrb;
        break;
    case MFP_IMRA:
        *result = mfp_imra;
        break;
    case MFP_IMRB:
        *result = mfp_imrb;
        break;
    case MFP_IVR:
        *result = mfp_ivr;
        break;
    case MFP_TACR:
        *result = mfp_tacr;
        break;
    case MFP_TBCR:
        *result = mfp_tbcr;
        break;
    case MFP_TCDCR:
        *result = mfp_tcdcr;
        break;
    case MFP_TADR:
        *result = mfp_tadr;
        break;
    case MFP_TBDR:
        /* Timer B Data Register counts raster lines */
        /* always 1: this seems to keep TOS1.00+ timer loop happy */
        *result = mfp_tbdr;
        mfp_tbdr = 1;
        break;
    case MFP_TCDR:
        /* 200Hz-Timer */
        /* this seems to keep TOS2.06+ timer loop happy */
        *result = mfp_tcdr++;
        break;
    case MFP_TDDR:
        *result = mfp_tddr;
        break;
    case MFP_SCR:
        *result = mfp_scr;
        break;
    case MFP_UCR:
        *result = mfp_ucr;
        break;
    case MFP_RSR:
        *result = mfp_rsr;
        break;
    case MFP_TSR:
        *result = mfp_tsr;
        break;
    case MFP_UDR:
        *result = mfp_udr;
        break;
    case ACIA1_SR:
        *result = 2 | acia1_sr;
        break;
    case ACIA1_DR:              /* Get char from KBD */
        if (!(acia1_cr & 0x20)) {       /* transmit interrupt enabled? */
            acia1_sr &= 0x7e;   /* no, clear interrupt bit */
            mfp_gpip |= 0x10;   /* clear ACIA-Interrupt request */
            SetInterruptPriority ();
        }
        *result = acia1_dr;
        /* kill (getpid(), SIGSTOP); */
        break;
    case ACIA2_SR:
        *result = acia2_sr;
        break;
    case ACIA2_DR:
        *result = 0;
        break;
#ifndef NO_BLITTER
    case BLT_HOP:
        *result = blt_hop;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%02x\n", address, (uint8) *result);
#endif
        break;
    case BLT_OP:
        *result = blt_op;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%02x\n", address, (uint8) *result);
#endif
        break;
    case BLT_STAT:
        *result = blt_status;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%02x\n", address, (uint8) *result);
#endif
        break;
    case BLT_SKEW:
        *result = blt_skew;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%02x\n", address, (uint8) *result);
#endif
        break;
#endif
    default:
        ON_NOIO(address, *result);
        *result = VALUE_OPEN;
        if ((address >= 0xff8200 && address <= 0xff820f)
            || (address >= 0xff8600 && address <= 0xff860f)
            || (address >= 0xff8800 && address <= 0xff8807)
            || (address >= 0xfffa00 && address <= 0xfffa2f)
            || (address >= 0xfffc00 && address <= 0xfffc07)
            || (address >= 0xfffc20 && address <= 0xfffc3f)) {
            return 0;
#ifndef NO_BLITTER
        } else if ((blt_status & 0x80) && !(blt_ready)) {
            /* the blitter's fault */
#if (VERBOSE & 0x8)
            DBG_OUT("Blitter read fault at 0x%08lx\n", address);
#endif
            return 0; /* no bus error */
#endif
        } else {
            return 1;
        }
        break;
    }
#if (VERBOSE & 0x1)
    DBG_OUT ("IORB 0x%06lx = 0x%02x\n", address, *result);
#endif
    return 0;
}

int             DoIORW(uint32 address, uint16 *result)
{
    int     ret = 0;
    uint8   resultL, resultH;
    if (address >= VID_COL0 && address <= VID_COL15) {
        *result = vid_col[(address & 0x1f) >> 1];
#ifndef NO_BLITTER
    } else if (address >= BLT_HFT && address < BLT_SXINC) {
        *result = blt_halftone[(address & 0x1f) >> 1];
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%04x\n", address, (uint16) *result);
#endif
#endif
    } else
        switch (address) {
        case DMA_SR:
            *result = dma_sr;
            break;
        case DMA_CAR:
            if (dma_mode & 0x10) {
                *result = dma_scr;
            } else {
                if (dma_mode & 0x8) {
                    *result = dma_car;
                } else {
                    switch (dma_mode & 0x6) {
                    case 0:
                        *result = fdc_status;
                        if (!fdc_int) {
                            mfp_gpip |= 0x20;   /* Clear FDC Interrupt
                                                 * Request */
                            SetInterruptPriority ();
                        }
                        break;
                    case 2:
                        *result = fdc_track;
                        break;
                    case 4:
                        *result = fdc_sector;
                        break;
                    case 6:
                        *result = fdc_data;
                        break;
                    default:                    /* get rid of super-
                                                 * silly warning */
                        *result = 0;
                        break;
                    }
                }
            }
            break;
#ifndef NO_BLITTER
        case BLT_SXINC:
            *result = blt_src_x_inc;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%04x\n", address, (uint16) *result);
#endif
            break;
        case BLT_SYINC:
            *result = blt_src_y_inc;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%04x\n", address, (uint16) *result);
#endif
            break;
        case BLT_END1:
            *result = blt_end_1;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%04x\n", address, (uint16) *result);
#endif
            break;
        case BLT_END2:
            *result = blt_end_2;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%04x\n", address, (uint16) *result);
#endif
            break;
        case BLT_END3:
            *result = blt_end_3;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%04x\n", address, (uint16) *result);
#endif
            break;
        case BLT_DXINC:
            *result = blt_dst_x_inc;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%04x\n", address, (uint16) *result);
#endif
            break;
        case BLT_DYINC:
            *result = blt_dst_y_inc;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%04x\n", address, (uint16) *result);
#endif
            break;
        case BLT_XCNT:
            *result = blt_x_cnt;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%04x\n", address, (uint16) *result);
#endif
            break;
        case BLT_YCNT:
            *result = blt_y_cnt;
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%04x\n", address, (uint16) *result);
#endif
            break;
#endif
        default:
            ret = DoIORB(address, &resultH)
                  || DoIORB(address + 1, &resultL);
            *result = (((uint16) resultH) << 8) + resultL;
            break;
        }
#if (VERBOSE & 0x1)
    DBG_OUT ("IORW 0x%06lx = 0x%04x\n", address, (uint16) *result);
#endif
    return ret;
}

int             DoIORL(uint32 address, uint32 *result)
{
    int     ret = 0;
    uint16  resultL, resultH;
    switch (address) {
#ifndef NO_BLITTER
    case BLT_SADR:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%08lx\n", address, (uint32) blt_src_addr);
#endif
        *result = blt_src_addr;
        break;
    case BLT_DADR:
#if (VERBOSE & 0x8)
    DBG_OUT ("BlitterR 0x%08lx, 0x%08lx\n", address, (uint32) blt_dst_addr);
#endif
        *result = blt_dst_addr;
        break;
#endif
    default:
        ret = DoIORW(address, &resultH)
              || DoIORW(address + 2, &resultL);
        *result = (((uint32) resultH) << 16) + resultL;
        break;
    }
    return ret;
}

