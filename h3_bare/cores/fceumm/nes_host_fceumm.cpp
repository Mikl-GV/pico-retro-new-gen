// nes_host_fceumm.cpp — host-слой FCEUmm (FCE Ultra) для bare-metal H3.
// Замена InfoNES: точный CPU/PPU, 250+ мапперов, поддержка клавиатуры
// SuborKB (Сюбор-картриджи с клавиатурой, SIFC_SUBORKB).
//
// Жизненный цикл:
//   emu_run_nes(rom, size, name)
//     -> fceumm_init_game: gb_heap_reset + FCEUI_Initialize + FCEUI_LoadGame
//     -> цикл: build_input() -> FCEUI_Emulate() -> рендер в EMU_FB
//     -> fceumm_stop: FCEUI_CloseGame + FCEUI_Kill

#include <stdint.h>
#include <string.h>

extern "C" {
#include "fceu.h"
#include "driver.h"
#include "video.h"
#include "palette.h"
#include "cart.h"
#include "ppu.h"
#include "x6502.h"
#include "sound.h"
#include "fceu-memory.h"
#include "general.h"
#include "git.h"
#include "usb_kbd.h"
#include "fb_text.h"
#include "emu.h"
}

extern "C" int printf(const char* fmt, ...);
extern "C" void gb_heap_reset(void);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// ---- состояние host-слоя ----
static int g_loaded = 0;

// буферы ввода (их адреса регистрируются в ядре через FCEUI_SetInput*)
static uint32_t g_joydata = 0;              // 1-я кнопка в младшем байте
static uint8_t  g_suborkb[0x65];            // клавиатура SuborKB

// палитра 512 записей (FCEUD_SetPalette заполняет; индексы как в libretro.c)
static uint16_t nes_pal_rgb565[512];

// ---- глобалы, которые в оригинале определял libretro.c (драйвер) ----
extern "C" {
unsigned dendy = 0;
unsigned overclocked = 0;
unsigned overclock_enabled = (unsigned)-1;
unsigned skip_7bit_overclocking = 1;
unsigned normal_scanlines = 240;
unsigned totalscanlines = 0;
unsigned vblankscanlines = 0;
unsigned extrascanlines = 0;
unsigned swapDuty = 0;

// Debug-лента: при трансляции линкер просит printf — он уже есть в printf.c

// Заглушки вместо nsf.c (тянет math-библиотеку, не нужна в bare-metal)
int NSFLoad(void* fp) { return 0; }
void DrawNSF(uint8_t* XBuf) { (void)XBuf; }
void DoNSFFrame(void) {}

// Заглушка для boards/transformer.c (ключи не транслируем)
const char* GetKeyboard(void) { return ""; }
}

// ---- FCEUD-колбэки (требует driver.h) ----
extern "C" void FCEUD_SetPalette(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 512)
        nes_pal_rgb565[index] = (r >> 3 << 11) | (g >> 2 << 5) | (b >> 3);
}

extern "C" void FCEUD_PrintError(const char* s) { printf("FCEU: %s\n", s); }
extern "C" void FCEUD_Message(const char* s) { printf("%s", s); }
extern "C" void FCEUD_DispMessage(enum retro_log_level l, unsigned d, const char* s) {
    (void)l; (void)d; (void)s;
}

// ---- ввод: USB-клавиатура -> геймпад + SuborKB ----
static void build_input(void) {
    // геймпад player1 (байт в младших 8 битах JSReturn):
    // Up=0x10 Down=0x20 Left=0x40 Right=0x80 A=0x01 B=0x02 Sel=0x04 Start=0x08
    g_joydata = 0;
    memset(g_suborkb, 0, sizeof(g_suborkb));

    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        if (sc == 82) g_joydata |= 0x10;
        if (sc == 81) g_joydata |= 0x20;
        if (sc == 80) g_joydata |= 0x40;
        if (sc == 79) g_joydata |= 0x80;
        if (sc == 29) g_joydata |= 0x01;   // Z = A
        if (sc == 27) g_joydata |= 0x02;   // X = B
        if (sc == 22) g_joydata |= 0x04;   // S = Select
        if (sc == 40) g_joydata |= 0x08;   // Enter = Start
    }

    // SuborKB: индекс в g_suborkb = позиция сканкода в suborkbmap (0x65 записей)
    // Ключевые для Сюбор-картриджей: буквы, цифры, Enter, Backspace, стрелки.
    // Маппинг USB HID scancode -> позиция в suborkbmap:
    static const struct { uint8_t usb; uint8_t sub; } kbmap[] = {
        { 44, 0x3E },   // Space
        { 40, 0x28 },   // Enter (RETURN)
        { 42, 0x1B },   // Backspace
        { 41, 0x30 },   // Esc
        { 82, 0x2C },   // Up
        { 81, 0x40 },   // Down
        { 80, 0x2F },   // Left
        { 79, 0x33 },   // Right
        { 30, 0x05 }, { 31, 0x0A }, { 32, 0x0F }, { 33, 0x14 },  // 1 2 3 4
        { 34, 0x19 }, { 35, 0x1E }, { 36, 0x3A }, { 37, 0x3F },  // 5 6 7 8
        { 38, 0x44 }, { 39, 0x34 },                              // 9 0
        { 4, 0x35 }, { 5, 0x16 }, { 6, 0x2D }, { 7, 0x0B },      // A B C D
        { 8, 0x06 }, { 9, 0x1F }, { 10, 0x2A }, { 11, 0x20 },    // E F G H
        { 12, 0x0C }, { 13, 0x36 }, { 14, 0x21 }, { 15, 0x2E },  // I J K L
        { 16, 0x10 }, { 17, 0x0D }, { 18, 0x02 }, { 19, 0x07 },  // M N O P
        { 20, 0x22 }, { 21, 0x3B }, { 22, 0x17 }, { 23, 0x34 },  // Q R S T
        { 24, 0x03 }, { 25, 0x08 }, { 26, 0x31 }, { 27, 0x26 },  // U V W X
        { 28, 0x11 }, { 29, 0x15 },                              // Y Z
        { 0, 0 }
    };
    for (int i = 0; i < n; i++) {
        for (int k = 0; kbmap[k].usb; k++) {
            if (keys[i] == kbmap[k].usb) { g_suborkb[kbmap[k].sub] = 1; break; }
        }
    }
    // Shift (левая/правая) => SuborKB позиция LSHIFT 0x47
    uint8_t mods = usb_kbd_get_mods();
    if (mods & 0x02) g_suborkb[0x47] = 1;   // LShift
    if (mods & 0x20) g_suborkb[0x47] = 1;   // RShift -> тот же
    // CapsLock
    if (mods & 0x04) g_suborkb[0x1A] = 0;   // не маппим CapsLock/Alt детально
}

