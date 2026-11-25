// 32x_host.c — host-слой PicoDrive для bare-metal H3 (ядро Sega 32X).
// Вендор ядра — picodrive/ (pico/ + cpu/), рома в /roms/sega32x/.
// Жизненный цикл: emu_run_32x -> sega32x_init_game -> цикл (sega32x_run_frame)
// -> sega32x_stop. Контракт emu.c как у прочих систем.
//
// PicoDrive скомпилирован как «голое ядро» БЕЗ libretro-обвязки:
//   - CPU 68000  -> Musashi (EMU_M68K)
//   - Z80        -> cz80   (_USE_CZ80)
//   - SH2 (32X)  -> интерпретатор (cpu/sh2/mame); dynarec — опция PICO_DRC=1
// Платформенные функции (plat_mmap/munmap/mremap, pm_*, elprintf) — здесь.
// Видео: ядро рисует RGB565 в EMU_FB через PicoDrawSetOutBuf (+ PDF_RGB555).
// Звук: 44100 Гц стерео -> линейный ресемплер 44100->48000 (фаза 16.16,
// непрерывная между кадрами) -> i2s_push_block.
// Ввод: Sega-геймпад 6-кнопочный, pad (PicoDrive) = MXYZ SACB RLDU.

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "pico/pico_int.h"   // ядро PicoDrive
#include "pico/pico.h"
// конвенция проекта
#include "../usb_kbd.h"
#include "../sega_pad.h"
#include "../btn_pad.h"
#include "../remap.h"
#include "../fb_text.h"
#include "../emu.h"
#include "../emulog.h"
#include "../i2s.h"
#include "../../include/h3_hs_timer.h"

// DRC: по умолчанию SH2 работает интерпретатором (cpu/sh2/mame/sh2pico.c).
// Опция сборки PICO_DRC=1 добавляет cpu/sh2/compiler.c + cpu/drc/{cmn,emit_arm}.c
// и -DDRC_SH2; тогда ниже включаются платформенные хуки кэша кода.

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240
// r1493: DE2 корректно читает 32-битные кадры (PXT), а 16-битные (RGB565)
// — шахматит (проверено 1:1 и ×2.68: битый DE2-16бит, CPU-копия чистая).
// Поэтому 32X рендерит RGB565 в EMU_FB, а в цикле кадр конвертируется в
// XRGB8888-буфер (32-бит) и отдаётся DE2 GSU напрямую — CPU освобождён от
// скейла, но формат для DE2 — проверенный 32-бит. Буфер — в пуле аркад.
static uint32_t s32x_rgb32[EMU_W * EMU_H] __attribute__((aligned(4), section(".rgb_hi")));

extern int printf(const char *fmt, ...);
extern int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

// ROM подаётся ИЗ ПАМЯТИ (без SD-чтения), файловый слой pm_* даёт ядро (cart.c).
void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed) {
    (void)addr; (void)need_exec; (void)is_fixed;
    return malloc(size);
}
void *plat_mremap(void *ptr, size_t oldsize, size_t newsize) {
    (void)ptr; (void)oldsize;
    return malloc(newsize);   // bump: новый блок, старый жив
}
void  plat_munmap(void *ptr, size_t size) {
    (void)ptr; (void)size;   // malloc-пул не возвращает отдельно; сброс при входе
}
#ifdef DRC_SH2
// ---- SH2 DRC (опция PICO_DRC=1) ----
// Свой блок не выделяем: cmn.c при NULL использует статический tcache_default
// (4 МБ BSS), второго буфера не нужно.
void *plat_mem_get_for_drc(size_t size) { (void)size; return NULL; }

// Сгенерированный код должен быть виден и как данные, и как инструкции:
// D-clean (MVA) по диапазону → DSB → инвалидация I-cache целиком → DSB/ISB.
static void drc_sync_code(uint32_t start, uint32_t end) {
    uint32_t a = start & ~0x1Fu;
    for (; a < end; a += 32)
        __asm volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(a));   // D-clean MVA
    __asm volatile("dsb" ::: "memory");
    __asm volatile("mcr p15, 0, %0, c7, c5, 0" :: "r"(0));        // ICIALLU
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
}

int plat_mem_set_exec(void *ptr, size_t size) {
    drc_sync_code((uint32_t)(uintptr_t)ptr, (uint32_t)(uintptr_t)ptr + (uint32_t)size);
    return 0;
}

