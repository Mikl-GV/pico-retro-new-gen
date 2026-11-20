// a2600_host.cpp — host-слой Stella 2014 (libretro-порт) для bare-metal H3.
// Замена MCUME (x2600/PocketVCS): точное ядро Stella (TIA/M6502/M6532, все
// банки Cart*), кадр 160×N (NTSC ~210, PAL до 256 — setTIAProperties)
// индексов палитры, звук 31400 Гц стерео через Sound::processFragment.
//
// Жизненный цикл:
//   emu_run_a2600(rom, size, name)
//     -> a2600_init: osystem->attach(settings) + стеты (палитра/звук/регион)
//     -> Cartridge::create + new Console + initializeVideo/Audio
//     -> цикл: build_input() -> riot().update() -> tia.update() -> рендер
//        в EMU_FB (RGB565 320×240, кадр растянут до 240 строк) ->
//        a2600_render_frame (GSU)
//     -> a2600_stop: delete console (картридж освобождает ~System) + UI restore
//
// Ввод: только левый порт (JoystickZero*/Paddle порта 0). Правый геймпад/пэдёл
// (порт 1, JoystickOne*/PaddleTwo/Three) НЕ эмулируется — ядру для спецнужд не
// нужен, 2-игроковые/правые paddle-тайтлы управляются без него (r1542).

#include <stdint.h>
#include <string.h>
#include <new>

#include "a2600/emucore/OSystem.hxx"
#include "a2600/emucore/Settings.hxx"
#include "a2600/emucore/Props.hxx"
#include "a2600/emucore/PropsSet.hxx"
#include "a2600/emucore/Cart.hxx"
#include "a2600/emucore/Console.hxx"
#include "a2600/emucore/Event.hxx"
#include "a2600/emucore/EventHandler.hxx"
#include "a2600/emucore/Sound.hxx"
#include "a2600/emucore/TIA.hxx"
#include "a2600/emucore/M6532.hxx"   // r1531: riot().update() — Event -> пины
#include "a2600/emucore/MD5.hxx"
#include "a2600/stubs/Stubs.hxx"

extern "C" {
#include "usb_kbd.h"
#include "a2600_ctrl.h"   // r1534: тип контроллеров по MD5/имени
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "i2s.h"
#include "fb_text.h"
#include "emulog.h"
#include "emu.h"
#include "uart.h"
#include "settings.h"
}

extern "C" int printf(const char* fmt, ...);

// r1527: OSystem НЕ статический объект. Статический конструктор выполняется
// в .init_array ДО main (startup.S) и выделяет Sound/EventHandler/PropertiesSet
// из newlib-кучи; emu_prepare() затем зовёт newlib_heap_reset() (emu.c:471) —
// объекты становятся висячими, и первая же getMD5() падала на мусорном
// указателе (стенд: F1/DFAR=0x0F, LR в PropsSet::getMD5). Поэтому OSystem
// конструируется заново на каждый вход — уже после сброса кучи — в статическом
// буфере (в .bss он места не занимает).
static unsigned char s_osys_mem[sizeof(OSystem)] __attribute__((aligned(16)));
static OSystem* osystem = 0;
static Settings* settings = 0;
static Cartridge* cartridge = 0;
static Console* console = 0;
static int g_loaded = 0;
// r1531: тумблер Color/B&W переживает сессию, поэтому он файловый: сброс в
// a2600_init_game держит палитру новой игры в согласии с битом коммутаторов.
static int a26_color_bw = 0;
// r1533: правая сложность — тумблер только в сессии (0 = B/Novice, как в
// Properties по умолчанию); сбрасывается на входе в игру.
static int a26_rdiff_expert = 0;
// r1534: типы контроллеров, выбранные по MD5 рома (см. a2600_ctrl.c).
// Нужны в build_input: KEYBOARD требует цифровых событий клавиатуры, а
// PADDLES читает движение из тех же JoystickZero* (Paddles.cxx).
static int a26_ctrl_left  = A26C_JOY;
static int a26_ctrl_right = A26C_JOY;

