// ngp_host.cpp — Neo Geo Pocket / Pocket Color (RACE core) for H3 bare-metal
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "emu.h"
#include "ngp_host.h"
#include "h3_hs_timer.h"

extern "C" {
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "cheatdb.h"
}

// Graphics buffer
#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// RACE forward declarations
#include "main.h"
#include "memory.h"
#include "tlcs900h.h"
#include "input.h"
#include "graphics.h"
#include "flash.h"
#include "neopopsound.h"
#include "neopop_blip.h"
#include "sound.h"
#include "i2s.h"

// BIOS ROM data (koyote.bin) — defines the koyote_bin symbol
#include "koyote_bin.h"

// Options stub for graphics.cpp Thor renderer
#define HICOLOR_OPTION 0
int options[8] = {0};

// RACE memory globals — memory.h declares them as extern;
// the definitions live here (see ngp_host.cpp design)
unsigned char __attribute__((aligned(4))) mainrom[4*1024*1024];
unsigned char __attribute__((aligned(4))) cpurom[256*1024];
unsigned char *cpuram = 0;
unsigned char __attribute__((aligned(4))) mainram[(64+32+128)*1024];
unsigned char realBIOSloaded = 0;

// drawBuffer — must be accessible as extern (graphics.cpp uses it)
// We define it with SIZEX pitch: each scanline uses SIZEX entries
unsigned short __attribute__((aligned(4))) drawBuffer[SIZEX * NGPC_SIZEY];

// Stub SDL surface
struct SDL_Surface { int w; int h; unsigned short* pixels; };
static SDL_Surface g_screen = { SIZEX, SIZEY, drawBuffer };
SDL_Surface* screen = &g_screen;
SDL_Surface* actualScreen = &g_screen;

// NGP state — m_bIsActive/m_emuInfo/m_sysInfo and ngpInputState are
// defined in main.cpp / input.cpp
extern int m_bIsActive;
extern EMUINFO m_emuInfo;
extern SYSTEMINFO m_sysInfo[NR_OF_SYSTEMS];
extern unsigned char ngpInputState;
BOOL mute = TRUE;

#define HOST_FPS 60

// NGP/NGPC state — ФИЗИЧЕСКАЯ раскладка регистра 0x6F82 (см. железу NGPC):
//   bit0=Up, bit1=Down, bit2=Left, bit3=Right, bit4=A, bit5=B, bit6=Start,
//   bit7=Option(Select). Up=0x01 Down=0x02 Left=0x04 Right=0x08 A=0x10 B=0x20
//   Start=0x40 Option=0x80.
// r168: имена KEY_* в main.h RACE СДВИНУТЫ (называют Start=0x10 и т.д.) —
// верить железу, а не им. Отсюда был «Start стреляет, B прыгает» в Metal Slug.
static int ngp_input_state(void) {
    uint8_t raw[6];
    int n = usb_kbd_get_raw(raw, 6);
    unsigned char state = 0;

    // Sega-геймпад: крестовина + A/B + Start + Mode->Option
    uint16_t sp = pad_scan_combined();
    if (sp & 0x0001) state |= 0x01;   // Up
    if (sp & 0x0002) state |= 0x02;   // Down
    if (sp & 0x0004) state |= 0x04;   // Left
    if (sp & 0x0008) state |= 0x08;   // Right
    if (sp & 0x0010) state |= 0x10;   // Sega A -> NGPC A
    if (sp & 0x0020) state |= 0x20;   // Sega B -> NGPC B
    if (sp & 0x0080) state |= 0x40;   // Sega Start -> NGPC Start
    if (sp & 0x0800) state |= 0x80;   // Sega Mode -> NGPC Option (Select)

    // Клавиатура -> NGP: та же раскладка (A=0x10, B=0x20, Start=0x40,
    // Select=0x80); ремап через Settings → Keyboard remap.
    if (remap_kbd_pressed(REMAP_PLAT_NGP, BTN_UP, raw, n))    state |= 0x01;
    if (remap_kbd_pressed(REMAP_PLAT_NGP, BTN_DOWN, raw, n))  state |= 0x02;
    if (remap_kbd_pressed(REMAP_PLAT_NGP, BTN_LEFT, raw, n))  state |= 0x04;
    if (remap_kbd_pressed(REMAP_PLAT_NGP, BTN_RIGHT, raw, n)) state |= 0x08;
    if (remap_kbd_pressed(REMAP_PLAT_NGP, BTN_A, raw, n))     state |= 0x10;
    if (remap_kbd_pressed(REMAP_PLAT_NGP, BTN_B, raw, n))     state |= 0x20;
    if (remap_kbd_pressed(REMAP_PLAT_NGP, BTN_START, raw, n)) state |= 0x40;
    if (remap_kbd_pressed(REMAP_PLAT_NGP, BTN_SELECT, raw, n)) state |= 0x80;
    return state;
}

