// msx_host.c — host-слой порта fMSX для bare-metal H3.
// Включает кадровый цикл, рендер (PutImage), ввод (Sega-геймпад + USB-клава).
//
// Регистры Z80 и слоты: Установка Mode=MSX_MSX2|MSX_PAL, RAMPages=4, VRAMPages=4
// (128K+64K) — V9938 MSX2-совместимость. ROM-дампы вшиты (MSX2.ROM + MSX2EXT.ROM).

#include <stdint.h>
#include <string.h>

// ---- ядро fMSX (ДО Common.h: Common.h использует макросы/глобалы из MSX.h) ----
#include "../fMSX/MSX.h"
#include "../EMULib/Sound.h"
#include "../EMULib/EMULib.h"

#include "../fMSX/V9938.h"

// ---- макросы для рендера (должны быть до Common.h/Wide.h) ----
#define SND_RATE 48000
#define BORDER 8
#define WIDTH  (256 + (BORDER << 1))
#define HEIGHT (212 + (BORDER << 1))
#define MAX_HEIGHT   (256 + BORDER)
#define MAX_SCANLINE (255)

#define PIXEL(R,G,B) (uint16_t)((((31*(R)/255)<<11)|((63*(G)/255)<<5)|(31*(B)/255)))

uint16_t XPal[80];
uint16_t BPal[256];
uint16_t XPal0;
int      LastScanline;
int      OverscanMode = 0;
unsigned frame_number = 0;

#define HiResMode        (0)
#define InterlacedMode   (0)
#define OverscanMode     (OverscanMode)
#define OddPage          (frame_number & 1)

// Буфер кадра: плоский массив. В 512-пиксельных режимах (SCREEN6/7)
// Wide.h использует stride 2*WIDTH слов/строку — буфер должен вмещать
// максимум: 2*WIDTH * MAX_HEIGHT. Для 256-режимов stride = WIDTH.
static uint16_t image_buffer[2 * WIDTH * MAX_HEIGHT];
unsigned image_buffer_width  = WIDTH;
unsigned image_buffer_height = HEIGHT;

#define XBuf image_buffer
#define WBuf image_buffer

#include "../fMSX/Common.h"
#include "../fMSX/Wide.h"

#include "msx_compat.h"
#include "sega_pad.h"
#include "usb_kbd.h"
#include "fb_text.h"
#include "emu.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// Состояние
static int g_loaded = 0;

// ---- SetColor — вызывается ядром для установки цвета палитры XPal ----
void SetColor(uint8_t N, uint8_t R, uint8_t G, uint8_t B) {
    if (N < 80) XPal[N] = PIXEL(R, G, B);
    if (N == 0) XPal0 = XPal[0];  // цвет фона
}

// ---- Joystick — вызывается ядром на строке 192 каждого кадра ----
unsigned int Joystick(void) {
    uint16_t pad = sega_pad_scan();
    unsigned int js = 0;
    if (pad & 0x0001) js |= JST_UP;
    if (pad & 0x0002) js |= JST_DOWN;
    if (pad & 0x0004) js |= JST_LEFT;
    if (pad & 0x0008) js |= JST_RIGHT;
    if (pad & 0x0010) js |= JST_FIREA;   // A = fire
    if (pad & 0x0020) js |= JST_FIREB;   // B = fire2
    if (pad & 0x0080) js |= JST_FIREA;    // Start = fire тоже
    return js;
}

// ---- Mouse — заглушка ----
unsigned int Mouse(uint8_t N) { (void)N; return 0; }

// ---- PlayAllSound — заглушка (без звука) ----
void PlayAllSound(int uSec) { (void)uSec; }

// ---- PutImage — перенос image_buffer в EMU_FB (как libretro.c) ----
// Широкие режимы V9938: SCREEN 6, SCREEN 7, TEXT80 (MAXSCREEN+1) — рендер
// (Wide.h RefreshBorder512/RefreshLineTx80) пишет строки с шагом 2*WIDTH.
// Если копировать такие строки с шагом WIDTH — каждая строка двоится.
static void update_image_buffer_size(uint8_t screen_mode) {
    if ((screen_mode == 6) || (screen_mode == 7) || (screen_mode == MAXSCREEN + 1))
        image_buffer_width = WIDTH << 1;   // 544
    else
        image_buffer_width = WIDTH;        // 272
    image_buffer_height = HEIGHT;          // 228
}

