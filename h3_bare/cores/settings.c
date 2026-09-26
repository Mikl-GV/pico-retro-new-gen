#include <string.h>
#include <stdio.h>
#include "fb_text.h"
#include "fat.h"
#include "sd.h"
#include "systems.h"
#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "remap.h"
#include "h3_hs_timer.h"
extern int printf(const char* fmt, ...);

// r118: SRAM A1 почта калибровки тача (определена в tft_drv.c — дублируем
// адреса). .coherent между ядрами на этой плате не работает, поэтому запуск
// и результат калибровки идут через некэшируемую SRAM A1.
#define CAL_CMD (*(volatile uint32_t*)0x34u)   // core0→CPU1: 1 = калибровка
#define CAL_OK  (*(volatile uint32_t*)0x38u)   // CPU1→core0: 1 = OK
#define CAL_RX  ((volatile uint32_t*)0x3Cu)    // CPU1→core0: rx4[5]
#define CAL_RY  ((volatile uint32_t*)0x50u)    // CPU1→core0: ry4[5]

// r135: меню настроек на TFT (слоты, см. tft_drv.c)
#define SET_CMD   (*(volatile uint32_t*)0x78u) // core0→CPU1: 1 = режим настроек
#define SET_EV    (*(volatile uint32_t*)0x80u) // CPU1→core0: тап (0..7, 8=Back)
#define SET_EPOCH (*(volatile uint32_t*)0x84u) // core0→CPU1: перерисовать
#define SET_MODE  (*(volatile uint32_t*)0x88u) // core0→CPU1: подрежим (0=список,1=partition,2=confirm,3=result)
#define SET_INFO  (*(volatile uint32_t*)0x8Cu) // core0→CPU1: результат подрежима (create-счётчики)

uint16_t emu_period_us = 16667;   // 60 Гц по умолчанию
uint8_t  a2600_diff_expert = 0;   // Novice по умолчанию

#define PHYS_W 1024
#define PHYS_H 600

#define FOOTER_Y (PHYS_H - 30)

static void sega_pad_test_run(void);   // прототип — определён ниже
static int input_wait(void);           // определена ниже — для touch_cal_run

// r112: калибровка тача — вызов из меню настроек
// r118: команда и результат — через SRAM-почту (TFT_CMD/CAL_*). CPU1 рисует
// мишени, кладёт результат в CAL_OK/CAL_RX/CAL_RY и снимает CAL_CMD=0.
static void touch_cal_run(void) {
    fb_clear();
    fb_puts_s(60, 150, "Touch calibration...", 2, 0x00FFAA00);
    fb_puts_s(60, 200, "tap 5 TFT targets (1-4-C)", 1, 0x00FFFFFF);
    fb_puts_s(60, 225, "ESC - cancel", 1, 0x00888888);
    fb_flush();

    // r117 debug: что CPU1 делает перед калибровкой (ожидаем 0x02 = меню)
    printf("cal: TFT_STAT=0x%X before\n", *(volatile uint32_t*)0x24u);

    CAL_OK = 0;    // r119: чистим результат перед запуском (SRAM не zero-инициализируется)
    CAL_CMD = 1;

    // ждём завершения калибровки без таймаута: CPU1 выходит сам по 5 тапам
    // (снимет CAL_CMD в 0), либо прерываем по ESC с клавиатуры/геймпада.
    int cancelled = 0;
    while (CAL_CMD == 1) {
        int k = usb_input_poll();
        if (k == 41) {              // ESC — прервать калибровку
            cancelled = 1;
            CAL_CMD = 0;
            break;
        }
        for (int j = 0; j < 1000; j++) udelay(100);   // ~100 мс
    }

    int calok = (int)CAL_OK;
    // r117 debug: TFT_STAT=0x2E → CPU1 входил в калибровку, 0x2F → завершил
    printf("cal: TFT_STAT=0x%X after cmd=%d cancel=%d ok=%d\n",
           *(volatile uint32_t*)0x24u, (int)CAL_CMD, cancelled, calok);

    fb_clear();
    if (cancelled) {
        fb_puts_s(60, 100, "Cancelled", 2, 0x00FFAA00);
        fb_puts_s(60, 150, "Calibration not changed", 1, 0x00FFFFFF);
    } else if (calok) {
        fb_puts_s(60, 100, "OK", 2, 0x0000FF00);
        fb_puts_s(60, 150, "Touch calibrated!", 2, 0x00FFAA00);
    } else {
        fb_puts_s(60, 100, "NOT OK", 2, 0x00FF4444);
        fb_puts_s(60, 150, "Repeat calibration", 1, 0x00FFFFFF);
    }
    for (int i = 0; i < 5; i++) {
        char buf[64];
        snprintf(buf, 64, "pt%d rx=%d ry=%d", i + 1,
                 (int)(int32_t)CAL_RX[i], (int)(int32_t)CAL_RY[i]);
        fb_puts_s(60, 180 + i * 20, buf, 1, 0x00AAAAAA);
    }
    fb_puts_s(60, 300, "Press any key", 1, 0x00888888);
    fb_flush();
    input_wait();
}