static const char* a26_ctrl_name(int c) {
    switch (c) {
        case A26C_PADDLES:   return "PADDLES";
        case A26C_PAD_IAXIS: return "PADDLES_IAXIS";
        case A26C_PAD_IDIR:  return "PADDLES_IDIR";
        case A26C_PAD_IAXDR: return "PADDLES_IAXDR";
        case A26C_KEYBOARD:  return "KEYBOARD";
        default:             return "JOYSTICK";
    }
}

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// ---- r1525: аппаратный DE2 GSU (вместо программного emu_scale) ----
// Кадр A2600 — RGB565 320×240 в EMU_FB (NTSC: верх 192 строки, PAL: верх 228;
// низ до 240 — чёрный от предыдущего кадра, закрывается stretch-геометрией).
// В a2600_render_frame кадр конвертируется в XRGB8888 (a26_rgb32, упакован
// 320×240 — строки по 320 слов, DE2 PITCH = INSIZE, урок r1495), GSU
// растягивает на всю высоту FB_H=600 с сохранением пропорций
// (dst_w = 320×600/240 = 800, dx = 112 — как a7_rgb32 r1523). Высота активных
// строк ≤228 (PAL) — GSU растягивает 240 строк EMU_FB; строки, что ниже
// реального кадра (NTSC 192..240), в настройке TV-режима ядро не рисует и
// они остаются чёрными предыдущего кадра — коррекции не требуется.
// Размер фиксированный — перекалибровка при смене w/h не нужна (урок r1506
// не применим). Сброс флага при входе (урок r1496). Fallback — программный
// emu_scale (g_a26_hw_scaler_active=0). r1519-эталон: вызывается БЕЗУСЛОВНО.
static uint32_t a26_rgb32[EMU_W * EMU_H] __attribute__((aligned(4), section(".noinit_fbn")));
static int g_a26_hw_scaler_active = 0;

// Палитра TIA (XRGB8888, по одному uint32 на байт индекса кадра).
static uint32_t a26_pal32[256];

extern "C" int a2600_hw_scaler_active(void) { return g_a26_hw_scaler_active; }

extern "C" void a2600_render_frame(void) {
    extern int h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h,
                                     uint32_t bpp, uint32_t dx, uint32_t dy,
                                     uint32_t dw, uint32_t dh);
    extern void usb_cache_clean(uint32_t addr, uint32_t size);
    if (!g_a26_hw_scaler_active) {
        // Растяжение на всю высоту FB_H=600 с сохранением пропорций (как emu_scale).
        int dh = 600;   // FB_H
        int dw = (EMU_W * 600) / EMU_H;   // 320×600/240 = 800
        if (dw > 1024) dw = 1024;
        if (dw <= 0) return;
        int dx = (1024 - dw) / 2;   // 112 по центру
        if (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)a26_rgb32,
                                  (uint32_t)EMU_W, (uint32_t)EMU_H, 32,
                                  (uint32_t)dx, 0, (uint32_t)dw, (uint32_t)dh) == 0) {
            g_a26_hw_scaler_active = 1;
            printf("A2600: DE2 GSU scaler %dx%d->%dx%d\n", EMU_W, EMU_H, dw, dh);
        } else {
            printf("A2600: GSU scaler failed, fallback CPU emu_scale\n");
        }
    }
    if (g_a26_hw_scaler_active) {
        // RGB565 -> XRGB8888, упакованно 320×240 (строки по 320 слов):
        // DE2 PITCH = INSIZE (1280 Б), читает ровно 320 слов на строку.
        const uint16_t* s = EMU_FB;
        uint32_t* d = a26_rgb32;
        for (int y = 0; y < EMU_H; y++) {
            const uint16_t* srow = s + y * EMU_W;
            uint32_t* drow = d + (size_t)y * EMU_W;
            for (int x = 0; x < EMU_W; x++) {
                uint16_t p = srow[x];
                drow[x] = (uint32_t)(((p >> 11) & 0x1Fu) << 19) |
                          (uint32_t)(((p >> 5)  & 0x3Fu) << 10) |
                          (uint32_t)((p & 0x1Fu) << 3);
            }
        }
        usb_cache_clean((uint32_t)(uintptr_t)a26_rgb32,
                        (uint32_t)((size_t)EMU_W * EMU_H * 4u));
    }
}

// Есть ли HID-код среди удерживаемых клавиш (0-6 rollover).
static int key_held(const uint8_t* keys, int n, uint8_t code) {
    for (int i = 0; i < n; i++)
        if (keys[i] == code) return 1;
    return 0;
}

