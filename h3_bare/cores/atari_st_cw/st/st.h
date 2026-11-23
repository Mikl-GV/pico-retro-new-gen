/*
 * Castaway
 *  (C) 1994 - 2002 Martin Doering, Joachim Hoenig
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef STH
#define STH
#include "config.h"     /* r1566: uint8/int16/uint32 для структур */
#include <stdio.h>      /* r1566: FILE в struct Disk/cfg */
/*
 * I/O register addresses
 */
#define MEM_CONF        0xff8001

#define VID_BASEH       0xff8201
#define VID_BASEM       0xff8203
#define VID_ADRH        0xff8205
#define VID_ADRM        0xff8207
#define VID_ADRL        0xff8209
#define VID_SYNCMODE    0xff820a
#define VID_COL0        0xff8240
#define VID_COL1        0xff8242
#define VID_COL2        0xff8244
#define VID_COL3        0xff8246
#define VID_COL4        0xff8248
#define VID_COL5        0xff824a
#define VID_COL6        0xff824c
#define VID_COL7        0xff824e
#define VID_COL8        0xff8250
#define VID_COL9        0xff8252
#define VID_COL10       0xff8254
#define VID_COL11       0xff8256
#define VID_COL12       0xff8258
#define VID_COL13       0xff825a
#define VID_COL14       0xff825c
#define VID_COL15       0xff825e
#define VID_SHIFTMODE   0xff8260

#define DMA_CAR         0xff8604
#define DMA_SCR         0xff8604
#define DMA_SR          0xff8606
#define DMA_MODE        0xff8606
#define DMA_ADRH        0xff8609
#define DMA_ADRM        0xff860b
#define DMA_ADRL        0xff860d

#define SND_RS          0xff8800
#define SND_WD          0xff8802

#define BLT_HFT         0xff8a00
#define BLT_SXINC       0xff8a20
#define BLT_SYINC       0xff8a22
#define BLT_SADR        0xff8a24
#define BLT_END1        0xff8a28
#define BLT_END2        0xff8a2a
#define BLT_END3        0xff8a2c
#define BLT_DXINC       0xff8a2e
#define BLT_DYINC       0xff8a30
#define BLT_DADR        0xff8a32
#define BLT_XCNT        0xff8a36
#define BLT_YCNT        0xff8a38
#define BLT_HOP         0xff8a3a
#define BLT_OP          0xff8a3b
#define BLT_STAT        0xff8a3c
#define BLT_SKEW        0xff8a3d

#define MFP_GPIP        0xfffa01
#define MFP_AER         0xfffa03
#define MFP_DDR         0xfffa05
#define MFP_IERA        0xfffa07
#define MFP_IERB        0xfffa09
#define MFP_IPRA        0xfffa0b
#define MFP_IPRB        0xfffa0d
#define MFP_ISRA        0xfffa0f
#define MFP_ISRB        0xfffa11
#define MFP_IMRA        0xfffa13
#define MFP_IMRB        0xfffa15
#define MFP_IVR         0xfffa17
#define MFP_TACR        0xfffa19
#define MFP_TBCR        0xfffa1b
#define MFP_TCDCR       0xfffa1d
#define MFP_TADR        0xfffa1f
#define MFP_TBDR        0xfffa21
#define MFP_TCDR        0xfffa23
#define MFP_TDDR        0xfffa25
#define MFP_SCR         0xfffa27
#define MFP_UCR         0xfffa29
#define MFP_RSR         0xfffa2b
#define MFP_TSR         0xfffa2d
#define MFP_UDR         0xfffa2f

#define ACIA1_SR        0xfffc00
#define ACIA1_DR        0xfffc02

#define ACIA2_SR        0xfffc04
#define ACIA2_DR        0xfffc06

#define RTC_SECL        0xfffc21
#define RTC_SECH        0xfffc23
#define RTC_MINL        0xfffc25
#define RTC_MINH        0xfffc27
#define RTC_HRSL        0xfffc29
#define RTC_HRSH        0xfffc2b
#define RTC_DAY         0xfffc2d
#define RTC_DAYL        0xfffc2f
#define RTC_DAYH        0xfffc31
#define RTC_MONL        0xfffc33
#define RTC_MONH        0xfffc35
#define RTC_YRL         0xfffc37
#define RTC_YRH         0xfffc39
#define RTC_RES1        0xfffc3b
#define RTC_RES2        0xfffc3d
#define RTC_RES3        0xfffc3f

/*
 * ROM/Cartridge file names
 */
extern char cartridge[80], rom[80];
extern char datadir[80];
/*
 * I/O register values
 */