static const char* key_name(uint8_t sc) {
    switch (sc) {
        case 4: return "A"; case 5: return "B"; case 6: return "C"; case 7: return "D";
        case 8: return "E"; case 9: return "F"; case 10: return "G"; case 11: return "H";
        case 12: return "I"; case 13: return "J"; case 14: return "K"; case 15: return "L";
        case 16: return "M"; case 17: return "N"; case 18: return "O"; case 19: return "P";
        case 20: return "Q"; case 21: return "R"; case 22: return "S"; case 23: return "T";
        case 24: return "U"; case 25: return "V"; case 26: return "W"; case 27: return "X";
        case 28: return "Y"; case 29: return "Z";
        case 30: return "1"; case 31: return "2"; case 32: return "3"; case 33: return "4";
        case 34: return "5"; case 35: return "6"; case 36: return "7"; case 37: return "8";
        case 38: return "9"; case 39: return "0";
        case 40: return "Enter"; case 41: return "ESC"; case 42: return "BkSp"; case 43: return "Tab";
        case 44: return "Space";
        case 79: return "RArr"; case 80: return "LArr"; case 81: return "DArr"; case 82: return "UArr";
        case 89: return "K1";
        default: return "??";
    }
}

static int input_wait(void) {
    for (;;) {
        int k = usb_input_poll();
        if (k) return k;
        // r155: холостая итерация — пауза ~1 кадр, иначе usb_input_poll
        // дёргает I2C геймпада каждые 2 мс (500 сканов/с) и джой «тупит»
        // от гонок PA_DAT с TFT-ядром (то же, что в главном меню).
        udelay(16000);
    }
}

// --- Input Test with NES Famicom D-pad diagram ---
static void draw_pad(int cx, int cy, int active_bits) {
    uint32_t on  = 0x0000FF00;  // зелёный = нажато
    uint32_t off = 0x00444444;  // серый = фон

    // Cross D-pad: centre at (cx, cy), arms 50px, thickness 16px
    // Up = bit3 (0x08), Down = bit2 (0x04), Left = bit1 (0x02), Right = bit0 (0x01)
    int arm = 50, thick = 16;
    // Vertical bar (up + down)
    fb_fill_rect(cx - thick/2, cy - arm, thick, arm*2, off);
    // Horizontal bar (left + right)
    fb_fill_rect(cx - arm, cy - thick/2, arm*2, thick, off);
    // Centre circle
    fb_fill_rect(cx - thick/2, cy - thick/2, thick, thick, 0x00666666);

    // Active overlays
    if (active_bits & 0x08) fb_fill_rect(cx - thick/2, cy - arm, thick, arm, on);     // Up
    if (active_bits & 0x04) fb_fill_rect(cx - thick/2, cy, thick, arm, on);           // Down
    if (active_bits & 0x02) fb_fill_rect(cx - arm, cy - thick/2, arm, thick, on);     // Left
    if (active_bits & 0x01) fb_fill_rect(cx, cy - thick/2, arm, thick, on);           // Right

    // Labels
    fb_puts_s(cx - 8, cy - arm - 20, "Up", 1, 0x00FFFFFF);
    fb_puts_s(cx - 12, cy + arm + 5, "Down", 1, 0x00FFFFFF);
    fb_puts_s(cx - arm - 28, cy - 8, "Left", 1, 0x00FFFFFF);
    fb_puts_s(cx + arm + 5, cy - 8, "Right", 1, 0x00FFFFFF);

    // A/B buttons (to the right of d-pad)
    int bx = cx + arm + 100, by = cy;
    // A button (bit7 = 0x80) — outer circle
    fb_fill_rect(bx + 40, by - 8, 24, 16, off);
    fb_fill_rect(bx + 44, by - 12, 16, 24, off);
    if (active_bits & 0x80) {
        fb_fill_rect(bx + 40, by - 8, 24, 16, on);
        fb_fill_rect(bx + 44, by - 12, 16, 24, on);
    }
    // B button (bit6 = 0x40)
    fb_fill_rect(bx + 10, by - 8, 24, 16, off);
    fb_fill_rect(bx + 14, by - 12, 16, 24, off);
    if (active_bits & 0x40) {
        fb_fill_rect(bx + 10, by - 8, 24, 16, on);
        fb_fill_rect(bx + 14, by - 12, 16, 24, on);
    }
    fb_puts_s(bx + 44, by + 12, "A", 1, active_bits & 0x80 ? on : 0x00AAAAAA);
    fb_puts_s(bx + 18, by + 12, "B", 1, active_bits & 0x40 ? on : 0x00AAAAAA);

    // Start/Select (below A/B)
    int sy = cy + arm + 40;
    fb_fill_rect(bx, sy, 30, 14, off);     // Select
    fb_fill_rect(bx + 45, sy, 30, 14, off); // Start
    if (active_bits & 0x20) fb_fill_rect(bx, sy, 30, 14, on);      // Select
    if (active_bits & 0x10) fb_fill_rect(bx + 45, sy, 30, 14, on); // Start
    fb_puts_s(bx + 1, sy + 16, "Sel", 1, active_bits & 0x20 ? on : 0x00AAAAAA);
    fb_puts_s(bx + 46, sy + 16, "Strt", 1, active_bits & 0x10 ? on : 0x00AAAAAA);
}

