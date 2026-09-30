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

/* fMSX: ROMType[] определён в MSX.c, в заголовках extern НЕ объявлен */
extern uint8_t ROMType[];

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
static char g_cart_name[48];   // r0.392: имя ROM для сообщений (экрана/UART)

#define XBuf image_buffer
#define WBuf image_buffer

#include "../fMSX/Common.h"
#include "../fMSX/Wide.h"

#include "msx_compat.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "usb_kbd.h"
#include "fb_text.h"
#include "emu.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);
extern uint16_t emu_period_us;   // r0.210: для MSX ставим 50 Гц (PAL)

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// Состояние
static int g_loaded = 0;

// r0.208: выход по удержанию ESC. Детектим из ТОГО ЖЕ boot-отчёта, что читает
// и ядро (emu_run_msx больше не полагается на повторный usb_kbd_get_raw внутри
// emu_esc_hold — у Low-Speed донгла второй опрос за кадр мог отдать пусто и
// «терял» ESC, из-за чего из MSX нельзя было выйти).
int  msx_exit_req = 0;
static uint32_t msx_esc_t0 = 0;

// ---- SetColor — вызывается ядром для установки цвета палитры ----
// fMSX рисует текстовые/graphics-режимы (SCREEN 0-4) через XPal[], а
// BITMAP-режимы (SCREEN 5-8 — почти все MSX2-игры) через BPal[]. Раньше
// BPal нигде не заполнялся → MSX2-картриджи давали ЧЁРНЫЙ экран (r0.206).
void SetColor(uint8_t N, uint8_t R, uint8_t G, uint8_t B) {
    uint16_t c = PIXEL(R, G, B);
    if (N < 80) XPal[N] = c;
    if (N == 0) XPal0 = c;  // цвет фона
    BPal[N] = c;            // r0.206: bitmap-палитра (SCREEN 5-8)
}

// ---- Joystick — вызывается ядром на строке 192 каждого кадра ----
unsigned int Joystick(void) {
    uint16_t pad = pad_scan_combined();
    unsigned int js = 0;
    if (pad & 0x0001) js |= JST_UP;
    if (pad & 0x0002) js |= JST_DOWN;
    if (pad & 0x0004) js |= JST_LEFT;
    if (pad & 0x0008) js |= JST_RIGHT;
    if (pad & 0x0010) js |= JST_FIREA;   // A
    if (pad & 0x0020) js |= JST_FIREB;   // B
    if (pad & 0x0040) js |= JST_FIREA;   // C
    if (pad & 0x0100) js |= JST_FIREB;   // X
    if (pad & 0x0200) js |= JST_FIREA;   // Y
    if (pad & 0x0080) js |= JST_FIREA;   // Start
    if (pad & 0x0800) js |= JST_FIREB;   // Mode
    // r0.209: дублируем в ОБЕ половины (младший байт = джойстик 1, старший = 2) —
    // игры читают то порт 1, то порт 2; без дублирования пад «не работает».
    return js | (js << 8);
}

// ---- Mouse — заглушка ----
unsigned int Mouse(uint8_t N) { (void)N; return 0; }

// ---- PlayAllSound — реализация через fMSX RenderAndPlayAudio ----
// Вызывается ядром на каждом VBlank. Рендерит звук и зовёт WriteAudio().
void PlayAllSound(int uSec) {
    (void)uSec;
    RenderAndPlayAudio((unsigned int)-1);   // все доступные сэмплы
}

// ---- WriteAudio — заглушка: звук отключён ----
// fMSX рендерит стерео int16_t, Length — число сэмплов (не байт!).
unsigned int WriteAudio(int16_t *Data, unsigned int Length) {
    (void)Data;
    return Length;   // «всё записано»
}

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

// ---- DiskPresent/DiskRead/DiskWrite — заглушки (диски не поддерживаются) ----
uint8_t DiskPresent(uint8_t ID) { (void)ID; return 0; }
uint8_t DiskRead(uint8_t ID, uint8_t *Buf, int N) { (void)ID; (void)Buf; (void)N; return 0; }
uint8_t DiskWrite(uint8_t ID, const uint8_t *Buf, int N) { (void)ID; (void)Buf; (void)N; return 0; }

// ---- public API ----