void PutImage(void) {
    update_image_buffer_size(ScrMode);
    int iw = (int)image_buffer_width;     // 272 или 544
    int ih = (int)image_buffer_height;    // 228
    if (iw <= 0) iw = WIDTH;
    if (ih <= 0) ih = HEIGHT;

    // EMU_FB фиксированной ширины 320. Если рендер широкий (544) —
    // сжимаем по X nearest-neighbour (аккумулятор 16.16, без делений в цикле).
    if (iw <= EMU_W) {
        // Узкий режим: прямое копирование
        for (int y = 0; y < ih && y < EMU_H; y++) {
            const uint16_t* src = &image_buffer[y * iw];
            uint16_t* dstv = EMU_FB + y * EMU_W;
            for (int x = 0; x < iw; x++)
                dstv[x] = src[x];
        }
    } else {
        // Широкий режим: ikx — индекс исходной колонки для каждой целевой
        uint32_t step = ((uint32_t)iw << 16) / (uint32_t)EMU_W;
        uint32_t acc = step >> 1;
        int sx_tab[EMU_W];
        for (int x = 0; x < EMU_W; x++) {
            sx_tab[x] = (int)(acc >> 16);
            acc += step;
            if (acc >= ((uint32_t)iw << 16)) acc -= (uint32_t)iw << 16;
        }
        for (int y = 0; y < ih && y < EMU_H; y++) {
            const uint16_t* src = &image_buffer[y * iw];
            uint16_t* dstv = EMU_FB + y * EMU_W;
            for (int x = 0; x < EMU_W; x++)
                dstv[x] = src[sx_tab[x]];
        }
    }
    frame_number++;
}

// ---- WriteAudio — заглушка ----
unsigned int WriteAudio(int16_t *Data, unsigned int Length) {
    (void)Data;
    return Length;  // говорим «всё записано»
}

// ---- DiskPresent/DiskRead/DiskWrite — заглушки (диски не поддерживаются) ----
uint8_t DiskPresent(uint8_t ID) { (void)ID; return 0; }
uint8_t DiskRead(uint8_t ID, uint8_t *Buf, int N) { (void)ID; (void)Buf; (void)N; return 0; }
uint8_t DiskWrite(uint8_t ID, const uint8_t *Buf, int N) { (void)ID; (void)Buf; (void)N; return 0; }

// ---- public API ----

int msx_init_game(const uint8_t* rom, uint32_t size) {
    printf("MSX: init size=%u\n", (unsigned)size);
    g_loaded = 0;

    // Очищаем обращения к глобальным переменным через TrashMSX (если был предыдущий запуск)
    TrashMSX();

    // Инициализация переменных ядра
    Mode     = 0;
    RAMPages = 0;
    VRAMPages = 0;
    UPeriod  = 100;   // 100% кадра — рисуем всегда
    ExitNow  = 0;

    // MSX_MSXDOS2: ядро попытается загрузить MSXDOS2.ROM (с SD /roms/msx/bios/).
    // Если файла нет — LoadCart вернёт 0, и это не страшно (остаёмся в BASIC).
    int NewMode = MSX_MSX2 | MSX_PAL | MSX_MSXDOS2 | MSX_GUESSA | MSX_GUESSB;   // Ямаха YIS-503II — PAL (Европа, 50 Гц, 313 строк)
    int RAMpg = 4;    // 128 КБ
    int VRAMpg = 4;   // 128 КБ VRAM (V9938)

    // Устанавливаем режим, грузим BIOS из встроенных дампов
    if (!StartMSX(NewMode, RAMpg, VRAMpg)) {
        printf("MSX: StartMSX FAILED\n");
        return 0;
    }

    // Если был передан образ картриджа — загружаем в слот A.
    // LoadCart() в ядре делает rfopen("CARTA.ROM"), поэтому даём стабу
    // валидный источник через msx_compat_set_cart() (буфер в памяти).
    if (rom && size > 0) {
        msx_compat_set_cart(rom, size);
        // StartMSX уже загрузил системные картриджи (MSXDOS2 и т.д.),
        // но пользовательский ROMName[0]="CARTA.ROM" грузится ТОЛЬКО
        // в StartMSX (строка 592). Он уже выполнен. Поэтому повторно:
        LoadCart("CARTA.ROM", 0, ROMGUESS(0) | ROMTYPE(0));
    }

    printf("MSX: Mode=%08X RAM=%d VRAM=%d\n", (unsigned)Mode, RAMPages, VRAMPages);
    g_loaded = 1;
    return 1;
}