static void input_test_run(void) {
    for (;;) {
        // build NES pad bits from current key state
        uint8_t keys[6];
        int n = usb_kbd_get_raw(keys, 6);
        uint8_t pad = 0;
        for (int i = 0; i < n; i++) {
            uint8_t sc = keys[i];
            if (sc == 82) pad |= 0x08;  // UArrow = Up
            if (sc == 81) pad |= 0x04;  // DArrow = Down
            if (sc == 80) pad |= 0x02;  // LArrow = Left
            if (sc == 79) pad |= 0x01;  // RArrow = Right
            if (sc == 29) pad |= 0x80;  // Z = A
            if (sc == 27) pad |= 0x40;  // X = B
            if (sc == 40) pad |= 0x10;  // Enter = Start
            if (sc == 22) pad |= 0x20;  // S = Select
        }

        fb_draw_stars();
        fb_puts_s(60, 15, "Input Test — NES Famicom", 2, 0x00FF0000);

        // Draw controller diagram
        draw_pad(300, 200, pad);

        // Key list (left side)
        int y = 80;
        fb_puts_s(80, y, "Pressed keys:", 1, 0x00FFFF00);
        y += 24;
        if (n == 0) {
            fb_puts_s(100, y, "(none)", 1, 0x00888888);
            y += 24;
        }
        for (int i = 0; i < n; i++) {
            char buf[40];
            int pos = 0;
            uint8_t sc = keys[i];
            int u = sc / 100; if (u) buf[pos++] = '0'+u;
            u = (sc/10)%10; buf[pos++] = '0'+u;
            buf[pos++] = '0'+(sc%10);
            buf[pos++] = ' '; buf[pos++] = '(';
            const char* kn = key_name(sc);
            while (*kn) buf[pos++] = *kn++;
            buf[pos++] = ')'; buf[pos] = 0;
            fb_puts_s(100, y, buf, 1, 0x00FFFFFF);
            y += 22;
        }

        // Mapping legend
        y = 80;
        int x = 560;
        fb_puts_s(x, y, "Mapping:", 1, 0x00FFFF00); y += 24;
        fb_puts_s(x, y, "Arrows = D-Pad", 1, 0x00AAAAAA); y += 20;
        fb_puts_s(x, y, "Z = A    X = B", 1, 0x00AAAAAA); y += 20;
        fb_puts_s(x, y, "Enter = Start", 1, 0x00AAAAAA); y += 20;
        fb_puts_s(x, y, "S = Select", 1, 0x00AAAAAA); y += 20;
        fb_puts_s(x, y, "ESC = Exit", 1, 0x00AAAAAA);

        fb_puts(60, FOOTER_Y, "ESC: back", 0x00888888);
        fb_flush();

        int k = input_wait();
        if (k == 41) return;
    }
}