// Ввод: Sega-геймпад + клавиатура (ремап REMAP_PLAT_A2600) -> Event Stella.
static void build_input(void) {
    Event& ev = osystem->eventHandler().event();

    uint16_t sp = pad_scan_combined();
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);

    uint32_t j = 0;   // JoystickZero-биты (Up/Down/Left/Right/Fire)
    if (sp & 0x0001) j |= 1 << Event::JoystickZeroUp;
    if (sp & 0x0002) j |= 1 << Event::JoystickZeroDown;
    if (sp & 0x0004) j |= 1 << Event::JoystickZeroLeft;
    if (sp & 0x0008) j |= 1 << Event::JoystickZeroRight;
    if (sp & 0x0010) j |= 1 << Event::JoystickZeroFire;   // Sega A -> Fire
    if (sp & 0x0020) j |= 1 << Event::JoystickZeroFire;   // Sega B -> Fire (дубль)
    // r1530: коммутаторы корпуса — УРОВНИ, а не «нажал и забыл».
    // Switches::update() (M6532.cxx:93) читает Event::ConsoleSelect/Reset как
    // уровень: 1 = нажато, 0 = отпущено. Раньше события ставились только в 1,
    // поэтому после первого нажатия Select/Reset залипали навсегда.
    int sel_pressed = ((sp & 0x0800) != 0) ||   // Mode -> Select
                      remap_kbd_pressed(REMAP_PLAT_A2600, BTN_SELECT, keys, n);
    int rst_pressed = ((sp & 0x0080) != 0) ||   // Start -> Reset
                      remap_kbd_pressed(REMAP_PLAT_A2600, BTN_RESET, keys, n);
    ev.set(Event::ConsoleSelect, sel_pressed ? 1 : 0);
    ev.set(Event::ConsoleReset,  rst_pressed ? 1 : 0);

    // r1535: Game Reset — ТОЛЬКО уровень бита коммутатора, как в старом ядре
    // x2600 (keycons): игра сама опрашивает SWCHB bit0 и стартует по нажатию.
    // System::reset() здесь был ошибкой — он перезапускал CPU в момент нажатия,
    // игра не успевала увидеть отпускание и возвращалась на титул («игра не
    // начинается»). В Stella EventHandler тоже НЕ сбрасывает систему по
    // ConsoleReset — это просто коммутатор.

    // Клавиатура -> A2600 (ремап через Settings → Keyboard remap)
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_UP, keys, n))    j |= 1 << Event::JoystickZeroUp;
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_DOWN, keys, n))  j |= 1 << Event::JoystickZeroDown;
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_LEFT, keys, n))  j |= 1 << Event::JoystickZeroLeft;
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_RIGHT, keys, n)) j |= 1 << Event::JoystickZeroRight;
    if (remap_kbd_pressed(REMAP_PLAT_A2600, BTN_FIRE, keys, n))  j |= 1 << Event::JoystickZeroFire;

    // Сложность левого джойстика (A/B) — тумблер по ФРОНТУ нажатия:
    // remap_kbd_pressed возвращает уровень, без защёлки значение дрожало бы
    // каждый кадр. Состояние общее с меню (a2600_diff_expert + SRAM 0x70).
    // В Switches::update() позиция A проверяется первой, поэтому выставляем
    // РОВНО один из двух: до r1530 ставились оба и позиция B была недостижима.
    static int diff_key_prev = 0;
    int diff_key = remap_kbd_pressed(REMAP_PLAT_A2600, BTN_DIFF, keys, n);
    if (diff_key && !diff_key_prev) {
        a2600_diff_expert = (uint8_t)!a2600_diff_expert;
        *(volatile uint32_t*)0x70u = a2600_diff_expert;
    }
    diff_key_prev = diff_key;
    ev.set(Event::ConsoleLeftDiffA, a2600_diff_expert ? 1 : 0);
    ev.set(Event::ConsoleLeftDiffB, a2600_diff_expert ? 0 : 1);

    // Color/B&W (r1530): тумблер по ФРОНТУ — палитра TIA + bit3 коммутаторов.
    static int color_prev = 0;
    int color_key = remap_kbd_pressed(REMAP_PLAT_A2600, BTN_COLOR, keys, n);
    if (color_key && !color_prev) {
        a26_color_bw = !a26_color_bw;
        console->toggleColorLoss(a26_color_bw != 0);
    }
    color_prev = color_key;
    ev.set(Event::ConsoleColor,      a26_color_bw ? 0 : 1);
    ev.set(Event::ConsoleBlackWhite, a26_color_bw ? 1 : 0);

    // Правая сложность — тумблер по ФРОНТУ. Отдельного пункта меню нет:
    // состояние живёт в сессии и сбрасывается в a2600_init_game.
    static int rdiff_prev = 0;
    int rdiff_key = remap_kbd_pressed(REMAP_PLAT_A2600, BTN_RDIFF, keys, n);
    if (rdiff_key && !rdiff_prev) a26_rdiff_expert = !a26_rdiff_expert;
    rdiff_prev = rdiff_key;
    ev.set(Event::ConsoleRightDiffA, a26_rdiff_expert ? 1 : 0);
    ev.set(Event::ConsoleRightDiffB, a26_rdiff_expert ? 0 : 1);

    // Fry — холодный «прогрев»: Console::fry() мангалит RAM RIOT/TIA. В ядре
    // Event::Fry никто не читает (в Stella его обрабатывает EventHandler, у нас
    // заглушка), поэтому зовём напрямую по ФРОНТУ нажатия.
    static int fry_prev = 0;
    int fry_key = remap_kbd_pressed(REMAP_PLAT_A2600, BTN_FRY, keys, n);
    if (fry_key && !fry_prev) console->fry();
    fry_prev = fry_key;

    // r1534: keypad-игры (Hunt & Score / Concentration). Клавиатурный
    // контроллер Stella читает Event::KeyboardZero*/One* при сканировании
    // RIOT (Keyboard::write -> пины), джойстик ему не нужен. Верхний ряд
    // 1..0 = порт 0, цифровой блок = порт 1; '-'/'=' и numpad '*'/'+' = */#.
    if (a26_ctrl_left == A26C_KEYBOARD || a26_ctrl_right == A26C_KEYBOARD) {
        static const uint8_t kp0[12] = {
            30,31,32,33,34,35,36,37,38,39,45,46 };
        static const uint8_t kp1[12] = {
            0x59,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,0x60,0x61,0x62,0x55,0x57 };
        static const Event::Type ev0[12] = {
            Event::KeyboardZero1, Event::KeyboardZero2, Event::KeyboardZero3,
            Event::KeyboardZero4, Event::KeyboardZero5, Event::KeyboardZero6,
            Event::KeyboardZero7, Event::KeyboardZero8, Event::KeyboardZero9,
            Event::KeyboardZero0, Event::KeyboardZeroStar,
            Event::KeyboardZeroPound };
        static const Event::Type ev1[12] = {
            Event::KeyboardOne1, Event::KeyboardOne2, Event::KeyboardOne3,
            Event::KeyboardOne4, Event::KeyboardOne5, Event::KeyboardOne6,
            Event::KeyboardOne7, Event::KeyboardOne8, Event::KeyboardOne9,
            Event::KeyboardOne0, Event::KeyboardOneStar,
            Event::KeyboardOnePound };
        for (int i = 0; i < 12; i++) {
            if (a26_ctrl_left == A26C_KEYBOARD)
                ev.set(ev0[i], key_held(keys, n, kp0[i]));
            if (a26_ctrl_right == A26C_KEYBOARD)
                ev.set(ev1[i], key_held(keys, n, kp1[i]));
        }
    }

    ev.set(Event::JoystickZeroUp,    (j >> Event::JoystickZeroUp) & 1);
    ev.set(Event::JoystickZeroDown,  (j >> Event::JoystickZeroDown) & 1);
    ev.set(Event::JoystickZeroLeft,  (j >> Event::JoystickZeroLeft) & 1);
    ev.set(Event::JoystickZeroRight, (j >> Event::JoystickZeroRight) & 1);
    ev.set(Event::JoystickZeroFire,  (j >> Event::JoystickZeroFire) & 1);
}