// ---- маппинг HID-сканкода USB-клавиатуры в fMSX-код (KBD_*) ----
// USB клавиатура отдаёт сырые HID usage (0x04=a ... 0x1D=z, 0x28=Enter...).
// fMSX-код = индекс в Keys[][]; для ASCII это сам символ (0x21-0x7F).
// Возвращает fMSX-код или -1 (не мапится).
static int hid_to_fmsx(uint8_t sc) {
    switch (sc) {
    case 0x04: return 'a';   case 0x05: return 'b';   case 0x06: return 'c';
    case 0x07: return 'd';   case 0x08: return 'e';   case 0x09: return 'f';
    case 0x0A: return 'g';   case 0x0B: return 'h';   case 0x0C: return 'i';
    case 0x0D: return 'j';   case 0x0E: return 'k';   case 0x0F: return 'l';
    case 0x10: return 'm';   case 0x11: return 'n';   case 0x12: return 'o';
    case 0x13: return 'p';   case 0x14: return 'q';   case 0x15: return 'r';
    case 0x16: return 's';   case 0x17: return 't';   case 0x18: return 'u';
    case 0x19: return 'v';   case 0x1A: return 'w';   case 0x1B: return 'x';
    case 0x1C: return 'y';   case 0x1D: return 'z';
    case 0x1E: return '1';   case 0x1F: return '2';   case 0x20: return '3';
    case 0x21: return '4';   case 0x22: return '5';   case 0x23: return '6';
    case 0x24: return '7';   case 0x25: return '8';   case 0x26: return '9';
    case 0x27: return '0';
    case 0x28: return KBD_ENTER;
    case 0x29: return KBD_ESCAPE;
    case 0x2A: return KBD_BS;
    case 0x2B: return KBD_TAB;
    case 0x2C: return KBD_SPACE;
    case 0x2D: return '-';   case 0x2E: return '=';   case 0x2F: return '[';
    case 0x30: return ']';   case 0x31: return '\\';
    case 0x33: return ';';   case 0x34: return '\'';  case 0x35: return '`';
    case 0x36: return ',';   case 0x37: return '.';   case 0x38: return '/';
    case 0x39: return KBD_CAPSLOCK;
    case 0x3A: return KBD_F1;  case 0x3B: return KBD_F2;
    case 0x3C: return KBD_F3;  case 0x3D: return KBD_F4;  case 0x3E: return KBD_F5;
    case 0x49: return KBD_INSERT; case 0x4A: return KBD_HOME;
    case 0x4C: return KBD_DELETE;
    case 0x4F: return KBD_RIGHT; case 0x50: return KBD_LEFT;
    case 0x51: return KBD_DOWN;  case 0x52: return KBD_UP;
    case 0x53: return KBD_NUMPAD1; case 0x54: return KBD_NUMPAD2;
    case 0x55: return KBD_NUMPAD3; case 0x56: return KBD_NUMPAD4;
    case 0x57: return KBD_NUMPAD5; case 0x58: return KBD_NUMPAD6;
    case 0x59: return KBD_NUMPAD7; case 0x5A: return KBD_NUMPAD8;
    case 0x5B: return KBD_NUMPAD9; case 0x5C: return KBD_NUMPAD0;
    case 0x5D: return KBD_NUMDOT;  case 0x5F: return KBD_NUMMUL;
    case 0x60: return KBD_NUMMINUS; case 0x61: return KBD_NUMPLUS;
    case 0x62: return KBD_NUMDIV;
    case 0xE0: return KBD_SHIFT; case 0xE4: return KBD_SHIFT;
    case 0xE1: return KBD_CONTROL; case 0xE5: return KBD_CONTROL;
    case 0xE2: return KBD_GRAPH;   case 0xE6: return KBD_GRAPH;
    default: return -1;
    }
}