int msx_init_game(const uint8_t* rom, uint32_t size) {
    printf("MSX: init size=%u\n", (unsigned)size);
    g_loaded = 0;
    msx_exit_req = 0;   // r0.208
    msx_esc_t0 = 0;

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

    // r0.211: включить джойстики в сокетах 1/2. Без этого JOYTYPE(N)==JOY_NONE
    // (Mode=…0005), и порт PSG 0xA2 отдаёт 0x7F — т.е. пад «не активен»
    // (клавиатура при этом работает, т.к. идёт по матрице). Как в эталоне
    // fMSX libretro.c:1277-1278.
    SETJOYTYPE(0, JOY_STICK);
    SETJOYTYPE(1, JOY_STICK);

    // r0.392: YIS-503III — встроенные картриджи «СЕТЬ» (NET.ROM) и «СПМ»
    // = CP/M (CPM.ROM) лежат во внутренних слотах настоящей Ямахи.
    // Грузим их как картриджи до пользовательского слота.
    // r0.400: ПОСЛЕ загрузки ResetMSX() БОЛЬШЕ НЕ делаем — повторный reset
    // после StartMSX вывешивал машину (BIOS RESET сканирует слоты, init-хок
    // NET-картриджа «СЕТЬ» уводил в чёрный экран/цикл). В BASIC-режиме
    // картриджи лежат в слотах «молча»; их инициализация понадобится только
    // когда сделаем меню «СПМ» по запросу.
    //
    // r0.415 (Б1): НЕ грузим CPM/NET — они НЕ нужны для загрузки BIOS/BASIC
    // (нужны только MSX2.ROM+MSX2EXT.ROM). Проверяем гипотезу «внутренние
    // картриджи мешают тесту видеопамяти»: если MSX дойдёт до BASIC с
    // выключенными CPM/NET — виноваты они, если нет — дефект V9938 (Б1).
    // Вернуть одним переключателем после диагностики.
#if 1
    int net_lr = 0, cpm_lr = 0;
    (void)net_lr; (void)cpm_lr;
    printf("MSX: builtin carts — CPM/NET отключены (r0.415, диагноз Б1)\n");
#else
    int net_lr = LoadCart("NET.ROM", 3, 0);
    int cpm_lr = LoadCart("CPM.ROM", 2, 0);
    printf("MSX: builtin carts -> CPM.ROM (СПМ) slot2 lr=%d, NET.ROM (СЕТЬ) slot3 lr=%d\n",
           cpm_lr, net_lr);
#endif

    // Если был передан образ картриджа — загружаем в слот A.
    // LoadCart() в ядре делает rfopen("CARTA.ROM"), поэтому даём стабу
    // валидный источник через msx_compat_set_cart() (буфер в памяти).
    int cart_lr = -1;
    if (rom && size > 0) {
        msx_compat_set_cart(rom, size);
        // r0.392: диагностика до LoadCart — типичные причины «игра не
        // запустилась» с большими/чужими ROM:
        //   * файл на самом деле ZIP (начинается с 'PK') — fMSX грузит
        //     ТОЛЬКО сырой .rom (zip он не распаковывает);
        //   * нет «AB»-заголовка — fMSX принципиально откажет в LoadCart.
        if (size >= 2 && rom[0] == 'P' && rom[1] == 'K') {
            printf("MSX: WARN картридж похож на ZIP ('PK', %u Б) — распакуйте в .rom\n",
                   (unsigned)size);
        } else {
            int ab = (size >= 2 && rom[0] == 'A' && rom[1] == 'B') ||
                     (size >= 0x4002 && rom[0x4000] == 'A' && rom[0x4001] == 'B') ||
                     (size >= 0x2002 && rom[size - 0x2000] == 'A' && rom[size - 0x2000 + 1] == 'B');
            if (!ab)
                printf("MSX: WARN нет 'AB'-заголовка (0/0x4000/посл.стр.) — мэппер может не распознаться\n");
        }
        // StartMSX уже загрузил системные картриджи (MSXDOS2 и т.д.),
        // но пользовательский ROMName[0]="CARTA.ROM" грузится ТОЛЬКО
        // в StartMSX (строка 592). Он уже выполнен. Поэтому повторно:
        cart_lr = LoadCart("CARTA.ROM", 0, ROMGUESS(0) | ROMTYPE(0));
        printf("MSX: LoadCart -> %d (%u КБ, mapper=%d)%s\n", cart_lr,
               (unsigned)(size >> 10), (int)ROMType[0], cart_lr ? "" : " — НЕ ЗАГРУЖЕН");
        if (cart_lr <= 0) {
            fb_clear();
            fb_text_center("MSX: cart not loaded", 200, 2, 0x00FF4444);
            fb_text_center(g_cart_name, 240, 2, 0x00FFFFFF);
            fb_text_center("no AB header / ZIP (extract)", 258, 1, 0x00AAAAAA);
            fb_flush();
            udelay(2000000);
            fb_clear(); fb_flush();
        }
    }

    // r0.392: читаемая строка модели вместо голого Mode-битмаски.
    // Версия VDP по флагам Mode:
    //   MSX_MSX2P — V9958 (MSX2+), MSX_MSX2 — V9938, иначе V9918 (MSX1).
    const char* mach = (Mode & MSX_MSX2P) ? "MSX2+ (V9958)"
                     : (Mode & MSX_MSX2)  ? "MSX2 (V9938)"
                                          : "MSX1 (TMS9918)";
    const char* tv   = (Mode & MSX_PAL) ? "PAL 50Hz" : "NTSC 60Hz";
    printf("MSX: machine=%s %s RAM=%uKB VRAM=%uKB\n",
           mach, tv, (unsigned)RAMPages * 16, (unsigned)VRAMPages * 16);
    printf("MSX: Mode=%08X — Yamaha YIS-503III (MSX2, Европа, PAL)\n",
           (unsigned)Mode);

    // r0.392: «захват сессии» по UART при входе в эмулятор.
    extern unsigned char fMSX_ROMs_MSX2_ROM[];
    extern unsigned char fMSX_ROMs_MSX2EXT_ROM[];
    extern unsigned int  fMSX_ROMs_MSX2_ROM_len;
    extern unsigned int  fMSX_ROMs_MSX2EXT_ROM_len;
    printf("MSX: ================= session =================\n");
    printf("MSX: machine   YIS-503III (%s) %s RAM=%uKB VRAM=%uKB\n",
           mach, tv, (unsigned)RAMPages * 16, (unsigned)VRAMPages * 16);
    printf("MSX: bios      basic-bios2 %u B, sub %u B\n",
           fMSX_ROMs_MSX2_ROM_len, fMSX_ROMs_MSX2EXT_ROM_len);
    printf("MSX:   head(BIOS) %02X %02X %02X %02X | sub %02X %02X %02X %02X\n",
           fMSX_ROMs_MSX2_ROM[0], fMSX_ROMs_MSX2_ROM[1],
           fMSX_ROMs_MSX2_ROM[2], fMSX_ROMs_MSX2_ROM[3],
           fMSX_ROMs_MSX2EXT_ROM[0], fMSX_ROMs_MSX2EXT_ROM[1],
           fMSX_ROMs_MSX2EXT_ROM[2], fMSX_ROMs_MSX2EXT_ROM[3]);
    printf("MSX: carts     slot2=CPM.ROM (СПМ, lr=%d)  slot3=NET.ROM (СЕТЬ, lr=%d)\n",
           cpm_lr, net_lr);
    printf("MSX: user cart %s (%u Б) mapper=%d lr=%d\n",
           g_cart_name, (unsigned)size, (int)ROMType[0], cart_lr);
    printf("MSX: ==========================================\n");

    // Звук: fMSX PSG/AY-3-8910 + YM2413 (NukeYKT) → RenderAndPlayAudio →
    // WriteAudio → I2S. InitSound обязателен (иначе SndRate=0, тишина).
    InitSound(SND_RATE);

    g_loaded = 1;
    return 1;
}