// --- Sega 6-button gamepad test (PCF8574@0x20, TWI0 PA11/PA12) ---
// Показывает побитный скан по фазам SELECT (классический протокол):
//   raw[0] = ЦИКЛ1 TH=1: Up/Dn/L/R + B/C (TL/TR)
//   raw[1] = ЦИКЛ1 TH=0: A/Start (TL/TR)
//   raw[2] = ЦИКЛ3 TH=1: Z/Y/X/Mode (D0-D3)
// Выход: клавиша ESC или УДЕРЖАНИЕ Start ~0.8 с (не B — B это кнопка теста).
static void sega_pad_test_run(void) {
    sega_pad_init();

    uint16_t prev = 0xFFFF;
    uint32_t last_tick = 0;
    uint32_t start_hold = 0;
    uint32_t no_start_since = 0;   // r155: сколько времени Start отсутствует
    for (;;) {
        // --- СВОЙ слой: один аппаратный скан на кадр, антидребезг внутри ---
        usb_pad_update();
        uint16_t pad = usb_pad_get();

        // Печать только при смене состояния (нажатие/отпускание) — без спама
        if (pad != prev) {
            uint8_t raw[3];
            sega_pad_get_raw(raw);
            uint32_t st = sega_pad_get_status();
            char line[160];
            snprintf(line, sizeof(line),
                "SEGA: r=%02X/%02X/%02X 0x%04X ack=%d pad=%d t=%uus",
                raw[0], raw[1], raw[2], pad,
                !!(st & SEGA_STATUS_ACK), !!(st & SEGA_STATUS_PAD),
                (unsigned)sega_pad_get_scan_us());
            uart_puts(line);
            uart_puts("\n");
            prev = pad;
        }

        fb_draw_stars();
        fb_puts_s(60, 30, "Sega 6-button gamepad", 2, 0x00FFAA00);
        fb_fill_rect(60, 60, 220, 2, 0x00FFFFFF);

        int y = 90;
        uint8_t raw[3];
        sega_pad_get_raw(raw);
        char buf[96];

        fb_puts_s(80, y, "Raw reads (1=released, 0=pressed):", 1, 0x00FFFF00); y += 24;
        snprintf(buf, sizeof(buf), "TH1: %02X  Up=%d Dn=%d L=%d R=%d B=%d C=%d",
            raw[0], !!(raw[0]&0x01), !!(raw[0]&0x02), !!(raw[0]&0x04), !!(raw[0]&0x08),
            !!(raw[0]&0x10), !!(raw[0]&0x20));
        fb_puts_s(100, y, buf, 1, 0x00FFFFFF); y += 20;
        snprintf(buf, sizeof(buf), "TH0: %02X  A=%d St=%d",
            raw[1], !!(raw[1]&0x10), !!(raw[1]&0x20));
        fb_puts_s(100, y, buf, 1, 0x00FFFFFF); y += 20;
        snprintf(buf, sizeof(buf), "C3 : %02X  Z=%d Y=%d X=%d Mode=%d",
            raw[2], !!(raw[2]&0x01), !!(raw[2]&0x02), !!(raw[2]&0x04), !!(raw[2]&0x08));
        fb_puts_s(100, y, buf, 1, 0x00FFFFFF); y += 26;

        snprintf(buf, sizeof(buf), "Time: %uus  Raw: %02X/%02X/%02X",
            (unsigned)sega_pad_get_scan_us(), raw[0], raw[1], raw[2]);
        fb_puts_s(80, y, buf, 1, 0x00AAAAAA); y += 26;

        // Сводка какие кнопки зажаты
        static const struct { uint16_t bit; const char* name; } map[] = {
            { 0x001, "Up" }, { 0x002, "Down" }, { 0x004, "Left" }, { 0x008, "Right" },
            { 0x010, "A" }, { 0x020, "B" }, { 0x040, "C" }, { 0x080, "Start" },
            { 0x100, "X" }, { 0x200, "Y" }, { 0x400, "Z" }, { 0x800, "Mode" },
        };
        fb_puts_s(80, y, "Pressed:", 1, 0x00FFFF00); y += 24;
        int any = 0;
        for (int i = 0; i < 12; i++) {
            if (pad & map[i].bit) {
                any = 1; fb_puts_s(100, y, map[i].name, 1, 0x0000FF66); y += 20;
            }
        }
        if (!any) { fb_puts_s(100, y, "(none)", 1, 0x00888888); y += 20; }

        fb_puts(60, FOOTER_Y, "ESC / hold Start: back", 0x00888888);
        fb_flush();

        // Ограничение ~60 fps для комфорта.
        // r155: правильная мкс-пауза через udelay (множитель 97 в udelay.c).
        // Раньше стояло h3_hs_timer_delay((16667-elapsed)*24) — множитель 24
        // от старой шкалы 24 МГц: при 97 МГц тест крутился в ~4 раза быстрее.
        uint32_t now = h3_hs_timer_lo_us();
        uint32_t elapsed = now - last_tick;
        if (elapsed < 16667) udelay(16667 - elapsed);
        last_tick = h3_hs_timer_lo_us();

        // Выход: клавиша ESC (41), или удержание Start ~0.8 с (бит 0x0080).
        // НЕ через usb_input_poll — он мапит B в ESC; здесь обрабатываем
        // слой геймпада напрямую (скан уже сделан в начале кадра).
        // r155: счётчик НЕ сбрасывается на одиночном «моргании» скана —
        // start_hold теряется только если Start отсутствует >200 мс подряд.
        // Иначе дребезг/сбой I2C не давал выйти (тест «зависал» на выходе).
        int k = usb_kbd_poll();
        if (k == 41) return;
        if (pad & 0x0080) {
            no_start_since = 0;
            if (!start_hold) start_hold = h3_hs_timer_lo_us();
            else if (h3_hs_timer_lo_us() - start_hold > 800000) return;
        } else if (start_hold) {
            uint32_t ns = now;
            if (!no_start_since) no_start_since = ns;
            else if (ns - no_start_since > 200000) { start_hold = 0; no_start_since = 0; }
        }
    }
}