// Вызывается эмиттером (emit_arm.c) на каждый сгенерированный блок.
void cache_flush_d_inval_i(void *start, void *end) {
    drc_sync_code((uint32_t)(uintptr_t)start, (uint32_t)(uintptr_t)end);
}
#else
void *plat_mem_get_for_drc(size_t size) { (void)size; return NULL; }
int   plat_mem_set_exec(void *ptr, size_t size) { (void)ptr; (void)size; return 0; }
#endif

// r1171: EL_STATUS-диагностика ядра PicoDrive. Макрос elprintf (pico_int.h,
// EL_LOGMASK=EL_STATUS) компилируется в вызовы lprintf — при пустой заглушке
// терялись «drc_cmn_init: …», «testing if we can run recompiled code..»,
// «test passed/failed» и аварийные сообщения ядра. Печатаем первые
// LPRINTF_MAX строк: для старта/DRC этого достаточно, поток кадровых
// сообщений UART не заливает (форвардим через vsnprintf — своей обёртки нет).
#define LPRINTF_MAX 64
void lprintf(const char *fmt, ...) {
    static int shown = 0;
    if (shown >= LPRINTF_MAX) return;
    shown++;
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) printf("%s", buf);
}

// платформенные хоки ядра (видео-режим для 32X не меняем, лог/ввод-события не нужны)
void emu_video_mode_change(int start_line, int line_count, int start_col, int col_count) {
    (void)start_line; (void)line_count; (void)start_col; (void)col_count;
}
void emu_32x_startup(void) { }

// CDDA-аудио (mp3/ogg) не используется для 32X-картриджа — пустые стабы.
int  mp3_get_bitrate(void *f, int len) { (void)f; (void)len; return 0; }
void mp3_start_play(void *f, int pos) { (void)f; (void)pos; }
void mp3_update(s32 *buffer, int length, int stereo) { (void)buffer; (void)length; (void)stereo; }
int  ogg_get_length(void *f) { (void)f; return 0; }
void ogg_start_play(void *f, int offset) { (void)f; (void)offset; }
void ogg_stop_play(void) { }
void ogg_update(s32 *buffer, int length, int stereo) { (void)buffer; (void)length; (void)stereo; }

// ---- данные host ----
static int g_loaded = 0;
static int g_frame_h = 224;                        // активная высота кадра: 224 (NTSC) / 240 (PAL)
static int g_fmt32x_ready = 0;                     // r1175: формат кадра 32X выставлен после стартапа
static short  sndbuf[4096];                        // выход ядра (стерео 44100)
// r1346 (Ш1.3): 1200 пар — запас на 48000/50 Гц PAL (960 пар/кадр) и на
// «догоняющие» кадры после провала; прежние 1024 пары (rsbuf[2048]) были
// ровно на границе, а maxpairs считался как sizeof/4 = 512 и резал вывод вдвое.
static int16_t rsbuf[2400];                        // ресемпл 48000 (1200 пар)
static uint32_t rs_phase = 0;                      // дробная позиция в кадре (16.16)

// ---- ресемплинг 44100 -> 48000 (линейный, фаза 16.16, непрерывная между кадрами) ----
// Шаг = (44100<<16)/48000 = 60211: на каждый выход фаза растёт на ~0.919 входа,
// поэтому из 735 входных пар кадра (44100/60) выходит ~800 (48000/60).
// r1161: прежняя версия съедала ровно один вход на выход (i++ в обеих ветках
// мёртвого if) и теряла целую часть фазы — реального ресемплинга не было,
// дефицит ~8.8% сэмплов накапливался (звук/видео уплывали по ходу игры).
static uint32_t picodrive_resample(const short *in, unsigned nf, int16_t *out, uint32_t maxpairs) {
    const uint32_t step = ((uint32_t)44100 << 16) / 48000;
    uint32_t np = 0;
    while (np < maxpairs) {
        uint32_t i = rs_phase >> 16;
        if (i + 1 >= nf) break;                    // нужны пары i и i+1
        uint32_t fr = rs_phase & 0xFFFFu;
        int32_t l0 = in[2*i],     r0 = in[2*i + 1];
        int32_t l1 = in[2*i + 2], r1 = in[2*i + 3];
        out[2*np]     = (int16_t)(l0 + (int32_t)(((int64_t)(l1 - l0) * (int32_t)fr) >> 16));
        out[2*np + 1] = (int16_t)(r0 + (int32_t)(((int64_t)(r1 - r0) * (int32_t)fr) >> 16));
        np++;
        rs_phase += step;
    }
    rs_phase &= 0xFFFFu;                           // дробную часть — на следующий кадр
    return np;
}