// ---- маппинг HID-сканкода USB-клавиатуры в fMSX-код (KBD_*) ----
// USB клавиатура отдаёт сырые HID usage (0x04=a ... 0x1D=z, 0x28=Enter...).
// fMSX-код = индекс в Keys[][]; для ASCII это сам символ (0x21-0x7F),
// спецклавиши — KBD_* (MSX.h).
//
// r0.393: полная USB HID-таблица (Keyboard/Keypad Usage, 0x04..0x65+0xE0..).
// Было замаплено частично, а цифирный блок сидел СО СДВИГОМ (0x53 — это
// NumLock, а не Keypad 1; 0x59 — Keypad 1, а не 7): на обычной клавиатуре
// цифры с Numpad попадали не туда. Ниже — стандарт USB Foundation Spec.
// Клавиш, которых НЕТ на MSX (F6-F12, PageUp/Down, End, PrintScreen, Win),
// даём разумные MSX-эквиваленты; F9 занят переключателем RU/LAT.
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
    case 0x32: return KBD_DEAD;   /* ISO Non-US #~ — как клавиша акцентов */
    case 0x33: return ';';   case 0x34: return '\'';  case 0x35: return '`';
    case 0x36: return ',';   case 0x37: return '.';   case 0x38: return '/';
    case 0x39: return KBD_CAPSLOCK;
    case 0x3A: return KBD_F1;  case 0x3B: return KBD_F2;
    case 0x3C: return KBD_F3;  case 0x3D: return KBD_F4;  case 0x3E: return KBD_F5;
    /* F6-F12 — нет на MSX: даём ближайшие MSX-клавиши (F9 — RU/LAT см. ниже);
       F8=INSERT, F11=CapsLock (как на ПК-клавиатурах), F10/F12=STOP(COPY). */
    case 0x3F: return KBD_COUNTRY;   /* F6 → COUNTRY */
    case 0x40: return KBD_SELECT;    /* F7 → SELECT  */
    case 0x41: return KBD_INSERT;    /* F8 → INSERT  */
    /* 0x42 = F9 — переключатель RU/LAT (обрабатывается в msx_run_frame) */
    case 0x43: return KBD_STOP;      /* F10 → STOP  */
    case 0x44: return KBD_CAPSLOCK;  /* F11 → CapsLock */
    case 0x45: return KBD_STOP;      /* F12 → STOP  */
    case 0x46: return KBD_STOP;      /* PrintScreen → COPY(STOP) — печать экрана */
    case 0x47: return KBD_DEAD;      /* ScrollLock → DEAD (не используется MSX) */
    case 0x48: return KBD_STOP;      /* Pause/Break → STOP */
    case 0x49: return KBD_INSERT;
    case 0x4A: return KBD_HOME;
    case 0x4B: return KBD_SELECT;    /* PageUp → SELECT */
    case 0x4C: return KBD_DELETE;
    case 0x4D: return KBD_STOP;      /* End → STOP */
    case 0x4E: return KBD_INSERT;    /* PageDown → INSERT */
    case 0x4F: return KBD_RIGHT; case 0x50: return KBD_LEFT;
    case 0x51: return KBD_DOWN;  case 0x52: return KBD_UP;
    /* ---- Keypad (стандарт USB: 0x53 NumLock, 0x54 '/', ... 0x59 '1'...) ---- */
    case 0x53: return KBD_DEAD;      /* NumLock — на MSX нет */
    case 0x54: return KBD_NUMDIV;
    case 0x55: return KBD_NUMMUL;
    case 0x56: return KBD_NUMMINUS;
    case 0x57: return KBD_NUMPLUS;
    case 0x58: return KBD_ENTER;     /* Keypad Enter = Enter */
    case 0x59: return KBD_NUMPAD1;
    case 0x5A: return KBD_NUMPAD2;
    case 0x5B: return KBD_NUMPAD3;
    case 0x5C: return KBD_NUMPAD4;
    case 0x5D: return KBD_NUMPAD5;
    case 0x5E: return KBD_NUMPAD6;
    case 0x5F: return KBD_NUMPAD7;
    case 0x60: return KBD_NUMPAD8;
    case 0x61: return KBD_NUMPAD9;
    case 0x62: return KBD_NUMPAD0;
    case 0x63: return KBD_NUMDOT;    /* Keypad . */
    case 0x64: return '\\';          /* ISO Non-US \| (слева от Shift) */
    case 0x65: return KBD_INSERT;    /* App/Context menu */
    case 0x67: return '=';           /* Keypad = */
    case 0x85: return KBD_NUMCOMMA;  /* Keypad , (бразильские) */
    case 0xE0: return KBD_CONTROL; case 0xE4: return KBD_CONTROL;   /* L/R Ctrl  */
    case 0xE1: return KBD_SHIFT;   case 0xE5: return KBD_SHIFT;     /* L/R Shift */
    case 0xE2: return KBD_GRAPH;   case 0xE6: return KBD_GRAPH;     /* L/R Alt → GRAPH */
    case 0xE3: return KBD_SELECT;  case 0xE7: return KBD_SELECT;    /* Win → SELECT */
    default: return -1;
    }
}