void UpdateInputState(void) {
    ngpInputState = ngp_input_state();
}

// Stub SDL
extern "C" void SDL_LockSurface(void*) {}
extern "C" void SDL_UnlockSurface(void*) {}
extern "C" unsigned int SDL_GetTicks(void) {
    return h3_hs_timer_lo_us() / 1000;
}

// Sound stubs — only for functions not provided by neopopsound.cpp / sound.cpp
int initSound() { return 0; }
void soundCleanup() {}

// soundOutput(): разово вызывается из ngp_run_frame() раз в кадр.
// r628/r629: NGP звук синтезируем на 44100 (эталонный default RETRO_SAMPLE_RATE),
// затем ресемплим 44100→48000 для I2S. Основная причина «нарастающего шума»:
// DAC conv (5/6) в neopopsound.c рассчитан под 44100/8000≈5.5; при синтезе на
// 48000 conv неверен → DAC-буфер накапливается и выдаёт непрерывный шум,
// усиливающийся с числом звуков. На 44100 conv корректен.
extern int neopop_audio_accurate;
void soundOutput() {
    static int snd_inited = 0;
    if (!snd_inited) {
        sound_init(44100);
        system_sound_chipreset(44100);
        snd_inited = 1;
    }

    // Fast (per-sample) путь — эталонный default (rare_audio_quality='fast').
    // Число входных пар за кадр считаем по ФАКТИЧЕСКОЙ длительности кадра NGP
    // (16200 мкс = 61.7 Гц), НЕ по 60 Гц: 44100/61.7 = 715. Раньше было 735
    // (44100/60) — звук опережал кадр, на стыках кадров возникали периодические
    // клики (~52 Гц). Теперь синтез по времени кадра, ресемпл 44100→48000
    // даёт ровно столько, сколько проигрывает I2S за кадр.
    static _u16 sampleBuffer[900];
    const int in_n = 715;   // 44100/61.7 (фактический кадр NGP)
    sound_update(sampleBuffer, in_n * (int)sizeof(sampleBuffer[0]));
    dac_update(sampleBuffer, in_n * (int)sizeof(sampleBuffer[0]));

    static uint32_t rs_phase = 0;
    int o = 0;
    static int16_t out[900];
    while (o < 900) {
        uint32_t i = rs_phase >> 16;
        if (i + 1 >= (uint32_t)in_n) break;
        uint32_t f = rs_phase & 0xFFFFu;
        int32_t m0 = (int32_t)(int16_t)sampleBuffer[i];
        int32_t m1 = (int32_t)(int16_t)sampleBuffer[i + 1];
        int32_t s = m0 + (int32_t)(((int64_t)(m1 - m0) * (int32_t)f) >> 16);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[o++] = (int16_t)s;
        rs_phase += 60211u;   // (44100<<16)/48000
    }
    if (rs_phase >= ((uint64_t)in_n << 16))
        rs_phase = (uint32_t)(rs_phase - ((uint64_t)in_n << 16));
    else
        rs_phase = 0;
    for (int k = 0; k < o; k++)
        i2s_push_sample(out[k], out[k]);   // моно → стерео
}
BOOL system_sound_init(void) { return TRUE; }
void system_VBL(void) {}
void dac_write(unsigned char) {}
void increaseVolume() {}
void decreaseVolume() {}
void writeSaveGameFile() {}

// BIOS font
extern unsigned char sysfont[8*256];
extern void ngpBiosSYSFONTSET(unsigned char *pt, char trans, char font);

static void blit_to_fb(void) {
    for (int y = 0; y < NGPC_SIZEY; y++) {
        for (int x = 0; x < NGPC_SIZEX; x++) {
            EMU_FB[y * EMU_W + x] = drawBuffer[y * SIZEX + x];
        }
    }
}

