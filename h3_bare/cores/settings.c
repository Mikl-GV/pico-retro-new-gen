#include <string.h>
#include "fb_text.h"
#include "fat.h"
#include "systems.h"
#include "uart.h"
#include "usb_kbd.h"
extern int printf(const char* fmt, ...);

uint16_t emu_period_us = 16667;   // 60 Гц по умолчанию
uint8_t  a2600_diff_expert = 0;   // Novice по умолчанию

#define PHYS_W 1024
#define PHYS_H 600

#define FOOTER_Y (PHYS_H - 30)

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
        if (uart_rx_ready()) return uart_getc();
        int k = usb_input_poll();
        if (k) return k;
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

// --- Создать одну папку ---
static int ensure_dir(const char* name) {
    char path[32];
    strcpy(path, "/roms/");
    strcat(path, name);

    fat_entry_t e;
    if (fat_find("/roms", name, &e)) return 1;

    int r = fat_mkdir("/roms", name);
    if (r < 0) printf("mkdir fail: %s\n", name);
    return r >= 0;
}

void settings_run(void) {
    for (;;) {
        fb_draw_stars();
        fb_puts_s(60, 40, "Settings", 2, 0x00FF0000);
        fb_fill_rect(60, 70, 200, 2, 0x00FFFFFF);

        fb_puts_s(80, 110, "1 - Create default ROM folders", 1, 0x00FFFF00);
        fb_puts_s(80, 135, "2 - Input Test for NES / A2600", 1, 0x00FFFF00);
        fb_puts_s(80, 160, "3 - Video Mode", 1, 0x00FFFF00);
        {
            char buf[32];
            int l = 0;
            const char* p = emu_period_us == 16667 ? "60 Hz (NTSC)" : "50 Hz (PAL)";
            while (*p) buf[l++] = *p++;
            buf[l] = 0;
            fb_puts_s(280, 160, buf, 1, 0x00AAAAAA);
        }
        fb_puts_s(80, 185, "4 - Atari 2600 Difficulty", 1, 0x00FFFF00);
        {
            fb_puts_s(320, 185, a2600_diff_expert ? "Expert" : "Novice", 1, 0x00AAAAAA);
        }

        fb_puts(60, FOOTER_Y, "  1/2/3/4: select    ESC: back", 0x00888888);
        fb_flush();

        int k = input_wait();
        if (k == 41) {
            return;
        } else if (k == 40 || k == '\n' || k == '\r' || k == 30) {
            // Enter or "1" -> create folders
            goto create_folders;
        } else if (k == 31) {
            // "2" -> Input Test
            input_test_run();
        } else if (k == 32) {
            // "3" -> Video Mode 50/60 Гц
            emu_period_us = (emu_period_us == 16667) ? 20000 : 16667;
        } else if (k == 33) {
            // "4" -> Atari 2600 Difficulty
            a2600_diff_expert = !a2600_diff_expert;
        }
    }
create_folders:
    {
        fb_clear();
        fb_puts_s(60, 100, "Create all ROM folders?", 2, 0x00FFAA00);
        fb_puts_s(60, 140, "This will create /roms/<system>/", 1, 0x00FFFFFF);
        fb_puts_s(60, 170, "for all registered systems.", 1, 0x00FFFFFF);
        fb_puts_s(60, 210, "", 1, 0x00FFFFFF);
        fb_puts_s(80, 230, "  Enter: confirm    ESC: cancel", 1, 0x00888888);
        fb_flush();

        int confirm = input_wait();
        if (confirm == 40 || confirm == '\n' || confirm == '\r') {
            int ok = 0, fail = 0;
            for (int i = 0; i < (int)NUM_SYSTEMS; i++) {
                if (ensure_dir(system_rom_dir(i))) ok++; else fail++;
            }
            char buf[64];
            int len = 0;
            const char* fmt = "Created: ";
            while (*fmt) buf[len++] = *fmt++;
            buf[len] = 0;
            char ok_str[12];
            int ok_tmp = ok, i = 11;
            ok_str[11] = 0;
            do { ok_str[--i] = '0' + (ok_tmp % 10); ok_tmp /= 10; } while (ok_tmp);
            memcpy(buf + len, ok_str + i, 11 - i); len += 11 - i;
            const char* fmt2 = "   Failed: ";
            while (*fmt2) buf[len++] = *fmt2++;
            char fail_str[12];
            int fail_tmp = fail; i = 11; fail_str[11] = 0;
            do { fail_str[--i] = '0' + (fail_tmp % 10); fail_tmp /= 10; } while (fail_tmp);
            memcpy(buf + len, fail_str + i, 11 - i);
            fb_clear();
            fb_puts_s(60, 100, buf, 2, fail ? 0x00FF4444 : 0x0000FF00);
            fb_puts_s(60, 140, "Press any key", 1, 0x00AAAAAA);
            fb_flush();
            input_wait();
        }
    }
}