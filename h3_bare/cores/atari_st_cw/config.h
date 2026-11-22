/*
 * Castaway
 *  (C) 1994 - 2002 Joachim Hoenig, Martin Doering
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef CONFIGH
#define CONFIGH
/*
 * Environment Configuration
 */
/* r1565: H3 (Cortex-A7) — little-endian. В оригинале ARM попадал в ветку
 * BIG_ENDIAN, что ломало ReadW/ReadL/WriteW/WriteL в st/mem.c. x86-asm там
 * закрыт `#if defined(i386)` и на ARM не активен — работает C-ветка. */
#if defined (__WIN32__) || defined(i386) || defined(__arm__) || defined(__aarch64__)
#ifndef WORDS_LITTLE_ENDIAN
#define WORDS_LITTLE_ENDIAN
#endif
#else
#ifndef WORDS_BIG_ENDIAN
#define WORDS_BIG_ENDIAN
#endif
#endif

#ifdef __GNUC__
#define INLINE static inline
#endif
#ifdef __BORLANDC__
#define INLINE __inline
#endif


#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/*
 * compiler representation of M68000 .B .W .L operands
 */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
typedef signed char     bool;   /* в C23 bool — ключевое слово */
#endif
typedef signed char     int8;
typedef signed short    int16;
typedef signed long     int32;
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned long   uint32;

/*
 * Atari ST emulator defaults
 */
#define MEMBASE 0x00000000L
#define MEMSIZE 0x00400000L /* default memsize 4 Mb */
#define CARBASE 0x00fa0000L
#define CARSIZE 0x00020000L /* 128k cartridge */
#define ROMBASE 0x00fc0000L
#define ROMSIZE 0x00030000L /* 192k */
#define IOBASE  0x00ff8000L
#define IOSIZE  0x00008000L /* 32k */
#define SVADDR  0x00000800L

#define MONITOR 2           /* 0=color 320x200 or 2=monochrome 640x400 */
#define DISKA   "diska"     /* Disk A pathname */
#define DISKB   "diskb"     /* Disk B pathname */
#define CARTRIDGE "cartridge"   /* cartridge image pathname */
#define ROM "rom"           /* ROM image pathname */
#define SIDES   2           /* disk sides */
#define TRACKS  80          /* tracks on disk */
#define SECTORS 9           /* sectors per track */
#define SECSIZE 512         /* byte per sector */
#define TIMER   0           /* 0=normal (200Hz), 2=slow (100Hz) */
#undef NO_BLITTER           /* Do not emulate Blitter */
#define NO_RTC              /* Do not emulate Real-Time-Clock */
#define CHKADDRESSERR       /* if set, unaligned access will raise an address
                               error (slower, but expected behaviour) */
#define CHKTRACE            /* if set, the trace bit works (slower). */
#define NATFEAT             /* if set, native features are supported */

#undef DEBUG                /* Debug */

/*
 * Debug options
 */
#ifdef DEBUG
#ifndef CHKADDRESSERR
#define CHKADDRESSERR       /* force address error checking */
#endif
#define VERBOSE 0x1         /* ~INT, IO, ~BLITTER */
#define TRACEBACK 2000     /* 68k traceback buffer size */
#undef INTERNALTRACE       /* trace 68k operation internal execution */
#define DBG_OUT if (verb_on) printf
#define DBG_STOP if (stop_on) Stop
#define NO_TIMER
/* special DEBUG action on traps */
#define ON_TRAP(number) if (number == 33) {stop_on++;};
/* special DEBUG action on R/W access to an unmapped address */
#define ON_UNMAPPED(address, value)
/* special DEBUG action on unmapped I/O access */
#define ON_NOIO(offset, value)
/* special DEBUG action on write access */
#define ON_WRITE(address, value)
extern int      trace_on;
extern int      stop_on;
extern int      verb_on;
extern void     SaveState(unsigned short inst);
extern void     Stop(void);
#else /* not DEBUG */
#define ON_TRAP(number) if (number == 33 && GetMemW(areg[7]) == 0x4c) { \
    TraceStop(); }
#define ON_UNMAPPED(address, value)
#define ON_NOIO(address, value)
#define ON_WRITE(address, value)
#endif
#endif