// --- Создать одну папку ---
// Возвращает: 1 = создана, 2 = уже существует, 0 = ошибка
static int ensure_dir(const char* name) {
    if (!name || !name[0]) return 2;
    fat_entry_t e;
    if (fat_find("/roms", name, &e)) return 2;

    int r = fat_mkdir("/roms", name);
    if (r < 0) printf("mkdir fail: %s\n", name);
    return r >= 0 ? 1 : 0;
}

// Периоды кадра в мкс для доступных частот (index = позиция цикла)
static const uint16_t period_table[] = { 16667, 20000, 22222, 25000, 28571, 33333 };
static const char* const freq_table[] = { "60 Hz (NTSC)", "50 Hz (PAL)", "45 Hz", "40 Hz", "35 Hz", "30 Hz" };
#define FREQ_COUNT (sizeof(period_table) / sizeof(period_table[0]))

// индекс текущей частоты по emu_period_us
static int current_freq_idx(void) {
    for (int i = 0; i < (int)FREQ_COUNT; i++)
        if (emu_period_us == period_table[i]) return i;
    return 0;   // если значение не из таблицы (старое) — считаем 60 Гц
}

// Изменение частоты по кругу: dir = +1 (вправо/дальше), -1 (влево/назад)
static void freq_next_dir(int dir) {
    int i = current_freq_idx();
    i = (i + dir + (int)FREQ_COUNT) % (int)FREQ_COUNT;
    emu_period_us = period_table[i];
    printf("fps: %s (period=%u us)\n", freq_table[i], (unsigned)emu_period_us);
}

// Пункты меню настроек
enum {
    SET_CREATE_FOLDERS = 0,
    SET_INPUT_TEST,
    SET_VIDEO_MODE,
    SET_A2600_DIFF,
    SET_SEGA_PAD,
    SET_KEYBOARD_REMAP,
    SET_PART_INFO,
    SET_TOUCH_CAL,
    SET_COUNT,
};

static const char* const set_labels[SET_COUNT] = {
    "Create ROM system folders",
    "Input Test for NES / A2600",
    "Video Mode / Throttle",
    "Atari 2600 Difficulty",
    "Sega 6-button gamepad",
    "Keyboard remap (per system)",
    "ROM partition info",
    "Touch Calibration (TFT)",
};