extern "C" int ngp_init_game(const uint8_t* rom, uint32_t size) {
    if (!rom || size == 0) return 0;
    if (size > 4*1024*1024) size = 4*1024*1024;

    i2s_dc_shift_set(6);   // r597: NGP — непрерывный поток → ~120 Гц (как Lynx)

    memset(mainrom, 0, sizeof(mainrom));
    // r502 fix: верхний 16Mbit-слот читается как mainrom[0x200000..] — у файла
    // <4МБ там нули. В RACE-эталоне незанятый ROM/стёртая flash — 0xFF.
    // Заполняем хвост 0xFF, чтобы «пустой» банк был как стёртый чип.
    memcpy(mainrom, rom, size);
    if (size < sizeof(mainrom))
        memset(mainrom + size, 0xFF, sizeof(mainrom) - size);
    // r502 fix: игры типа Sonic (2МБ = 2×8Mbit) читают «верхний 16Mbit» слот
    // (0x800000+) как вторую половину рома; он мапится на mainrom[0x200000..].
    // Кладём туда вторую половину файла. Для 4МБ (MS2) это no-op, для 1МБ —
    // «верхний» не читается.
    if (size > 1u * 1024 * 1024 && size < sizeof(mainrom)) {
        uint32_t half = size / 2;
        memcpy(mainrom + 2u * 1024 * 1024, mainrom + half, size - half);
    }

    memset(cpurom, 0, sizeof(cpurom));
    memset(mainram, 0, sizeof(mainram));
    // r0.410 (S8): drawBuffer не очищался при повторном входе — 1-2 кадра
    // хвоста предыдущей игры при быстром перезапуске NGP.
    memset(drawBuffer, 0, sizeof(drawBuffer));

    // Init palette lookup table
    for (int r = 0; r < 32; r++)
        for (int g = 0; g < 32; g++)
            for (int b = 0; b < 32; b++)
                totalpalette[b*256 + g*16 + r] = (r << 10) | (g << 5) | b;

    m_bIsActive = FALSE;
    initSysInfo();
    m_emuInfo.romSize = size;
    strcpy(m_emuInfo.RomFileName, "rom");

    // Детекция машины должна идти ДО mainemuinit() — как в эталонном RACE:
    // initRom() вызывает SetEmu(m) перед mainemuinit(), чтобы mem_init() и
    // graphics_init() сразу знали NGP / NGPC.
    if (!initRom()) {
        printf("NGP: init failed (bad ROM?)\n");
        return 0;
    }
    setFlashSize(size);
    printf("NGP: %s size=%d\n",
           (m_emuInfo.machine == NGPC) ? "NGPC" : "NGP", (int)size);
    return 1;
}

extern "C" void ngp_run_frame(void) {
    if (!m_bIsActive) return;
    // RAW-читы: пишем байт каждый кадр через tlcsMemWriteB (разруливает
    // карту памяти: cpuram/mainram/flash). Резерв — tlcsMemReadB для cmp.
    int rc = cheats_raw_count();
    for (int i = 0; i < rc; i++) {
        uint32_t a; uint8_t v, c; int hc;
        if (cheats_raw_get(i, &a, &v, &c, &hc)) {
            if (!hc || tlcsMemReadB(a) == c) tlcsMemWriteB(a, v);
        }
    }
    // Полный кадр NGPC = 198 сканлайнов x 515 тактов = 101970.
    // Важно: ровно один кадр, БЕЗ запаса — иначе дрейф ~50 линий/с
    // и чёрная полоса ползёт снизу вверх. Фаза стабильна (ngOverflow).
    // Один блит в кадр делает graphics_paint() при scanlineY==151 (VBlank),
    // когда все 152 строки уже нарисованы — здесь НЕ блинкуем повторно.
    tlcs_execute(515 * 198);
    soundOutput();   // r502/r631: тик звуковых чипов каждый кадр (вывод в I2S)
}

// Graphics override for graphics_paint — must be C-linkage
extern "C" void graphics_paint_impl(void);
void graphics_paint_impl(void) {
    // Called from graphics.cpp at VBlank — blits drawBuffer to EMU_FB
    blit_to_fb();
}