extern uint32   memsize;
extern int8    *membase;
extern uint8    memconf;
extern uint8    mfp_gpip, mfp_aer, mfp_ddr, mfp_iera, mfp_ierb, mfp_ipra, mfp_iprb,
                mfp_isra, mfp_isrb, mfp_imra, mfp_imrb, mfp_ivr, mfp_tacr,
                mfp_tbcr, mfp_tcdcr, mfp_tadr, mfp_tbdr, mfp_tcdr, mfp_tddr,
                mfp_scr, mfp_ucr, mfp_rsr, mfp_tsr, mfp_udr;
extern uint8    acia1_cr, acia1_sr, acia1_dr, acia2_cr, acia2_sr,
                acia2_dr;
extern uint8    snd_rs, snd_regs[16];
extern uint8    vid_baseh, vid_basem;
extern uint8    vid_adrh, vid_adrm, vid_adrl, vid_syncmode, vid_shiftmode;
extern int16   vid_col[];
extern int      vid_flag;
extern uint16   dma_car, dma_scr, dma_sr, dma_mode;
extern uint8    dma_adrh, dma_adrm, dma_adrl;

/* ikbd.c */
extern void     cw_ikbd_reset(void);    /* r1609: сброс IKBD на входе в эмулятор */
extern void     IkbdMouse(void);
extern void     IkbdRecv(uint8);
extern void     IkbdSend(uint8);
extern int      IkbdQueryBuffer(void);  /* true if IKBD output buffer not empty */
extern void     IkbdWriteBuffer(void);  /* write byte IKBD -> ST */
extern void     IkbdKeyPress(unsigned short keysym); /* key press */
extern void     IkbdKeyRelease(unsigned short keysym); /* key release */
extern void     IkbdMousePress(int);    /* mouse button press */
extern void     IkbdMouseRelease(int);  /* mouse button release */
extern void     IkbdMouseMotion(int x, int y); /* mouse movement */
#define JOY_UP       1
#define JOY_DOWN     2
#define JOY_LEFT     4
#define JOY_RIGHT    8
#define JOY_BUTTON 128
extern void     IkbdJoystickChange(int number, uint8 state); /* joystick state change */

/* fdc.c */
extern unsigned char fdc_data, fdc_track, fdc_sector,
        fdc_status, fdc_command, fdc_int;
struct Disk {
    FILE       *file;
    char        name[160];   /* r1586: полный путь с SD (в 80 не влезал) */
    int         head;
    int         sides;
    int         tracks;
    int         sectors;
    int         secsize;
};
extern struct Disk disk[2];
extern void     FDCInit(void);
extern void     FDCCommand(void);

/* blitter.c */
extern uint16   blt_halftone[16];
extern int16    blt_src_x_inc, blt_src_y_inc;
extern uint32   blt_src_addr;
extern int16    blt_end_1, blt_end_2, blt_end_3;
extern int16    blt_dst_x_inc, blt_dst_y_inc;
extern uint32   blt_dst_addr;
extern uint16   blt_x_cnt, blt_y_cnt;
extern int8     blt_hop, blt_op, blt_status, blt_skew;

extern void     bitblt(void);

/* init.c */
extern int     Init(int argc, char **argv);

/* main.c */
extern unsigned ips;

/* mem.c */
extern void     MemInit(void);
extern void     MemTableSet(uint32 base, uint32 size,
    void (*setbyte)(uint32, uint8),
    void (*setword)(uint32, uint16),
    void (*setlong)(uint32, uint32 ),
    uint8  (*getbyte)(uint32),
    uint16 (*getword)(uint32),
    uint32 (*getlong)(uint32) );

extern void RamSetB(uint32, uint8);
extern void RamSetW(uint32, uint16);
extern void RamSetL(uint32, uint32 );
extern uint8  RamGetB(uint32);
extern uint16 RamGetW(uint32);
extern uint32  RamGetL(uint32);
/*
 * Video shift modes
 */
#define COL4 0
#define COL2 1
#define MONO 2
extern int display_mode;

/*
 * Interrupts
 */
extern void     IOInit(void);               /* init IO registers */
extern void     IOTimer(void);              /* 200Hz timer interrupt */
extern void     SetInterruptPriority(void); /* set pending interrupt priority */
extern void     HBlank(void);               /* horizontal blanking interrupt */
extern void     VBlank(void);               /* vertical blanking interrupt */

/*
 * Read/Write IO Registers (return value != 0 if bus error)
 */
int             DoIORB(uint32 address, uint8 *result);
int             DoIORW(uint32 address, uint16 *result);
int             DoIORL(uint32 address, uint32 *result);
int             DoIOWB(uint32 address, uint8 value);
int             DoIOWW(uint32 address, uint16 value);
int             DoIOWL(uint32 address, uint32 value);


#endif
