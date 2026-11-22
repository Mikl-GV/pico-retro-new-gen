/* cw_input.c — ввод USB -> IKBD (ST) для Castaway (r1566)
 *
 * USB-слой отдаёт удерживаемые HID usage-коды (usb_kbd_get_raw). Ядро ждёт
 * ST-сканкоды в IkbdKeyPress/IkbdKeyRelease. Таблица HID -> ST собрана по
 * keysymToAtari() из sdl/main.c оригинала Castaway.
 */
#include <stdint.h>
#include <string.h>

#include "st.h"          /* IkbdKeyPress / IkbdKeyRelease */
#include "cw_input.h"
#include "../usb_kbd.h"

/* HID usage (0x04..0xE7) -> ST scancode; 0 = не отображаем. */
static const uint8_t g_hid2st[256] = {
    /* 0x04..0x1D: a..z */
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20,
    [0x08] = 0x21, [0x09] = 0x22, [0x0A] = 0x23, [0x0B] = 0x24,
    [0x0C] = 0x25, [0x0D] = 0x26, [0x0E] = 0x27, [0x0F] = 0x28,
    [0x10] = 0x29, [0x11] = 0x2A, [0x12] = 0x2C, [0x13] = 0x2D,
    [0x14] = 0x2E, [0x15] = 0x2F, [0x16] = 0x30, [0x17] = 0x31,
    [0x18] = 0x32, [0x19] = 0x33, [0x1A] = 0x34, [0x1B] = 0x35,
    [0x1C] = 0x36, [0x1D] = 0x37,
    /* 0x1E..0x27: 1..0 */
    [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05,
    [0x22] = 0x06, [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09,
    [0x26] = 0x0A, [0x27] = 0x0B,
    /* 0x28..0x39 */
    [0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F,
    [0x2C] = 0x39, [0x2D] = 0x0C, [0x2E] = 0x0D, [0x2F] = 0x1A,
    [0x30] = 0x1B, [0x31] = 0x2B, [0x32] = 0x2B, [0x33] = 0x27,
    [0x34] = 0x28, [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34,
    [0x38] = 0x35, [0x39] = 0x3A,
    /* 0x3A..0x45: F1..F12 */
    [0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D, [0x3D] = 0x3E,
    [0x3E] = 0x3F, [0x3F] = 0x40, [0x40] = 0x41, [0x41] = 0x42,
    [0x42] = 0x43, [0x43] = 0x44, [0x44] = 0x62, [0x45] = 0x61,
    /* 0x4A..0x52: Home..Up */
    [0x4A] = 0x47, [0x4B] = 0x52, [0x4C] = 0x53, [0x4D] = 0x60,
    [0x4E] = 0x5E, [0x4F] = 0x4D, [0x50] = 0x4B, [0x51] = 0x50,
    [0x52] = 0x48,
    /* 0xE0..0xE7: модификаторы */
    [0xE0] = 0x1D, [0xE1] = 0x2A, [0xE2] = 0x38, [0xE3] = 0x38,
    [0xE4] = 0x1D, [0xE5] = 0x36, [0xE6] = 0x38, [0xE7] = 0x38,
};

#define CW_KEYS 16

static uint8_t g_prev[CW_KEYS];

void cw_input_init(void)
{
    memset(g_prev, 0, sizeof(g_prev));
}

void cw_input_poll(void)
{
    uint8_t now[CW_KEYS];
    int n = usb_kbd_get_raw(now, CW_KEYS);
    int i, j;

    if (n < 0)
        n = 0;
    if (n > CW_KEYS)
        n = CW_KEYS;

    /* Отпускания: были, стали нет. */
    for (i = 0; i < CW_KEYS; i++) {
        uint8_t code = g_prev[i];
        int still = 0;
        if (!code)
            continue;
        for (j = 0; j < n; j++)
            if (now[j] == code) { still = 1; break; }
        if (!still) {
            uint8_t st = g_hid2st[code];
            if (st)
                IkbdKeyRelease(st);
        }
    }
    /* Нажатия: стали есть, не было. */
    for (j = 0; j < n; j++) {
        int was = 0;
        for (i = 0; i < CW_KEYS; i++)
            if (g_prev[i] == now[j]) { was = 1; break; }
        if (!was) {
            uint8_t st = g_hid2st[now[j]];
            if (st)
                IkbdKeyPress(st);
        }
    }

    memset(g_prev, 0, sizeof(g_prev));
    memcpy(g_prev, now, (size_t)n);

    /* r1582: мышь/тачпад — сырые дельты трекпада со сглаживанием EMA и
     * масштабом 1/3; если относительного источника нет (тач на донгле
     * отдаёт абсолютную позицию) — дельта из usb_pad_get_pos, как в
     * Amiga-ядре uae4all_host.c. */
    {
        static int32_t acc_x, acc_y, sm_x, sm_y;
        static int last_x, last_y, have_last;
        int rdx = 0, rdy = 0;

        if (usb_mouse_take_delta(&rdx, &rdy)) {
            sm_x = (sm_x * 3 + rdx * 16) / 4;
            sm_y = (sm_y * 3 + rdy * 16) / 4;
        } else {
            int mx, my;
            if (usb_pad_get_pos(&mx, &my)) {
                if (!have_last) {
                    last_x = mx;
                    last_y = my;
                    have_last = 1;
                }
                sm_x = (mx - last_x) * 8;   /* множитель 1/2 в 1/16-шкале */
                sm_y = (my - last_y) * 8;
                last_x = mx;
                last_y = my;
            } else {
                sm_x = sm_y = 0;
            }
        }
        acc_x += sm_x / 3;
        acc_y += sm_y / 3;

        int odx = (int)(acc_x >> 4), ody = (int)(acc_y >> 4);
        acc_x -= (int32_t)(odx << 4);
        acc_y -= (int32_t)(ody << 4);
        if (odx || ody)
            IkbdMouseMotion(odx, ody);
    }

    /* Кнопки мыши: бит0=ЛКМ, бит1=ПКМ (IKBD: 1 — левая, 2 — правая). */
    {
        static int prev_btn;
        int btn = usb_pad_buttons();
        for (int b = 0; b < 2; b++) {
            int now_b = (btn >> b) & 1;
            int was_b = (prev_btn >> b) & 1;
            if (now_b != was_b) {
                if (now_b)
                    IkbdMousePress(1 << b);
                else
                    IkbdMouseRelease(1 << b);
            }
        }
        prev_btn = btn;
    }
}