// r0.392: RU-индикация для русских MSX-программ.
// На физической «Ямахе» YIS-503II русские буквы напечатаны прямо на
// латинских клавишах (фонетическая раскладка), а русские игры читают
// МАТРИЦУ клавиатуры — отдельного «русского» слоя в эмуляции не требуется:
// сканкоды и так совпадают с железом. Проблема пользователя обычно в том,
// что он не знает раскладку, либо программа ждёт кириллицу через CHGET
// (а западный MSX2.ROM отдаёт латиницу — для полноценного ввода нужна
// советская BIOS-прошивка, это отдельная задача).
// F9 переключает индикатор RU/LAT и показывает таблицу раскладки 2.5 с.
static int g_msx_ru = 0;
static int g_ru_prev = 0;          // фронт F9
static uint32_t g_ru_hint_until = 0;

static void msx_ru_hint_print(void)
{
    printf("MSX: RU=%s — фонетическая раскладка YIS-503II:\n", g_msx_ru ? "ON" : "OFF");
    if (!g_msx_ru) return;
    printf("  Q=`Я' W=`Ш' E=`Е' R=`Р' T=`Т' Y=`Ы' U=`У' I=`И' O=`О' P=`П'\n");
    printf("  A=`А' S=`С' D=`Д' F=`Ф' G=`Г' H=`Х' J=`Й' K=`К' L=`Л'\n");
    printf("  Z=`З' X=`Ь' C=`Ц' V=`В' B=`Б' N=`Н' M=`М'\n");
    printf("  (CHGET-программы вернут ЛАТИНИЦУ: нужна русская BIOS)\n");
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

    // 2) Сырые сканкоды USB (обновляет boot-отчёт из USB)
    uint8_t keys[8];
    int n = usb_kbd_get_raw(keys, 8);

    // 3) Модификаторы — из первого байта ТОГО ЖЕ свежего отчёта (r0.207:
    //    раньше mods читались ДО get_raw и были на кадр позади)
    uint8_t mods = usb_kbd_get_mods();
    if (mods & 0x02) { KeyState[6] &= ~0x01; } // LShift
    if (mods & 0x20) { KeyState[6] &= ~0x01; } // RShift
    if (mods & 0x01) { KeyState[6] &= ~0x02; } // LCtrl
    if (mods & 0x10) { KeyState[6] &= ~0x02; } // RCtrl
    if (mods & 0x04) { KeyState[6] &= ~0x04; } // LAlt → GRAPH
    if (mods & 0x40) { KeyState[6] &= ~0x04; } // RAlt → GRAPH

    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i]; // HID-сканкод (0x04='a', 0x28=Enter, и т.д.)
        int fmsx = hid_to_fmsx(sc);
        if (fmsx >= 0) {
            KeyState[Keys[fmsx][0]] &= ~Keys[fmsx][1];
        }
    }

    /* r0.208: выход по удержанию ESC (~0.9с) — из этого же отчёта */
    {
        int esc = 0;
        for (int i = 0; i < n; i++) if (keys[i] == 0x29) esc = 1;
        uint32_t now = h3_hs_timer_lo_us();
        if (esc) {
            if (!msx_esc_t0) msx_esc_t0 = now;
            else if (now - msx_esc_t0 > 900000u) msx_exit_req = 1;
        } else {
            msx_esc_t0 = 0;
        }
    }

    /* r0.393: F9 (сканкод 0x42) — RU/LAT индикатор (матрица не меняется,
       смена только подсказки: физически русские буквы уже лежат на тех же
       клавишах, как на YIS-503II/III). Старый код ловил 0x44 = F11. */
    {
        int f9 = 0;
        for (int i = 0; i < n; i++) if (keys[i] == 0x42) f9 = 1;
        if (f9 && !g_ru_prev) {
            g_msx_ru = !g_msx_ru;
            msx_ru_hint_print();
            g_ru_hint_until = h3_hs_timer_lo_us() + 2500000u;
        }
        g_ru_prev = f9;
    }

    // 4) Запускаем Z80 до конца кадра. RunZ80 сам переустанавливает