// ---- Public API ----
extern "C" int fceumm_init_game(const uint8_t* rom, uint32_t size) {
    printf("FCEUmm: init size=%u\n", (unsigned)size);
    g_loaded = 0;

    // Сброс общего bump-пула (malloc из gameboy_stubs.c): перед каждым
    // запуском NES — иначе повторные init копят XBuf/ROM и упрутся в 3 МБ.
    gb_heap_reset();

    if (!FCEUI_Initialize()) {
        printf("FCEUmm: FCEUI_Initialize failed\n");
        return 0;
    }

    FCEUGI* gi = FCEUI_LoadGame("rom", rom, size, NULL);
    if (!gi) {
        printf("FCEUmm: load failed (bad/missing mapper?)\n");
        FCEUI_Kill();
        return 0;
    }

    // Геймпады 1 и 2
    FCEUI_SetInput(0, SI_GAMEPAD, &g_joydata, 0);
    FCEUI_SetInput(1, SI_GAMEPAD, &g_joydata, 0);

    // FC-порт: принудительно SuborKB — картриджи Сюбора с клавиатурой.
    // Если ROM требует другой девайс (FKB и т.п.) — можно расширить.
    FCEUI_SetInputFC(SIFC_SUBORKB, g_suborkb, 0);

    // Звук не нужен (нет DAC-вывода) — отключаем, чтобы не аллоцировать буферы
    FCEUI_Sound(0);

    g_loaded = 1;
    printf("FCEUmm: type=%d inputfc=%d\n", gi->type, gi->inputfc);
    return 1;
}

extern "C" void fceumm_run_frame(void) {
    if (!g_loaded) return;

    build_input();

    uint8_t* gfx = NULL;
    int32_t* snd = NULL;
    int32_t ssize = 0;
    FCEUI_Emulate(&gfx, &snd, &ssize, 0);
    if (!gfx) return;

    // Рендер: gfx = XBuf[256×240] индексов палитры.
    // Деэмфазис строки из XDBuf: база 256 + (deemp&7)<<6, иначе база 0.
    extern uint8_t *XDBuf;
    for (int y = 0; y < 240; y++) {
        uint8_t deemp = XDBuf ? XDBuf[y * 256] : 0;
        uint32_t base = deemp ? (256u + ((unsigned)(deemp & 0x07) << 6)) : 0;
        const uint8_t* src = gfx + y * 256;
        uint16_t* dst = EMU_FB + y * EMU_W;
        for (int x = 0; x < 256; x++)
            dst[x] = nes_pal_rgb565[base + (src[x] & 0xFF)];
    }
}

// ---- пауза/стоп (освобождение) ----
extern "C" void fceumm_stop(void) {
    if (!g_loaded) return;
    FCEUI_CloseGame();
    FCEUI_Kill();
    g_loaded = 0;
    printf("FCEUmm: stopped\n");
}

// ---- точка входа emu.c (совместима со старым emu_run_nes) ----
extern "C" void emu_run_nes(const uint8_t* rom, uint32_t size, const char* rom_name) {
    (void)rom_name;
    fb_clear(); fb_flush();
    if (fceumm_init_game(rom, size) != 1) {
        printf("NES(FCEUmm): init failed\n");
        return;
    }
    uint8_t raw_keys[6];
    for (;;) {
        fceumm_run_frame();
        emu_throttle();
        emu_scale(256, 240);
        fb_flush();
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++)
            if (raw_keys[i] == 41) goto exit;   // ESC — выход
    }
exit:
    fceumm_stop();
    fb_clear(); fb_flush();
}