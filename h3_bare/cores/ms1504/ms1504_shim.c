/* ms1504_shim.c — заглушки для собранного подмножества Fake86 (МС1504).
   Здесь всё, на что ссылаются cpu/bios/video/ports/disk/i825x, но чего нет
   в не-компилируемых SDL-файлах (main/render/input/parsecl/timing/hostfs). */

#include <stdint.h>
#include <stddef.h>

#include "fake86_src/config.h"
#include "fake86_src/cpu.h"
#include "fake86_src/ports.h"
#include "fake86_src/timing.h"
#include "fake86_src/parsecl.h"
#include "fake86_src/render.h"
#include "fake86_src/speaker.h"
#include "fake86_src/i8253.h"
#include "fake86_src/i8259.h"
#include "fake86_src/video.h"
#include "fake86_src/hostfs.h"

#include "h3_hs_timer.h"   // HSTMR — реальное µs-время для PIT/retrace

/* ---- parsecl.h globals (раньше жили в parsecl.c) ---- */
uint16_t constanth = 0;
uint16_t constantw = 0;
uint8_t  slowsystem = 0;
char     *biosfile = 0;
uint32_t speed = 0;
uint8_t  verbose = 0;
uint8_t  useconsole = 0;
uint8_t  usessource = 0;
int      usekvm = 0;
uint8_t  dohardreset = 0;

void parsecl ( int argc, char *argv[] ) { (void)argc; (void)argv; }
uint32_t loadrom ( uint32_t addr32, const char *filename, uint8_t failure_fatal )
{ (void)addr32; (void)filename; (void)failure_fatal; return 1; }

/* ---- render.h globals ---- */
uint8_t  renderbenchmark = 0;
uint8_t  scrmodechange = 0;
uint32_t  framedelay = 0;
uint64_t  totalframes = 0;
uint8_t  noscale = 0, nosmooth = 0;

int      sdl_error        ( const char *msg ) { (void)msg; return -1; }
void     sdl_shutdown     ( void ) {}
int      initscreen       ( const char *ver ) { (void)ver; return 0; }
void     setwindowtitle   ( const char *extra ) { (void)extra; }
void     doscrmodechange  ( void ) {}

const char *SDL_GetError ( void ) { return ""; }

/* пиксельный формат для DAC-палитры: RGB888-сдвиги */
static struct ms1504_pix_fmt sdl_fmt = { 16, 8, 0, 24 };
struct ms1504_pix_fmt *sdl_pixfmt = &sdl_fmt;

/* ---- speaker.h ---- */
uint8_t speakerenabled = 0;
/* r735: PC-спикер — «квадрат» по частоте PIT канала 2 (i8253.chandata[2]).
 * fake86_src/audio.c НЕ собирается (нет в MS1504_BONES), поэтому генерация
 * и буфер живут здесь (shim), host читает m15_audbuf. */
static int16_t speaker_ph = 0;
static int64_t speaker_acc = 0;
int16_t speakergensample ( void ) {
    extern struct i8253_s i8253;   // fake86_src/i8253.h
    if (!speakerenabled) return 0;
    uint16_t div = (uint16_t)(i8253.chandata[2] ? i8253.chandata[2] : 1);
    speaker_acc += div;
    if (speaker_acc >= 1193182) {   /* ~1 сек (округлённо по 1.19 МГц) */
        speaker_acc -= 1193182;
        speaker_ph = (int16_t)(speaker_ph ? 0 : 12000);
    }
    return speaker_ph;
}

/* r735: буфер PC-спикера (генерирует host-цикл, не audio.c) */
int8_t m15_audbuf[96000];
int32_t m15_audbufptr;
void m15_tick_audio(int n) {
    /* заполнить n сэмплов квадратом по текущей частоте (44100 Гц) */
    extern struct i8253_s i8253;
    uint16_t div = (uint16_t)(i8253.chandata[2] ? i8253.chandata[2] : 1);
    if (n > 96000) n = 96000;
    for (int i = 0; i < n; i++) {
        speaker_acc += div;
        if (speaker_acc >= 1193182) {
            speaker_acc -= 1193182;
            speaker_ph = (int16_t)(speaker_ph ? 0 : 12000);
        }
        m15_audbuf[i] = (int8_t)((speakerenabled ? speaker_ph : 0) >> 8);
    }
    m15_audbufptr = n;
}

/* ---- timing: реальная генерация IRQ0 (PIT канал 0) и статуса CGA ----
   Стоковый timing.c привязан к SDL-часам и аудио. У нас единственные часы
   HSTMR (µs). Реальные тайминги важны, чтобы BIOS при HLT (ожидание
   прерывания) просыпался, а видео-циклы, ждущие ретрейс по порту 3DA,
   не висели. */
uint64_t gensamplerate = 0;
uint64_t hostfreq      = 1000000;
uint64_t sampleticks   = 0;
uint64_t tickgap       = 0;
uint64_t lasttick      = 0;

void inittiming ( void ) {}

#define PIT_IRQ0_US 54925u       /* 1.19318 МГц / 65536 ≈ 18.2 Гц */
#define VGA_RETRACE_US 8333u     /* ~120 Гц переключение H/V phase */

void timing ( void )
{
    uint32_t ct = h3_hs_timer_lo_us();
    static uint32_t p_last = 0;    // последний IRQ0
    static uint32_t v_last = 0;    // последний перепад ретрейса
    static uint8_t  v_ph   = 0;

    // PIT канал 0: если BIOS разрешил IRQ0 — держим ~18.2 Гц
    if (i8253.active[0] && (ct - p_last) >= PIT_IRQ0_US) {
        p_last = ct;
        doirq(0);          // wake от HLT, тики BIOS-таймера
    }
    // порт 3DA: бит3 (VBlank) и бит0 — грубая имитация ретрейса
    if ((ct - v_last) >= VGA_RETRACE_US) {
        v_last = ct;
        v_ph = (uint8_t)(v_ph ^ 1);
        port3da = v_ph ? 8u : 1u;
    }
}

/* ---- hostfs: файлов не открываем (образы — следующая итерация) ---- */
int hostfs_was_fallback_mode = 0;

/* i8259.c ссылается на keyboardwaitack (был в input.c — у нас без SDL) */
uint8_t keyboardwaitack = 0;

struct hh_file { int dummy; };

int hostfs_init ( void ) { return 0; }
HOSTFS_FILE *hostfs_open ( const char *fn, const char *mode )
{ (void)fn; (void)mode; return 0; }
HOSTFS_FILE *hostfs_open_with_feedback ( const char *fn, const char *mode, const char *msg )
{ (void)fn; (void)mode; (void)msg; return 0; }
int hostfs_load_binary ( const char *fn, void *buf, int min_size, int max_size, const char *msg )
{ (void)fn; (void)buf; (void)min_size; (void)max_size; (void)msg; return 0; }

size_t ms1504_hfs_read  ( HOSTFS_FILE *f, void *p, size_t size, size_t nmemb )
{ (void)f; (void)p; (void)size; (void)nmemb; return 0; }
size_t ms1504_hfs_write ( HOSTFS_FILE *f, const void *p, size_t size, size_t nmemb )
{ (void)f; (void)p; (void)size; (void)nmemb; return 0; }
int    ms1504_hfs_close ( HOSTFS_FILE *f ) { (void)f; return 0; }
int64_t ms1504_hfs_size ( HOSTFS_FILE *f ) { (void)f; return 0; }
int64_t ms1504_hfs_seek ( HOSTFS_FILE *f, int64_t offset, int whence )
{ (void)f; (void)offset; (void)whence; return -1; }