// ---- writeSound колбэк ядра (вызывается в конце кадра, len = байт стерео) ----
// r1346 (Ш1.3): maxpairs передавался как sizeof(rsbuf)/4 = 512, хотя за кадр
// 48000/60 = 800 пар. Остаток молча терялся каждый кадр (~35% сэмплов) —
// треск/недостача звука. Теперь передаём ёмкость в ПАРАХ (sizeof/4 байта
// на пару), rsbuf — 1200 пар с запасом на PAL (960) и на кадры с задержкой.
static void picodrive_snd_write(int len) {
    unsigned nf = (unsigned)len / 4;               // пар стерео (2 сэмпла * 2 байта)
    if (nf > (unsigned)(sizeof(sndbuf) / 4)) nf = (unsigned)(sizeof(sndbuf) / 4);
    uint32_t np = picodrive_resample(PicoIn.sndOut, nf, rsbuf,
                                     (uint32_t)(sizeof(rsbuf) / 4));
    if (np) i2s_push_block(rsbuf, np);
}

// ---- ввод: Sega 6-btn, формат PicoDrive MXYZ SACB RLDU ----
static void picodrive_poll_input(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    uint16_t pad = 0;
    uint16_t sp = pad_scan_combined();
    if (sp & 0x0001) pad |= 0x0001; // Up
    if (sp & 0x0002) pad |= 0x0002; // Down
    if (sp & 0x0004) pad |= 0x0004; // Left
    if (sp & 0x0008) pad |= 0x0008; // Right
    if (sp & 0x0080) pad |= 0x0080; // Start
    if (sp & 0x0010) pad |= 0x0040; // A
    if (sp & 0x0020) pad |= 0x0010; // B
    if (sp & 0x0040) pad |= 0x0020; // C
    if (sp & 0x0100) pad |= 0x0100; // X
    if (sp & 0x0200) pad |= 0x0200; // Y
    if (sp & 0x0400) pad |= 0x0400; // Z
    if (sp & 0x0800) pad |= 0x0800; // Mode
    // клавиатура через remap (платформа MD — как у 32X)
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_UP,    keys, n)) pad |= 0x0001;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_DOWN,  keys, n)) pad |= 0x0002;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_LEFT,  keys, n)) pad |= 0x0004;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_RIGHT, keys, n)) pad |= 0x0008;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_START, keys, n)) pad |= 0x0080;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_A,     keys, n)) pad |= 0x0040;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_B,     keys, n)) pad |= 0x0010;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_C,     keys, n)) pad |= 0x0020;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_X,     keys, n)) pad |= 0x0100;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_Y,     keys, n)) pad |= 0x0200;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_Z,     keys, n)) pad |= 0x0400;
    if (remap_kbd_pressed(REMAP_PLAT_MD, BTN_MODE,  keys, n)) pad |= 0x0800;
    PicoIn.pad[0] = pad;
    PicoIn.pad[1] = 0;
    PicoIn.pad[2] = 0;
    PicoIn.pad[3] = 0;
}