// Рендер кадра TIA в EMU_FB (RGB565 320×240): кадр 160×N индексов палитры,
// растягивается по горизонтали 1:2 (160→320) и по вертикали N→240
// (строки sy = y*h/240). N задаётся setTIAProperties (Console.cxx): NTSC 210,
// PAL — до 256 (height = MAX(…, 250), строка 492). Push в 240 строк EMU_FB:
// PAL-кадр (250–256) слегка сжимается по вертикали; GSU растягивает буфер
// целиком на 600 — изображение всегда на всю высоту.

// Пишем кадр из TIA в EMU_FB — масштаб 1:2 по горизонтали, по вертикали
// N → 240 (строки кадра повторяются с целым шагом).
static void a2600_render_tia_to_fb(void) {
    if (!console) return;
    TIA& tia = console->tia();
    const uint8_t* buf = tia.currentFrameBuffer();
    uint32_t w = tia.width();      // 160
    uint32_t h = tia.height();     // NTSC ~210, PAL до 256 (setTIAProperties)
    if (w > 160) w = 160;
    if (h < 1) h = 1;

    // Палитра: XRGB8888 -> RGB565 (младшие биты отбрасываются).
    const uint32_t* pal = console->getPalette(0);
    for (int i = 0; i < 256; i++) {
        uint32_t c = pal ? pal[i] : 0;
        a26_pal32[i] = c;
    }

    for (uint32_t y = 0; y < EMU_H; y++) {
        uint32_t sy = (y * h) / EMU_H;
        const uint8_t* srow = buf + sy * w;
        uint16_t* drow = EMU_FB + y * EMU_W;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t c = a26_pal32[srow[x] & 0xFF];
            uint16_t p = (uint16_t)(((c >> 19) & 0x1F) << 11) |
                          (uint16_t)(((c >> 10) & 0x3F) << 5) |
                          (uint16_t)((c >> 3) & 0x1F);
            drow[x * 2] = p;
            drow[x * 2 + 1] = p;
        }
    }
}