// Рисуем меню настроек с курсором
static void settings_draw(int sel) {
    fb_draw_stars();
    fb_puts_s(60, 40, "Settings", 2, 0x00FF0000);
    fb_fill_rect(60, 70, 200, 2, 0x00FFFFFF);

    int y = 110;
    for (int i = 0; i < SET_COUNT; i++) {
        int is_sel = (i == sel);
        uint32_t clr = is_sel ? 0x00FFFF00 : 0x00FFFFFF;
        if (is_sel)
            fb_fill_rect(50, y - 4, PHYS_W - 100, 28, 0x00181818);
        // маркер курсора
        if (is_sel)
            fb_puts_s(58, y, ">", 1, 0x00FFFF00);
        fb_puts_s(80, y, set_labels[i], 1, clr);

        // Значение справа (для переключаемых)
        if (i == SET_VIDEO_MODE) {
            int fi = current_freq_idx();
            fb_puts_s(440, y, freq_table[fi], 1, 0x00AAAAAA);
            fb_puts_s(440, y + 16, "Enter to change", 1, 0x00666666);
        } else if (i == SET_A2600_DIFF) {
            fb_puts_s(440, y, a2600_diff_expert ? "Expert" : "Novice", 1, 0x00AAAAAA);
        }
        y += 34;
    }

    fb_puts(60, FOOTER_Y, "  ^v : select    <- -> : change    Enter : action    ESC : back", 0x00888888);
    fb_flush();
}

// Создание папок с ДВОЙНЫМ подтверждением
static void create_folders_flow(void) {
    // Проверяем, есть ли /roms
    fat_entry_t dummy;
    int has_roms = fat_find("/", "roms", &dummy);
    if (!has_roms) {
        fb_clear();
        fb_text_center("ROM partition not found!", 100, 2, 0x00FF4444);
        fb_puts_s(60, 160, "Create a FAT32 partition on the SD", 1, 0x00FFFFFF);
        fb_puts_s(60, 185, "and create folder 'roms' in its root.", 1, 0x00FFFFFF);
        fb_puts_s(60, 210, "Then come back and try again.", 1, 0x00FFFFFF);
        fb_puts_s(60, 260, "Press any key", 1, 0x00888888);
        fb_flush();
        input_wait();
        return;
    }

    // Подтверждение 1: предупреждение
    for (;;) {
        fb_clear();
        fb_puts_s(60, 60, "Create ROM system folders?", 2, 0x00FFAA00);
        fb_puts_s(60, 120, "This will create /roms/<system>/", 1, 0x00FFFFFF);
        fb_puts_s(60, 145, "for ALL registered systems on SD.", 1, 0x00FFFFFF);
        fb_puts_s(60, 175, "No data will be deleted.", 1, 0x00AAAAAA);
        fb_puts_s(80, 260, "  Enter: continue    ESC: cancel", 1, 0x00888888);
        fb_flush();
        int k = input_wait();
        if (k == 41) return;                    // ESC -> отмена
        if (k == 40) break;  // Enter -> дальше
    }

    // Подтверждение 2: финальное
    for (;;) {
        fb_clear();
        fb_puts_s(60, 80, "Are you SURE?", 2, 0x00FF4444);
        fb_puts_s(60, 140, "This writes folders to the SD card", 1, 0x00FFFFFF);
        fb_puts_s(60, 165, "and is not reversible.", 1, 0x00FFFFFF);
        fb_puts_s(80, 260, "  Enter: CREATE    ESC: cancel", 1, 0x00888888);
        fb_flush();
        int k = input_wait();
        if (k == 41) return;                    // ESC -> отмена
        if (k == 40) break;  // Enter -> создаём
    }

    // Создание: для систем с alt_dir создаём ОБЕ папки (dir/alt),
    // т.к. меню может искать ROM-папку по alt_dir (nes→nes_roms, sms→sms_roms).
    int created = 0, existing = 0, fail = 0;
    for (int i = 0; i < (int)NUM_SYSTEMS; i++) {
        int r = ensure_dir(system_rom_dir(i));
        if (r == 1) created++;
        else if (r == 2) existing++;
        else fail++;
        if (systems[i].alt_dir) {
            r = ensure_dir(systems[i].alt_dir);
            if (r == 1) created++;
            else if (r == 2) existing++;
            else fail++;
        }
    }

    fb_clear();
    char buf[64];
    if (created > 0) {
        int len = 0;
        const char* p = "Created: ";
        while (*p) buf[len++] = *p++;
        int tmp = created, ii = 11; char s[12];
        s[11] = 0;
        do { s[--ii] = '0' + (tmp % 10); tmp /= 10; } while (tmp);
        memcpy(buf + len, s + ii, 11 - ii); len += 11 - ii;
        buf[len] = 0;
        fb_puts_s(60, 80, buf, 2, 0x0000FF00);
    }
    if (existing > 0) {
        int len = 0;
        const char* p = "Already exist: ";
        while (*p) buf[len++] = *p++;
        int tmp = existing, ii = 11; char s[12];
        s[11] = 0;
        do { s[--ii] = '0' + (tmp % 10); tmp /= 10; } while (tmp);
        memcpy(buf + len, s + ii, 11 - ii); len += 11 - ii;
        buf[len] = 0;
        fb_puts_s(60, 110, buf, 1, 0x00AAAAAA);
    }
    if (fail > 0)
        fb_puts_s(60, 140, "Some folders failed! Check UART", 1, 0x00FF4444);
    fb_puts_s(60, 200, "Press any key", 1, 0x00AAAAAA);
    fb_flush();
    input_wait();
}