// ---- точка входа ----
int sega32x_init_game(const uint8_t* rom, uint32_t size) {
    printf("32X: init size=%u\n", (unsigned)size);
    g_loaded = 0;
    if (!rom || size == 0) { printf("32X: no rom\n"); return 0; }

    // Набор опций — как reference-конфиг (platform/common/emu.c:664-668) и
    // apply_renderer (platform/libretro/libretro.c:541-547): стерео + FM + PSG
    // + Z80 + 32X + PWM; ACC_SPRITES — быстрый рендер спрайтов, FM_YM2612 —
    // MD-синтез вместо OPLL, DIS_32C_BORDER — без рамки 32C. ALT_RENDERER и
    // SOFTSCALE для 32X не используются (эталон их снимает).
    // r1174: opt — глобальная переменная ядра, PicoExit её не чистит. Выставляем
    // набор с нуля, чтобы вход был детерминированным и состояние не переживало
    // сессию (в этой сборке PicoDrive обслуживает только 32X, но владелец один).
    PicoIn.opt = 0;
    PicoIn.opt |= POPT_EN_STEREO | POPT_EN_FM | POPT_EN_PSG | POPT_EN_Z80
                | POPT_EN_32X | POPT_EN_PWM
                | POPT_ACC_SPRITES | POPT_FM_YM2612 | POPT_DIS_32C_BORDER;
    PicoIn.opt &= ~(POPT_ALT_RENDERER | POPT_EN_SOFTSCALE);
#ifdef DRC_SH2
    // PICO_DRC=1: SH2 dynarec (runtime-бит ядра; при выключенной сборке не ставим).
    PicoIn.opt |= POPT_EN_DRC;
#endif
    // Регион: автоопределение по заголовку ROM (0), порядок US->EU->JP как в
    // эталоне (0x184). Присваиваем явно, чтобы не зависеть от стартового BSS.
    PicoIn.regionOverride = 0;
    PicoIn.autoRgnOrder = 0x184;

    // Звук настраивается ДО PicoInit: ядро инициализирует PSG/FM по sndRate.
    PicoIn.sndRate = 44100;
    PicoIn.sndOut  = sndbuf;
    PicoIn.writeSound = picodrive_snd_write;
    rs_phase = 0;
    // r1178: «нет низов, звук игрушечный» — виноват не ядро, а штатный ФВЧ
    // CPU2 в i2s.c (однополюсный high-pass, fc≈119 Гц): срезает бас и низ
    // барабанов. У GBA и Mega Drive (gpgx) DC-блокер уже отключается в их
    // init_game; для 32X этого не делалось (emu_prepare ставит bypass(0) на
    // входе). Внутренние фильтры ядра при этом выключены: POPT_EN_SNDFILTER
    // не выставлен, mix_reset(alpha=0) → полоса не режется; FM/PSG ресемплер
    // (polyphase FIR 53267→44100) остаётся как есть — это часть синтеза.
    i2s_dc_bypass(1);

    PicoInit();

    // r1175: построчный режим 32X — как эталон (platform/common/emu.c:1514:
    // PicoDrawSetOutFormat(PDF_RGB555, 1)). На момент init PicoIn.AHW ещё без
    // PAHW_32X (стартап идёт по записи в 0x5100), поэтому здесь выставится
    // FinalizeLine555; 32X-режим включим в run_frame, когда AHW уже 32X.
    PicoDrawSetOutFormat(PDF_RGB555, 1);
    g_fmt32x_ready = 0;
    PicoDrawSetOutBuf((void*)EMU_FB, EMU_W * 2);

    // звук: 44100 Гц стерео; буфер/колбэк выставлены до PicoInit
    PsndRerate(0);

    PicoSetInputDevice(0, PICO_INPUT_PAD_6BTN);

    // загрузка ROM из памяти (без pm_open): f==NULL, rom буфер в памяти
    unsigned char *prom = NULL; unsigned int psize = 0;
    if (PicoCartLoad(NULL, rom, size, &prom, &psize, 0)) {
        printf("32X: cart load failed\n");
        PicoExit();
        return 0;
    }
    PicoCartInsert(prom, psize, NULL);

    PicoLoopPrepare();
    PicoFrame();   // первый кадр (прогрев)

    // r1161: кадр 32X — 224 строки (NTSC) / 240 (PAL), период — по региону.
    // Раньше всегда скейлили 320x240 при throttle 60 Гц: NTSC-кадр растягивался
    // вместе с 16 «пустыми» строками, а PAL-игры шли ~20% быстрее.
    g_frame_h = Pico.m.pal ? 240 : 224;
    emu_set_period_us(Pico.m.pal ? 20000u : 16667u);

    g_loaded = 1;
    printf("32X: loaded %u bytes (%s, %d строк, period %u us)\n",
           (unsigned)size, Pico.m.pal ? "PAL" : "NTSC", g_frame_h,
           (unsigned)(Pico.m.pal ? 20000u : 16667u));
    return 1;
}

void sega32x_run_frame(void) {
    if (!g_loaded) return;
    // 32X активируется в рантайме (Pico32xStartup по записи в 0x5100 поднимает
    // PicoIn.AHW |= PAHW_32X). PicoDrawSetOutBuf(EMU_FB) при init (до стартапа)
    // уходит в MD-ветку и не настраивает DrawLineDestBase32x, поэтому 32X-слой
    // рисовал бы в DefOutBuff -> чёрный экран. Переустанавливаем буфер перед
    // каждым кадром: как только AHW содержит PAHW_32X, вызов идёт в ветку 32X
    // (PicoDrawSetOutBuf32X) и перенаправляет рендер в EMU_FB. По паттерну
    // reference emu_cmn_forced_frame (platform/common/emu.c:1515).
    // r1175: формат кадра тоже зависит от AHW. PicoDrawSetOutFormat(PDF_RGB555, 1)
    // до стартапа оставил FinalizeLine555 и Pico32xDrawMode = PDM32X_OFF — 32X-слой
    // не рисовался (чёрный экран у части игр, у остальных только MD-слой со сдвигом
    // на 8 строк V28). Как только стартап случился, включаем построчный режим
    // (FinalizeLine32xRGB555 читает line table из DRAM — вертикальный скролл верный).
    if (!g_fmt32x_ready && (PicoIn.AHW & PAHW_32X)) {
        PicoDrawSetOutFormat(PDF_RGB555, 1);
        g_fmt32x_ready = 1;
    }
    PicoDrawSetOutBuf((void*)EMU_FB, EMU_W * 2);
    picodrive_poll_input();
    PicoFrame();
    // видео и звук отдаются внутри PicoFrame (видео в EMU_FB, звук — колбэк)
}