// ---- ресемплер звука 31400 → 48000 Гц (линейный, стерео-пары) ----
// Sound::processFragment отдаёт 31400 Гц стерео s16 уже полной шкалы:
// TIASound держит AUDV со сдвигом AUDV_SHIFT=10 (TIASnd.hxx:144), то есть
// 0..15360 на канал и до 30720 на сумме. Усиление не нужно — r1539 умножал
// сырьё на 1024 и заворачивал int16; в r1541 множитель снят.
// DC-смещение снимает DC-блокер CPU2 (shift 6) — отдельный фильтр не нужен.
// I2S на 48 кГц; 31400→48000 = ×1.529. Фрагмент кадра считается от частоты
// консоли (r1537): NTSC 1498/25 → 524 сэмпла → 801 пара, PAL 1248/25 → 629
// → 962 пары. Линейная интерполяция достаточна для TIA-шума.
#define A26_RS_INC ((31400u << 16) / 48000u)  // шаг входа на выход = 42871
#define A26_RS_MAX_OUT 1600                   // 1024 входа × 1.529 ≈ 1566
static uint32_t a26_rs_phase = 0;   // непрерывна между кадрами

static uint32_t a26_resample(const int16_t* in, uint32_t nin, int16_t* out) {
    uint32_t ph = a26_rs_phase;   // непрерывная 16.16-фаза входного потока
    uint32_t o = 0;
    while (o < A26_RS_MAX_OUT) {
        uint32_t i = ph >> 16;
        if (i >= nin) break;      // вышли за фрагмент — данных больше нет
        uint32_t j = (i + 1 < nin) ? (i + 1) : i;  // последняя пара — hold
        uint32_t f = ph & 0xFFFFu;
        int32_t l0 = in[2 * i],     r0 = in[2 * i + 1];
        int32_t l1 = in[2 * j],     r1 = in[2 * j + 1];
        out[2 * o]     = (int16_t)(l0 + (((l1 - l0) * (int32_t)f) >> 16));
        out[2 * o + 1] = (int16_t)(r0 + (((r1 - r0) * (int32_t)f) >> 16));
        o++;
        ph += A26_RS_INC;
    }
    // r1543: фаза обязана лежать ВНУТРИ фрагмента. Раньше после кадра (nin=524)
    // ph застревала на краю (i≈523), а условие `ph >= period` из-за округления
    // не срабатывало -> фаза оставалась у края, следующий кадр сразу выходил из
    // цикла, n=0, i2s_push_block получал 0 -> полная тишина после первого кадра
    // (лог r1542: audio heard peak=4096 pairs=0). Остаток по периоду = сдвиг на
    // целое число фрагментов, непрерывность входного потока сохраняется.
    a26_rs_phase = (uint32_t)(ph % ((uint64_t)nin << 16));
    return o;
}

// ---- диагностика звука (r1541) -------------------------------------------
// peak — максимальная амплитуда СЫРЬЯ TIA по модулю: 0 означает, что ядро
// отдаёт нули (AUDV не пишется или звук выключен в самом ядре), а не проблему
// вывода H3 — остальные системы на том же I2S звучат.
static uint32_t a26_dbg_frame = 0;
static uint32_t a26_dbg_peak_all = 0;
static int      a26_dbg_heard = 0;
static int      a26_dbg_beats = 0;
static uint32_t a26_dbg_pairs = 0;

static uint32_t a26_peak(const int16_t* buf, uint32_t count) {
    uint32_t peak = 0;
    for (uint32_t i = 0; i < count; i++) {
        int32_t v = buf[i];
        if (v < 0) v = -v;
        if ((uint32_t)v > peak) peak = (uint32_t)v;
    }
    return peak;
}