void settings_run(void) {
    int sel = 0;

    for (;;) {
        settings_draw(sel);
        int k = input_wait();

        if (k == 41) return;                       // ESC -> выход
        else if (k == 82) { sel--; if (sel < 0) sel = SET_COUNT - 1; }  // Up
        else if (k == 81) { sel++; if (sel >= SET_COUNT) sel = 0; }      // Down

        // Стрелки влево/вправо: меняют значение выбранного переключаемого пункта
        else if (k == 80 || k == 79) {   // LArr / RArr
            int dir = (k == 79) ? 1 : -1;   // RArr = следующее, LArr = предыдущее
            switch (sel) {
            case SET_VIDEO_MODE:
                freq_next_dir(dir);
                break;
            case SET_A2600_DIFF:
                a2600_diff_expert = !a2600_diff_expert;
                break;
            default:
                break;
            }
        }
        else if (k == 40) {
            switch (sel) {
            case SET_CREATE_FOLDERS:
                create_folders_flow();
                break;
            case SET_INPUT_TEST:
                input_test_run();
                break;
            case SET_VIDEO_MODE:
                freq_next_dir(1);
                break;
            case SET_A2600_DIFF:
                a2600_diff_expert = !a2600_diff_expert;
                break;
            case SET_SEGA_PAD:
                sega_pad_test_run();
                break;
            case SET_KEYBOARD_REMAP:
                remap_menu();
                break;
            case SET_TOUCH_CAL:
                touch_cal_run();
                break;
            case SET_PART_INFO:
                goto partition_info;
            }
        }
        // клавиши 1..7 тоже работают для быстрого доступа
        else if (k == 30) { sel = SET_CREATE_FOLDERS; }
        else if (k == 31) { sel = SET_INPUT_TEST; }
        else if (k == 32) { sel = SET_VIDEO_MODE; }
        else if (k == 33) { sel = SET_A2600_DIFF; }
        else if (k == 34) { sel = SET_SEGA_PAD; }
        else if (k == 35) { sel = SET_KEYBOARD_REMAP; }
        else if (k == 38) { sel = SET_PART_INFO; }
        else if (k == 39) { sel = SET_TOUCH_CAL; }
    }

partition_info:
    {
        fb_clear();
        fb_puts_s(60, 40, "ROM partition setup", 2, 0x00FFAA00);
        fb_fill_rect(60, 70, 200, 2, 0x00FFFFFF);
        fb_puts_s(60, 90, "1. Create FAT32 partition in Windows", 1, 0x00FFFFFF);
        fb_puts_s(60, 115, "   (DiskPart / GUI / second partition)", 1, 0x00AAAAAA);
        fb_puts_s(60, 145, "2. Create folder 'roms' in its root", 1, 0x00FFFFFF);
        fb_puts_s(60, 175, "3. Put ROM files in /roms/<system>/", 1, 0x00FFFFFF);
        fb_puts_s(60, 205, "4. Insert & reboot the console", 1, 0x00FFFFFF);
        fb_puts_s(60, 240, "Then Settings -> Create folders", 1, 0x00FFFF00);
        fb_puts_s(60, 270, "Press any key", 1, 0x00888888);
        fb_flush();
        input_wait();
    }
}

// r135: обработчик меню настроек на TFT. core0 держит SET_CMD=1; CPU1 в
// tft_settings_mode рисует кнопки-строки и шлёт тапы в SET_EV. Здесь читаем
// события и выполняем действия пунктов (порядок = SET_*).
extern void remap_menu(void);