void sega32x_render_frame(void) {
}

void sega32x_stop(void) {
    PicoExit();
    PicoCartUnload();
    g_loaded = 0;
}

// ---- точка входа emu.c для Sega 32X (по паттерну emu_run_megadrive) ----
void emu_run_32x(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    emulog_begin("32X");
    emulog_field("32X", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    snd_manifest("32x", "ym2612 pwm psg");
    if (sega32x_init_game(rom, size) != 1) {
        printf("32X: init failed\n");
        emulog_field("32X", "init", "FAILED");
        emulog_end("32X");
        return;
    }
    emulog_field("32X", "video", "320x%d", g_frame_h);
    emulog_field("32X", "machine", "%s", Pico.m.pal ? "PAL" : "NTSC");
    emulog_field("32X", "init", "OK");
    emu_set_border_color(0x0014161E);   // тёмно-графитовый (32X)
    // r1493: аппаратный путь — DE2 GSU растягивает 32-битную копию кадра на
    // всю высоту 600 (как программный emu_scale r1177): 224 → 857×600,
    // 240 → 800×600. Слой настраивается один раз, дальше DE2 читает
    // s32x_rgb32 по DMA; CPU только конвертирует RGB565→XRGB8888 (дёшево,
    // без интерполяции). Fallback — программный emu_scale из самого EMU_FB.
    extern int h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h,
                                     uint32_t bpp, uint32_t dx, uint32_t dy,
                                     uint32_t dw, uint32_t dh);
    extern void h3_de2_set_bkcolor(uint32_t argb);
    extern void usb_cache_clean(uint32_t addr, uint32_t size);
    int hw_scaler_ok;
    int diag_dx, diag_dy, diag_dw = 0, diag_dh = 0;
    {
        diag_dw = (EMU_W * 600) / g_frame_h;   // как emu_scale
        if (diag_dw > 1024) diag_dw = 1024;
        diag_dh = 600;
        diag_dx = (1024 - diag_dw) / 2;
        diag_dy = 0;
        hw_scaler_ok = (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)s32x_rgb32,
                                              EMU_W, g_frame_h, 32,
                                              diag_dx, diag_dy, diag_dw, diag_dh) == 0);
        if (hw_scaler_ok) {
            h3_de2_set_bkcolor(0xFF14161Eu);   // бордюр, как emu_set_border_color
            emulog_field("32X", "scaler", "DE2 GSU 32bpp 320x%d->%dx600",
                         g_frame_h, diag_dw);
        } else {
            emulog_field("32X", "scaler", "fallback CPU r1177");
        }
    }
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        sega32x_run_frame();
        emu_throttle();
        if (!hw_scaler_ok)
            emu_scale(EMU_W, g_frame_h);
        else {
            // RGB565 -> XRGB8888 (аппаратный upscale делает DE2; тут только
            // формат, без интерполяции). 320×240 = 76 800 пикселей ≈ 0.3 мс.
            const uint16_t* s = EMU_FB;
            uint32_t* d = s32x_rgb32;
            for (int i = EMU_W * EMU_H; i > 0; i--, s++, d++) {
                uint16_t p = *s;
                *d = (uint32_t)(((p >> 11) & 0x1Fu) << 19) |
                     (uint32_t)(((p >> 5)  & 0x3Fu) << 10) |
                     (uint32_t)((p & 0x1Fu) << 3);
            }
            usb_cache_clean((uint32_t)(uintptr_t)s32x_rgb32,
                            (uint32_t)EMU_W * EMU_H * 4u);
        }
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    sega32x_stop();
    // r1491: слой DE2 вернуть на буфер меню (иначе экран держит последний
    // кадр 32X и меню не видно) + бордюр обратно чёрный.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    emu_audio_stop();
    fb_clear(); fb_flush();
    emulog_end("32X");
}