static void a26_log_audio(const char* what, uint32_t spf, uint32_t peak) {
    Sound& snd = osystem->sound();
    emulog_field("A2600", what,
                 "f=%u peak=%u spf=%u pairs=%u ring=%u vol=%d audv=%u/%u audc=%u/%u",
                 (unsigned)a26_dbg_frame, (unsigned)peak, (unsigned)spf,
                 (unsigned)a26_dbg_pairs, (unsigned)i2s_ring_fill(),
                 i2s_volume_pct(),
                 (unsigned)snd.registerValue(0x19),
                 (unsigned)snd.registerValue(0x1a),
                 (unsigned)snd.registerValue(0x15),
                 (unsigned)snd.registerValue(0x16));
}

extern "C" int a2600_init_game(const uint8_t* rom, uint32_t size,
                               const char* rom_name) {
    printf("A2600(Stella): init size=%u\n", (unsigned)size);
    g_loaded = 0;
    // r1525: сброс флага аппаратного скейлера ПРИ КАЖДОМ входе (урок r1496):
    // static переживает сессию, без сброса повторный запуск = чёрный экран.
    g_a26_hw_scaler_active = 0;
    a26_rs_phase = 0;
    a26_dbg_frame = 0;          // r1541: статистика звука — с нуля на игру
    a26_dbg_peak_all = 0;
    a26_dbg_heard = 0;
    a26_dbg_beats = 0;
    a26_dbg_pairs = 0;
    a26_color_bw = 0;           // r1531: новая игра всегда в цвете
    a26_rdiff_expert = 0;       // r1533: правая сложность — B/Novice
    a26_ctrl_left = A26C_JOY;   // r1534: до lookup — джойстик
    a26_ctrl_right = A26C_JOY;

    // r1527/r1528: свежий OSystem на каждый вход — уже ПОСЛЕ
    // newlib_heap_reset() (emu_prepare). Старый НЕ разрушаем: его члены
    // (Sound/EventHandler/PropertiesSet) живут в куче, которую сброс уже
    // отобрал, — delete по ним испортил бы хип. Утечка безвредна: куча
    // откатывается на следующем входе.
    osystem = new (s_osys_mem) OSystem();

    // Картридж: 2К..512К (кроме MovieCart >512К — не поддерживаем в H3).
    if (!rom || size < 2u * 1024u || size > 512u * 1024u) {
        printf("A2600: bad ROM size %u\n", (unsigned)size);
        return 0;
    }

    // Настройки: звук вкл., громкость 100, палитра standard, регион — AUTO
    // (Nucleus-детект в Console при Display_Format=AUTO из properties).
    if (!settings) settings = new Settings(osystem);
    settings->setValue("sound", Variant(true));
    settings->setValue("volume", Variant((int32_t)100));
    settings->setValue("palette", Variant("standard"));
    settings->setValue("romloadcount", Variant(false));
    osystem->attach(settings);

    string md5str = MD5(rom, size);
    Properties props;
    osystem->propSet().getMD5(md5str, props);

    // r1534: тип контроллеров по MD5 (база DefProps не подключена — все игры
    // получали JOYSTICK, а paddle-игры читают TIA INPT0-3 и не реагировали).
    // Таблица — только PADDLES*/KEYBOARD; остальные типы остаются JOYSTICK.
    int ctrl_swap = 0;
    a2600_ctrl_lookup(md5str.c_str(), rom_name,
                      &a26_ctrl_left, &a26_ctrl_right, &ctrl_swap);
    props.set(Controller_Left,  a26_ctrl_name(a26_ctrl_left));
    props.set(Controller_Right, a26_ctrl_name(a26_ctrl_right));
    if (ctrl_swap) props.set(Controller_SwapPaddles, "YES");
    printf("A2600: controller L=%s R=%s swap=%d\n",
           a26_ctrl_name(a26_ctrl_left), a26_ctrl_name(a26_ctrl_right),
           ctrl_swap);
    // r1535: фактические привязки (после /retro.cfg) — видно, что придёт с SD.
    printf("A2600: bind up=%u dn=%u lt=%u rt=%u fire=%u rst=%u sel=%u\n",
           (unsigned)remap_get(REMAP_PLAT_A2600, BTN_UP),
           (unsigned)remap_get(REMAP_PLAT_A2600, BTN_DOWN),
           (unsigned)remap_get(REMAP_PLAT_A2600, BTN_LEFT),
           (unsigned)remap_get(REMAP_PLAT_A2600, BTN_RIGHT),
           (unsigned)remap_get(REMAP_PLAT_A2600, BTN_FIRE),
           (unsigned)remap_get(REMAP_PLAT_A2600, BTN_RESET),
           (unsigned)remap_get(REMAP_PLAT_A2600, BTN_SELECT));

    // r1526/r1538: TV-режим из Settings. Auto (2) — это ЯВНОЕ "AUTO": пустое
    // свойство Stella автоопределением не считает (Console.cxx:114 сравнивает
    // с "AUTO"), формат оставался неизвестным и PAL-игры шли как NTSC.
    // NTSC/PAL форсируем в свойствах ДО Console — иначе автодетект перезапишет.
    props.set(Display_Format,
              a2600_tv_mode == 2 ? "AUTO" : (a2600_tv_mode ? "PAL" : "NTSC"));

    string cartType = "AUTO";   // автодетект типа (Cartridge::create)
    string cartId;
    cartridge = Cartridge::create(rom, size, md5str, cartType, cartId,
                                  *osystem, *settings, "");
    if (!cartridge) {
        printf("A2600: cart load failed\n");
        return 0;
    }

    console = new Console(osystem, cartridge, props);
    osystem->myConsole = console;
    console->initializeVideo();
    console->initializeAudio();

    // Событие: сброс флагов ввода (без этого Stella помнит предыдущую сессию).
    osystem->eventHandler().event().clear();
    osystem->eventHandler().reset(EventHandler::S_EMULATE);

    // r1526/r1531: сложность левого джойстика из меню (Settings «A2600
    // Difficulty»). Switches::update() проверяет A первой, поэтому «ровно
    // один из двух» действует и на входе — иначе позиция B была недостижима.
    osystem->eventHandler().event().set(Event::ConsoleLeftDiffA,
                                        a2600_diff_expert ? 1 : 0);
    osystem->eventHandler().event().set(Event::ConsoleLeftDiffB,
                                        a2600_diff_expert ? 0 : 1);

    printf("A2600: cart type=%s msgs=%s region=%s\n",
           cartType.c_str(),
           console->properties().get(Display_Format).c_str(),
           console->properties().get(Cartridge_Sound).c_str());
    g_loaded = 1;
    return 1;
}

