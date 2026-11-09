#include <string.h>
#include <stdint.h>

#include "fb_text.h"
#include "rk86_video.h"
#include "rk86_machine.h"

#define FB_W 1024
#define FB_H 600

#define CH_W   6
#define CH_H   8
#define CELL_W (CH_W * 2)          // 12 пикселей HDMI на знакоместо
#define DST_W  (RK86_COLS * CELL_W)             // 936
#define DST_X  ((FB_W - DST_W) / 2)             // 44

#define COL_BG 0x00000000u
#define COL_FG 0x00E8E8E8u

// Атрибуты ВГ75 (байт атрибута, биты 0..5).
#define A_HIGHLIGHT 0x01
#define A_BLINK     0x02
#define A_GPA0      0x04
#define A_GPA1      0x08
#define A_REVERSE   0x10
#define A_UNDERLINE 0x20

// Маска псевдографического символа: TOP — верхняя половина вертикали,
// BOTTOM — нижняя, LEFT/RIGHT — горизонталь от центра к краю.
#define PG_LEFT   0x01
#define PG_RIGHT  0x02
#define PG_TOP    0x04
#define PG_BOTTOM 0x08
#define PG_VERT   (PG_TOP | PG_BOTTOM)

static const uint8_t pg_tab[16] = {
    PG_BOTTOM | PG_RIGHT,
    PG_BOTTOM | PG_LEFT,
    PG_TOP | PG_RIGHT,
    PG_TOP | PG_LEFT,
    PG_BOTTOM | PG_LEFT | PG_RIGHT,
    PG_VERT | PG_LEFT,
    PG_VERT | PG_RIGHT,
    PG_TOP | PG_LEFT | PG_RIGHT,
    PG_LEFT | PG_RIGHT,
    PG_VERT,
    PG_VERT | PG_LEFT | PG_RIGHT,
    0, 0, 0, 0, 0,
};

// Кэш последнего кадра: текст и атрибуты каждого знакоместа. Перерисовываем
// только изменившиеся ячейки — иначе каждый кадр приходилось заливать 2.4 МБ
// фреймбуфера и картинка мерцала на медленной записи в память.
static uint8_t s_txt[RK86_ROWS][RK86_COLS];
static uint8_t s_att[RK86_ROWS][RK86_COLS];
static int s_cache_ok;
static int s_cols, s_rows, s_ch, s_top;
static int s_blink;

// Разбор строки текстовой памяти ВГ75 (как next_line в vg75.c): управляющие
// байты 0x80..0xBF задают атрибуты/псевдографику, 0xF0..0xFF — конец строки
// и экрана. Возвращает число израсходованных байт.
static int decode_row(const uint8_t* p, int width, uint8_t* text, uint8_t* attr,
                      uint8_t* flags, int* end_screen, int blink_on)
{
    int o = 0, x = 0;
    uint8_t burst = rk86_screen.dma_burst ? rk86_screen.dma_burst : 1;

    if (*end_screen) goto done;

    for (int i = 0; i < width; i++) {
        uint8_t c = p[o++];

        if (!(c & 0x80)) {
            text[x] = ((!(*flags & A_BLINK)) || blink_on) ? c : 0;
            attr[x] = *flags;
            x++;
        } else if (c & 0x40) {
            if ((c & 0xFC) == 0xF0) {
                switch (c & 0x03) {
                case 0:                      // конец строки
                    o = width;
                    goto done;
                case 1:                      // конец строки / стоп ПДП
                    if (((o & (burst - 1)) == 0) || (o == width))
                        goto done;
                    o++;
                    goto done;
                default:                     // конец экрана
                    *end_screen = 1;
                    goto done;
                }
            } else {
                text[x] = ((!(c & A_BLINK)) || blink_on)
                              ? (uint8_t)(((c >> 2) & 0x0F) | 0x80) : 0;
                attr[x] = (uint8_t)((*flags & 0xFC) | (c & 0x03));
                x++;
            }
        } else {
            *flags = (uint8_t)(c & 0x3F);
            if (rk86_screen.attr_visible) {
                text[x] = 0;
                attr[x] = *flags;
                x++;
            }
        }
    }

done:
    while (x < 80) {
        text[x] = 0;
        attr[x] = 0;
        x++;
    }
    return o;
}

// 6 пикселей знакоместа для строки l; 1 — точка. Как SYM в vg75.c: страница
// знакогенератора выбирается битами GPA атрибута, строки за пределами 8x8
// остаются пустыми (реально у ВГ75 char_h=10, подчёркивание — 9-я строка).
static uint8_t glyph_bits(uint8_t c, uint8_t a, int l)
{
    uint8_t bits;
    int uy = rk86_screen.underline_y;

    if (c & 0x80) {
        uint8_t pg = pg_tab[c & 0x0F];
        if (l < uy) {
            bits = (pg & PG_TOP) ? 0x08 : 0x00;
        } else if (l > uy) {
            bits = (pg & PG_BOTTOM) ? 0x08 : 0x00;
        } else {
            bits = (uint8_t)(((pg & PG_LEFT) ? 0x38 : 0x00) |
                             ((pg & PG_RIGHT) ? 0x0F : 0x00) |
                             ((pg & PG_VERT) ? 0x08 : 0x00));
        }
    } else {
        const uint8_t* font = rk86_font_page((a >> 2) & 0x03);
        // Знакогенератор построчный: символ c, строка l -> font[l*128 + c].
        bits = (l < CH_H) ? font[(l << 7) + (c & 0x7F)] : 0;
        if ((a & A_UNDERLINE) && l == uy) bits |= 0x3F;
    }

    if (a & A_REVERSE) bits ^= 0x3F;
    return (uint8_t)(bits & 0x3F);
}

