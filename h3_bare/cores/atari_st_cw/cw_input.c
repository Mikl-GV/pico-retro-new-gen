/* cw_input.c — ввод USB -> IKBD (ST) для Castaway (r1566)
 *
 * USB-слой отдаёт удерживаемые HID usage-коды (usb_kbd_get_raw), а
 * модификаторы — отдельным байтом (usb_kbd_get_mods). Ядро ждёт ST-сканкоды
 * (set 1, как у PC) в IkbdKeyPress/IkbdKeyRelease; пад/кнопочная плата идёт
 * в джойстик ST через IkbdJoystickChange.
 */
#include <stdint.h>
#include <string.h>

#include "st.h"          /* IkbdKeyPress / IkbdKeyRelease / IkbdJoystickChange */
#include "cw_input.h"
#include "../usb_kbd.h"
#include "../btn_pad.h"

/* HID usage (0x04..0x52) -> ST scancode (set 1); 0 = не отображаем.
 * r1601: буквы выверены по матрице ST (прежняя таблица сдвигала их подряд:
 * e→0x21 и т.д.), навигация — 0x47..0x53, F1..F10 — 0x3B..0x44. */
static const uint8_t g_hid2st[256] = {
    /* 0x04..0x1D: a..z */
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20,
    [0x08] = 0x12, [0x09] = 0x21, [0x0A] = 0x22, [0x0B] = 0x23,
    [0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26,
    [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19,
    [0x14] = 0x10, [0x15] = 0x13, [0x16] = 0x1F, [0x17] = 0x14,
    [0x18] = 0x16, [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
    [0x1C] = 0x15, [0x1D] = 0x2C,
    /* 0x1E..0x27: 1..0 */
    [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05,
    [0x22] = 0x06, [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09,
    [0x26] = 0x0A, [0x27] = 0x0B,
    /* 0x28..0x38: Enter Esc BS Tab Space - = [ ] \ ; ' ` , . / */
    [0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F,
    [0x2C] = 0x39, [0x2D] = 0x0C, [0x2E] = 0x0D, [0x2F] = 0x1A,
    [0x30] = 0x1B, [0x31] = 0x2B, [0x32] = 0x2B, [0x33] = 0x27,
    [0x34] = 0x28, [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34,
    [0x38] = 0x35,
    /* 0x39 CapsLock; 0x3A..0x43 F1..F10; F11 -> Help (ST 0x62).
     * F12 (HID 0x45) не отображаем: она занята hot-swap диска (disk_swap). */
    [0x39] = 0x3A, [0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D,
    [0x3D] = 0x3E, [0x3E] = 0x3F, [0x3F] = 0x40, [0x40] = 0x41,
    [0x41] = 0x42, [0x42] = 0x43, [0x43] = 0x44, [0x44] = 0x62,
    /* 0x49..0x52: Ins Home PgUp Del End PgDn Right Left Down Up */
    [0x49] = 0x52, [0x4A] = 0x47, [0x4B] = 0x49, [0x4C] = 0x53,
    [0x4D] = 0x4F, [0x4E] = 0x51, [0x4F] = 0x4D, [0x50] = 0x4B,
    [0x51] = 0x50, [0x52] = 0x48,
};

#define CW_KEYS 16

/* r1609: состояние сессии ввода. Сбрасывается в cw_input_init при каждом
 * входе в эмулятор — иначе второй запуск наследует «зажатые» клавиши,
 * состояние джойстика/модификаторов и позицию указателя прошлой игры. */
static uint8_t  g_prev[CW_KEYS];
static uint8_t  g_prev_mods;
static uint8_t  g_prev_joy;
static int      g_prev_start;
static int      g_prev_btn;
static uint16_t g_pad_state;
static unsigned g_pad_div;
static int32_t  g_acc_x, g_acc_y, g_sm_x, g_sm_y;
static int      g_last_x, g_last_y, g_have_last;
static int      g_cur_x, g_cur_y;

/* r1603: диагностика — сколько раз сдвинули указатель и последняя дельта. */
unsigned cw_in_motion;
int      cw_in_odx, cw_in_ody;
/* r1604: источник мыши найден и сколько опросов дали ненулевую дельту. */
unsigned cw_in_pad, cw_in_raw;

void cw_input_init(void)
{
    memset(g_prev, 0, sizeof(g_prev));
    g_prev_mods = 0;
    g_prev_joy = 0;
    g_prev_start = 0;
    g_prev_btn = 0;
    g_pad_state = 0;
    g_pad_div = 0;
    g_acc_x = g_acc_y = g_sm_x = g_sm_y = 0;
    g_last_x = g_last_y = g_have_last = 0;
    g_cur_x = g_cur_y = 0;
    cw_in_motion = 0;
    cw_in_odx = cw_in_ody = 0;
    cw_in_pad = cw_in_raw = 0;
}

void cw_input_poll(void)
{
    uint8_t now[CW_KEYS];

    /* r1585: наполнить счётчики дельт тачпада (I8 Pro boot mouse): их
     * отдаёт usb_mouse_take_delta только после опроса (uae4all_host.c:307). */
    usb_pad_poll();

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

    /* r1601: модификаторы приходят отдельным байтом отчёта, не в массиве
     * кодов. Биты HID: 0x01 LCTRL, 0x02 LSHIFT, 0x04 LALT, 0x10 RCTRL,
     * 0x20 RSHIFT, 0x40 RALT; GUI-клавиши на ST не отображаем. */
    {
        static const uint8_t mod_st[8] = {
            0x1D, 0x2A, 0x38, 0x00, 0x1D, 0x36, 0x38, 0x00
        };
        uint8_t m = usb_kbd_get_mods();
        uint8_t diff = (uint8_t)(m ^ g_prev_mods);

        for (int b = 0; b < 8; b++) {
            if (!mod_st[b] || !(diff & (1u << b)))
                continue;
            if (m & (1u << b))
                IkbdKeyPress(mod_st[b]);
            else
                IkbdKeyRelease(mod_st[b]);
        }
        g_prev_mods = m;
    }

    /* r1605: пад/плата + клавиатура -> джойстик ST0.
     * r1607: клавиатура — раскладка как в NES/Dendy (remap.c, def_nes):
     * стрелки, Z = A, X = B; у ST-джойстика один огонь, поэтому оба — fire.
     * Скан платы — раз в 4 вызова (50 Гц): это I2C/бит-банг. */
    {
        uint8_t js = 0;

        if ((g_pad_div++ & 3u) == 0)
            g_pad_state = pad_scan_combined();

        if (g_pad_state & 0x0001) js |= 0x01;     /* up */
        if (g_pad_state & 0x0002) js |= 0x02;     /* down */
        if (g_pad_state & 0x0004) js |= 0x04;     /* left */
        if (g_pad_state & 0x0008) js |= 0x08;     /* right */
        /* r1606: fire — любая кнопка действия пада: A/B/C (0x10/0x20/0x40)
         * и X/Y/Z (0x100/0x200/0x400): у ST-джойстика один огонь.
         * START (0x80) -> Space, MODE/Select (0x800) аналога на ST не имеет. */
        if (g_pad_state & 0x0770) js |= 0x10;

        /* START пада/платы -> ST Space по фронту (типовое start/pause). */
        {
            int start = (g_pad_state & 0x0080) != 0;

            if (start != g_prev_start) {
                if (start)
                    IkbdKeyPress(0x39);
                else
                    IkbdKeyRelease(0x39);
                g_prev_start = start;
            }
        }

        for (i = 0; i < n; i++) {
            switch (now[i]) {
            case 0x52: js |= 0x01; break;        /* Up */
            case 0x51: js |= 0x02; break;        /* Down */
            case 0x50: js |= 0x04; break;        /* Left */
            case 0x4F: js |= 0x08; break;        /* Right */
            case 0x1D: js |= 0x10; break;        /* Z = A -> fire */
            case 0x1B: js |= 0x10; break;        /* X = B -> fire */
            default: break;
            }
        }

        if (js != g_prev_joy) {
            IkbdJoystickChange(0, js);
            g_prev_joy = js;
        }
    }

    /* r1582: мышь/тачпад — сырые дельты трекпада со сглаживанием EMA и
     * масштабом 1/3; если относительного источника нет (тач на донгле
     * отдаёт абсолютную позицию) — дельта из usb_pad_get_pos, как в
     * Amiga-ядре uae4all_host.c. */
    {
        int rdx = 0, rdy = 0;

        if (usb_mouse_take_delta(&rdx, &rdy)) {
            if (rdx || rdy)
                cw_in_raw++;
            cw_in_pad = 1;
            g_sm_x = (g_sm_x * 3 + rdx * 16) / 4;
            g_sm_y = (g_sm_y * 3 + rdy * 16) / 4;
        } else {
            int mx, my;
            if (usb_pad_get_pos(&mx, &my)) {
                if (!g_have_last) {
                    g_last_x = mx;
                    g_last_y = my;
                    g_have_last = 1;
                }
                g_sm_x = (mx - g_last_x) * 8;   /* множитель 1/2 в 1/16-шкале */
                g_sm_y = (my - g_last_y) * 8;
                g_last_x = mx;
                g_last_y = my;
            } else {
                g_sm_x = g_sm_y = 0;
            }
        }
        g_acc_x += g_sm_x / 3;
        g_acc_y += g_sm_y / 3;

        int odx = (int)(g_acc_x >> 4), ody = (int)(g_acc_y >> 4);
        g_acc_x -= (int32_t)(odx << 4);
        g_acc_y -= (int32_t)(ody << 4);
        if (odx || ody) {
            cw_in_motion++;
            cw_in_odx = odx;
            cw_in_ody = ody;
            /* r1584: IkbdMouseMotion ждёт АБСОЛЮТНУЮ позицию (в IkbdMouse
             * dx = mouse.x - mouse.stx); передача дельты не накапливалась. */
            g_cur_x += odx;
            g_cur_y += ody;
            if (g_cur_x < 0) g_cur_x = 0;
            if (g_cur_y < 0) g_cur_y = 0;
            if (g_cur_x > 1023) g_cur_x = 1023;
            if (g_cur_y > 1023) g_cur_y = 1023;
            IkbdMouseMotion(g_cur_x, g_cur_y);
        }
    }

    /* Кнопки мыши: бит0=ЛКМ, бит1=ПКМ. IKBD: 2 — левая, 1 — правая
     * (r1605: было наоборот — ЛКМ/ПКМ перепутаны). */
    {
        int btn = usb_pad_buttons();
        for (int b = 0; b < 2; b++) {
            int now_b = (btn >> b) & 1;
            int was_b = (g_prev_btn >> b) & 1;
            if (now_b != was_b) {
                int code = b ? 1 : 2;   /* 0=ЛКМ -> 2, 1=ПКМ -> 1 */
                if (now_b)
                    IkbdMousePress(code);
                else
                    IkbdMouseRelease(code);
            }
        }
        g_prev_btn = btn;
    }
}