extern "C" void a2600_run_frame(void) {
    if (!g_loaded || !console) return;

    build_input();

    // r1531: Event -> пины Controller и Switches. M6532::update() —
    // единственный пересчёт (M6532.cxx:82-99): Joystick::update() читает
    // myEvent, Switches::update() переводит Select/Reset/Diff/Color в SWCHB.
    // Без вызова пины оставались дефолтными («отпущено» из Controller ctor),
    // и ввод — клавиатура, геймпад, огонь, коммутаторы — до игры не доходил.
    // Вызывать каждый кадр ПОСЛЕ build_input() и ДО tia.update().
    console->riot().update();

    // Эмулировать один кадр.
    TIA& tia = console->tia();
    tia.update();

    // Звук: TIA-фрагмент в 31400 Гц стерео (по образцу libretro retro_run).
    static int16_t a26_frag[2048];          // 1024 стерео-пары — потолок кадра
    static int16_t a26_rs[2 * A26_RS_MAX_OUT];
    // r1537: сэмплов на кадр = 31400*den/num, как libretro.cxx:1766
    // (NTSC 1498/25 → 524, PAL 1248/25 → 629; потолок 1024 — размер a26_frag).
    // Фикс. 600 давал 600×59.92 ≈ 35950 Гц при A26_RS_INC = 31400 — темп и
    // тон завышены на NTSC и занижены на PAL.
    uint32_t frNum = console->getFramerateNum();
    uint32_t frDen = console->getFramerateDen();
    uint32_t tiaSamples = frNum ? (31400u * frDen) / frNum : 524u;
    if (tiaSamples > 1024u) tiaSamples = 1024u;
    osystem->sound().processFragment(a26_frag, tiaSamples);

    // r1541: амплитуда сырья уже полной шкалы (комментарий у A26_RS_INC),
    // усиления нет; DC-смещение снимает DC-блокер CPU2 (shift 6).
    uint32_t n = a26_resample(a26_frag, tiaSamples, a26_rs);
    if (n) i2s_push_block(a26_rs, n);

    // r1541: статистика звука. peak=0 и на 300-м кадре означает, что ядро
    // отдаёт нули (AUDV не пишется), а не потерю на стороне I2S.
    uint32_t peak = a26_peak(a26_frag, tiaSamples * 2u);
    if (peak > a26_dbg_peak_all) a26_dbg_peak_all = peak;
    a26_dbg_frame++;
    a26_dbg_pairs = n;
    if (peak && !a26_dbg_heard) {
        a26_dbg_heard = 1;
        a26_log_audio("audio heard", tiaSamples, peak);
    } else if (!a26_dbg_heard && a26_dbg_frame % 300u == 0u &&
               a26_dbg_beats < 12) {
        a26_dbg_beats++;
        a26_log_audio("audio silent", tiaSamples, a26_dbg_peak_all);
    }
}