// r136: создание папок без HDMI-экранов (только для TFT). Возвращает
// упакованный счётчик: (created) | (existing<<8) | (failed<<16)
static uint32_t create_folders_silent(void) {
    int created = 0, existing = 0, fail = 0;
    for (int i = 0; i < (int)NUM_SYSTEMS; i++) {
        int r = ensure_dir(system_rom_dir(i));
        if (r == 1) created++;
        else if (r == 2) existing++;
        else fail++;
        if (systems[i].alt_dir) {
            r = ensure_dir(systems[i].alt_dir);
            if (r == 1) created++;
            else if (r == 2) existing++;
            else fail++;
        }
    }
    return (uint32_t)created | ((uint32_t)existing << 8) | ((uint32_t)fail << 16);
}

// r141: ждём, пока CPU1 пришлёт событие из тача (SET_EV), либо SET_CMD снимется.
// «Нет события» = -1 (0 — валидный пункт Create folders!). Параллельно
// опрашиваем клавиатуру: ESC = Back(8), Enter = Continue/CREATE(1).
static int tft_set_wait_ev(void) {
    for (;;) {
        if (SET_CMD != 1) return -1;
        int ev = (int)SET_EV;
        if (ev >= 0 && ev <= 8) { SET_EV = (uint32_t)0xFFFFFFFFu; return ev; }
        int k = usb_input_poll();
        if (k == 41) { SET_EV = (uint32_t)0xFFFFFFFFu; return 8; }   // ESC = Back
        if (k == 40) { SET_EV = (uint32_t)0xFFFFFFFFu; return 1; }   // Enter = Continue/CREATE
        for (int j = 0; j < 300; j++) udelay(100);
    }
}

void touch_settings_run(void) {
    SET_EV = (uint32_t)0xFFFFFFFFu;   // r141: нет события (0 = пункт!)
    SET_EPOCH++;                      // перерисовать меню при входе
    SET_CMD = 1;
    while (SET_CMD == 1) {
        int ev = (int)SET_EV;
        if (ev < 0 || ev > 8) { for (int j = 0; j < 300; j++) udelay(100); continue; }
        SET_EV = (uint32_t)0xFFFFFFFFu;   // взял событие
        if (ev == 8) { SET_CMD = 0; break; }   // Back
        switch (ev) {
            case 0: {
                // Create folders — ДВОЙНОЕ подтверждение на TFT (как HDMI):
                // 1) предупреждение (mode 2, Continue/Back)
                // 2) «Are you SURE?» (mode 4, CREATE/Back)
                SET_MODE = 2; SET_EPOCH++;
                int a = tft_set_wait_ev();       // ждём Continue(1) или Back(8)
                if (a == 1) {
                    SET_MODE = 4; SET_EPOCH++;   // Are you SURE?
                    int s = tft_set_wait_ev();   // ждём CREATE(1) или Back(8)
                    if (s == 1) {
                        SET_INFO = (int32_t)create_folders_silent();
                        SET_MODE = 3; SET_EPOCH++;   // результат
                    } else {
                        SET_MODE = 0; SET_EPOCH++;
                        break;
                    }
                } else {
                    SET_MODE = 0; SET_EPOCH++;
                    break;
                }
                tft_set_wait_ev();               // ждём Back(8) с результата
                SET_MODE = 0; SET_EPOCH++;
                break;
            }
            case 1: input_test_run(); break;   // HDMI-тесты — следующая итерация
            case 2:
                freq_next_dir(1);
                *(volatile uint32_t*)0x74u = emu_period_us;   // синхрон для TFT
                break;
            case 3:
                a2600_diff_expert = !a2600_diff_expert;
                *(volatile uint32_t*)0x70u = a2600_diff_expert;
                break;
            case 4: sega_pad_test_run(); break; // HDMI-тесты — следующая итерация
            case 5: remap_menu(); break;        // HDMI-ремап — следующая итерация
            case 6: {
                // калибровка: выйти из меню, запустить CAL_CMD, вернуться
                SET_CMD = 0;
                CAL_CMD = 1;
                while (CAL_CMD == 1) { for (int j = 0; j < 1000; j++) udelay(100); }
                SET_EPOCH++;
                SET_CMD = 1;
                break;
            }
            case 7: {
                // Partition info — подрежим на TFT
                SET_MODE = 1; SET_EPOCH++;
                tft_set_wait_ev();              // ждём Back(8)
                SET_MODE = 0; SET_EPOCH++;
                break;
            }
        }
        SET_EPOCH++;         // вернулись из действия — перерисовать на TFT
    }
}