// Одна ячейка: 12 пикселей в ширину и ch строк по 2 пикселя.
static void cell_draw(uint32_t* fb, int r, int c, uint8_t sym, uint8_t a,
                      int ch, int top)
{
    uint32_t* d = fb + (size_t)(top + r * ch * 2) * FB_W + DST_X + c * CELL_W;

    for (int l = 0; l < ch; l++) {
        uint8_t bits = glyph_bits(sym, a, l);
        uint32_t* p0 = d + (size_t)(l * 2) * FB_W;
        uint32_t* p1 = p0 + FB_W;
        for (int b = 0; b < CH_W; b++) {
            uint32_t v = (bits & (0x20 >> b)) ? COL_FG : COL_BG;
            p0[b * 2] = v;
            p0[b * 2 + 1] = v;
            p1[b * 2] = v;
            p1[b * 2 + 1] = v;
        }
    }
}

void rk86_render(uint32_t* fb, int blink_phase)
{
    int cols = rk86_screen.screen_w;
    int rows = rk86_screen.screen_h;
    if (cols > RK86_COLS || cols <= 0) cols = RK86_COLS;
    if (rows > RK86_ROWS || rows <= 0) rows = RK86_ROWS;

    int ch = rk86_screen.char_h;
    if (ch < 1 || ch > 16) ch = CH_H;
    // Не даём сетке выйти за пределы кадра (30 строк × 2 × char_h <= 600).
    while (ch > 1 && rows * ch * 2 > FB_H) ch--;

    int top = (FB_H - rows * ch * 2) / 2;
    if (top < 0) top = 0;

    int blink_on = blink_phase & 16;
    int geom = (!s_cache_ok || cols != s_cols || rows != s_rows ||
                ch != s_ch || top != s_top);

    if (geom) {
        for (int i = 0; i < FB_W * FB_H; i++) fb[i] = COL_BG;
        memset(s_txt, 0xFF, sizeof(s_txt));
        memset(s_att, 0xFF, sizeof(s_att));
        s_cols = cols;
        s_rows = rows;
        s_ch = ch;
        s_top = top;
        s_cache_ok = 1;
    }

    int dx0 = FB_W, dy0 = FB_H, dx1 = 0, dy1 = 0;

    const uint8_t* p = rk86_screen.vram;
    uint8_t text[80], attr[80];
    uint8_t flags = 0;
    int end_screen = 0;

    for (int r = 0; r < rows; r++) {
        memset(text, 0, sizeof(text));
        memset(attr, 0, sizeof(attr));

        p += decode_row(p, cols, text, attr, &flags, &end_screen, blink_on);

        for (int c = 0; c < cols; c++) {
            uint8_t sym = text[c];
            uint8_t a = attr[c];

            // Курсор ВГ75 — инверсия или подчёркивание атрибута знакоместа.
            int is_cur = (r == rk86_screen.cursor_y &&
                          c == rk86_screen.cursor_x);
            if (is_cur && (blink_on || (rk86_screen.cursor_type & 0x02))) {
                a ^= (rk86_screen.cursor_type & 0x01) ? A_UNDERLINE : A_REVERSE;
            }

            int need = (sym != s_txt[r][c]) || (a != s_att[r][c]);
            // Мигание: при смене фазы перерисовываем только мигающие ячейки.
            if (!need && blink_on != s_blink && (a & A_BLINK)) need = 1;
            if (!need && blink_on != s_blink && is_cur) need = 1;
            if (!need) continue;

            cell_draw(fb, r, c, sym, a, ch, top);
            s_txt[r][c] = sym;
            s_att[r][c] = a;

            int x = DST_X + c * CELL_W;
            int y = top + r * ch * 2;
            if (x < dx0) dx0 = x;
            if (y < dy0) dy0 = y;
            if (x + CELL_W > dx1) dx1 = x + CELL_W;
            if (y + ch * 2 > dy1) dy1 = y + ch * 2;
        }
    }

    s_blink = blink_on;

    if (dx1 > dx0 && dy1 > dy0) {
        fb_mark_dirty(dx0, dy0, dx1 - dx0, dy1 - dy0);
    } else {
        // Кадр не изменился: отмечаем минимум, чтобы fb_flush не чистил
        // весь фреймбуфер (dirty-область всегда узкая).
        fb_mark_dirty(0, 0, 1, 1);
    }
    fb_frame_scaled();
}