extern "C" void a2600_stop(void) {
    if (!g_loaded) { printf("A2600: stop (not loaded)\n"); return; }
    g_loaded = 0;
    if (console) {
        osystem->sound().close();
        osystem->myConsole = 0;
        // r1529: ~Console -> ~System освобождает ВСЕ привязанные устройства,
        // включая картридж: Console.cxx:110 делает System::attach(myCart), а
        // System::~System() удаляет myDevices[] как свои. Повторный
        // delete cartridge бил по освобождённой памяти (стенд: P:00000000:0E,
        // LR 4, BL 0x400EE9F4 — blx в нулевой слот vtable).
        delete console;
        console = 0;
        cartridge = 0;          // уже освобождён внутри ~System
    }
    else if (cartridge) { delete cartridge; cartridge = 0; }
    if (settings) { delete settings; settings = 0; }
    // r1541: итог диагностики звука — одна строка при выходе в меню.
    if (a26_dbg_frame)
        emulog_field("A2600", "audio stop",
                     "f=%u peakAll=%u heard=%d pairs=%u vol=%d",
                     (unsigned)a26_dbg_frame, (unsigned)a26_dbg_peak_all,
                     a26_dbg_heard, (unsigned)a26_dbg_pairs, i2s_volume_pct());
    // r1527: OSystem разрушается явно — следующий вход сконструирует свежий
    // (память под него могла быть отобрана newlib_heap_reset).
    if (osystem) { osystem->~OSystem(); osystem = 0; }
    printf("A2600: stopped\n");
}

// r1538: период кадра — из частоты консоли, а не из пункта меню. Stella ставит
// её по региону (NTSC 1498/25 = 59.92 Гц, PAL 1248/25 = 49.92 Гц) при
// автоопределении или форсировании формата. Фикс. 16667 в Auto гнал PAL-игры
// на 60 Гц — отсюда «мерцание» и ускоренный звук.
extern "C" uint32_t a2600_frame_period_us(void) {
    if (!console) return 16667u;
    uint32_t num = console->getFramerateNum();
    uint32_t den = console->getFramerateDen();
    if (!num) return 16667u;
    uint32_t us = (uint32_t)((1000000ull * den) / num);
    if (us < 8000u)  us = 8000u;
    if (us > 30000u) us = 30000u;
    return us;
}

// ---- точка входа emu.c (заменяет emu_run_a2600_mcume) ----
extern "C" void emu_run_a2600(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    emulog_begin("A2600");
    emulog_field("A2600", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    snd_manifest("a2600", "tia");
    if (a2600_init_game(rom, size, rom_name) != 1) {
        emulog_field("A2600", "init", "FAILED");
        emulog_end("A2600");
        return;
    }
    emulog_field("A2600", "video", "320x240");
    emulog_field("A2600", "machine", "Stella 2014");
    emulog_field("A2600", "init", "OK");
    emu_set_border_color(0x00201A08);   // тёмно-янтарный (woodgrain A2600)
    // r1538: период — по фактической частоте консоли (Auto/PAL/NTSC), см.
    // a2600_frame_period_us(); пункт меню «TV mode» теперь влияет и на темп.
    emu_set_period_us(a2600_frame_period_us());
    emulog_field("A2600", "tv", "%s, %u us",
                 a2600_tv_mode == 2 ? "Auto" : (a2600_tv_mode ? "PAL" : "NTSC"),
                 (unsigned)a2600_frame_period_us());
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        a2600_run_frame();
        emu_throttle();
        // r1525: a2600_render_frame вызывается БЕЗУСЛОВНО каждый кадр (эталон
        // r1519) — именно он при первом кадре настраивает DE2 GSU.
        a2600_render_tia_to_fb();       // кадр TIA -> EMU_FB (RGB565 320×240)
        a2600_render_frame();
        if (!a2600_hw_scaler_active())
            emu_scale(320, 240);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    // r1525: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр A2600 и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    a2600_stop();
    emu_audio_stop();
    fb_clear(); fb_flush();
    emulog_end("A2600");
}