void msx_run_frame(void) {
    if (!g_loaded) return;

    // fMSX вызывает Joystick() на строке 192 внутри LoopZ80.
    // Клавиатура: обновляем KeyState[] раз в кадр.
    // В libretro.c все клавиши сбрасываются на каждом кадре, потом
    // устанавливаются только зажатые — так же делаем здесь.

    // 1) Сбросить все клавиши ядра (KBD_RES для всех определённых)
    for (int i = 0; i < 137; i++) {
        // KeyState[Keys[i][0]] |= Keys[i][1] — очищаем маску
        KeyState[Keys[i][0]] |= Keys[i][1];
    }

    // 2) Модификаторы (из первого байта boot-отчёта)
    uint8_t mods = usb_kbd_get_mods();
    if (mods & 0x02) { KeyState[6] &= ~0x01; } // LShift
    if (mods & 0x20) { KeyState[6] &= ~0x01; } // RShift
    if (mods & 0x01) { KeyState[6] &= ~0x02; } // LCtrl
    if (mods & 0x10) { KeyState[6] &= ~0x02; } // RCtrl
    if (mods & 0x04) { KeyState[6] &= ~0x04; } // LAlt → GRAPH
    if (mods & 0x40) { KeyState[6] &= ~0x04; } // RAlt → GRAPH

    // 3) Сырые сканкоды USB
    uint8_t keys[8];
    int n = usb_kbd_get_raw(keys, 8);
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i]; // HID-сканкод (0x04='a', 0x28=Enter, и т.д.)
        int fmsx = hid_to_fmsx(sc);
        if (fmsx >= 0) {
            KeyState[Keys[fmsx][0]] &= ~Keys[fmsx][1];
        }
    }

    // 4) Запускаем Z80 до конца кадра. RunZ80 сам переустанавливает
//    CPU.ICount через LoopZ80 (IPeriod) и выходит по INT_QUIT,
//    когда LoopZ80 доходит до строки 192 (ExitNow=1).
    RunZ80(&CPU);

    // PutImage вызывается внутри LoopZ80 на VBlank (когда UCount>=100).
    // После возврата кадр готов к копированию в EMU_FB.
    (void)0;
}

void msx_stop(void) {
    if (g_loaded) {
        TrashMSX();
        g_loaded = 0;
        printf("MSX: stopped\n");
    }
}

// ---- точка входа из emu.c ----
void emu_run_msx(const uint8_t* rom, uint32_t size, const char* rom_name) {
    (void)rom_name;
    fb_clear(); fb_flush();
    if (!msx_init_game(rom, size)) {
        printf("MSX: init failed\n");
        return;
    }
    emu_set_border_color(0x00000000);
    uint8_t raw_keys[6];
    emu_throttle_reset();
    for (;;) {
        msx_run_frame();
        emu_throttle();
        // MSX выводит 256x212 (MSX2 NTSC) — скейлим по ширине экрана
        // image_buffer_width = 272 (с бордюром), image_buffer_height = 228
        int vw = (int)image_buffer_width;
        int vh = (int)image_buffer_height;
        emu_scale(vw > 0 ? vw : 256, vh > 0 ? vh : 212);
        fb_flush();
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++)
            if (raw_keys[i] == 41) goto exit;  // ESC — выход
    }
exit:
    msx_stop();
    fb_clear(); fb_flush();
}