//    CPU.ICount через LoopZ80 (IPeriod) и выходит по INT_QUIT,
//    когда LoopZ80 доходит до строки 192 (ExitNow=1).
    // r0.402: сторож зависания BIOS — если кадр крутится >1 с, печатаем
    // точку остановки Z80 (PC) и состояние видео, чтобы по UART было видно,
    // на чём виснет (напр. этап записи видеопамяти у русифицированного BIOS).
    {
        uint32_t t0 = h3_hs_timer_lo_us();
        uint16_t pc_end = RunZ80(&CPU);
        uint32_t el = h3_hs_timer_lo_us() - t0;
        static uint8_t s_last_mode = 0xFF;
        if (el > 990000u) {
            printf("MSX: HANG pc=%04X sp=%04X ScrMode=%d W=%u H=%u el=%ums\n",
                   (unsigned)pc_end, (unsigned)CPU.SP.W, (unsigned)ScrMode,
                   (unsigned)image_buffer_width, (unsigned)image_buffer_height,
                   (unsigned)(el / 1000));
            g_ru_hint_until = 0;   // не рисовать OSD поверх диагностики
        }
        if (ScrMode != s_last_mode) {
            s_last_mode = ScrMode;
            printf("MSX: ScrMode=%d W=%u H=%u\n", (unsigned)ScrMode,
                   (unsigned)image_buffer_width, (unsigned)image_buffer_height);
        }
    }

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
    if (rom_name && rom_name[0]) {
        snprintf(g_cart_name, sizeof(g_cart_name), "%s", rom_name);
    } else {
        snprintf(g_cart_name, sizeof(g_cart_name), "MSX cart");
    }
    emu_prepare();
    snd_manifest("msx", "psg ay8910 scc");
    memset(image_buffer, 0, sizeof(image_buffer));   // свой кадр-буфер не чистится emu_prepare
    if (!msx_init_game(rom, size)) {
        printf("MSX: init failed\n");
        return;
    }
    emu_set_border_color(0x00000000);
    /* r0.210: MSX у нас PAL (YIS-503II, 50 Гц). emu_throttle гнал 60 Гц →
     * гость шёл 1.2× и «рывками» (смена кадров 50↔60). Держим 50 Гц. */
    uint16_t saved_period = emu_period_us;
    emu_period_us = 20000;
    emu_throttle_reset();
    emu_esc_hold_reset();
    /* r0.407: «прогрев» — первые ~3 с после входа гоняем НА ПОЛНОЙ скорости
     * без throttle: BIOS YIS-503III в SCREEN 6 гоняет тест видеопамяти
     * (в fMSX VRAM-доступ идёт по сканлайнам — на 50 Гц это растягивалось
     * на десятки секунд с «рваным» экраном; на железе всё проскакивает
     * за секунды). После прогрева — обычные 50 Гц. */
    uint32_t msx_start = h3_hs_timer_lo_us();
    for (;;) {
        msx_run_frame();
        if ((int32_t)(h3_hs_timer_lo_us() - msx_start) >= 3000000)
            emu_throttle();
        int vw = (int)image_buffer_width;
        if (vw > EMU_W) vw = EMU_W;
        int vh = (int)image_buffer_height;
        emu_scale(vw > 0 ? vw : 256, vh > 0 ? vh : 212);
        fb_flush();
        // r0.392: OSD-подсказка раскладки после F9 (2.5 с) поверх кадра.
        if ((int32_t)(h3_hs_timer_lo_us() - g_ru_hint_until) < 0 && g_msx_ru) {
            fb_puts_s(30, 30, "RU: Q=YA W=SH E=E R=R T=T Y=YI U=U I=I O=O P=P", 1, 0x00FFAA00);
            fb_puts_s(30, 50, "   A=A S=S D=D F=F G=G H=H J=Y K=K L=L :=ZH '=E /=B", 1, 0x00FFAA00);
            fb_puts_s(30, 70, "   Z=Z X=Y Y=Y C=C V=V B=B N=N M=M ?=YU .=YO", 1, 0x00FFAA00);
            fb_flush();
        }
        if (emu_esc_hold() || msx_exit_req) goto exit;  // r0.208: ESC-hold из отчёта ядра
    }
exit:
    emu_period_us = saved_period;   // r0.210: вернуть общий период
    msx_stop();
    fb_clear(); fb_flush();
}