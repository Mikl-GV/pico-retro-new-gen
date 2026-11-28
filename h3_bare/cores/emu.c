#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "emu.h"
#include "fb_text.h"
#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "h3_hs_timer.h"
#include "led.h"
#include "settings.h"
#include "i2s.h"
#include "emulog.h"

extern int printf(const char* fmt, ...);

// Порог вывода логов ядер (значения enum retro_log_level: 0=DEBUG, 1=INFO,
// 2=WARN, 3=ERROR). Общий для всех host-слоёв: 2 — релизный режим (только
// предупреждения/ошибки). Почему не INFO по умолчанию: UART блокирующий, и
// INFO-поток VICE (Set resource, Arg0 …) тормозил эмуляцию (r987). Для
// отладки конкретного ядра host поднимает порог до 1 на время запуска.
int g_core_log_min = 2;

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

#define FB_ADDR 0x5F900000
#define FB_W    1024
#define FB_H    600

static uint32_t g_border_color = 0;   // цвет полей (XRGB8888), 0 = чёрный

void emu_set_border_color(uint32_t rgb888) {
    g_border_color = rgb888;
}

// r703 (C3): double-buffer HDMI для direct-писателей (BK/Vectrex).
// ВЫКЛЮЧЕН по умолчанию. DE2 UI-канал запущен с единственным TOP_LADDR=FB_ADDR;
// настоящий vsync-free flip двух буферов без аппаратной проверки опасен (риск
// чёрного/рваного кадра на стенде), поэтому активный dbl-buf закрыт за макросом.
// Функция остаётся точкой интеграции: при включении bk/vecx пишут в back buffer,
// а emu_hdmi_flip() меняет H3_DE2_MUX0_UI->CFG[0].BOT_LADDR (см. h3_de2.h).
#define CONFIG_HDMI_DOUBLE_BUF 0   // 0 = выкл. (безопасно), 1 = вкл. (только со стендом)
// P0 (r725): второй FB для direct-писателей. Прежний 0x5FB40000 пересекался со
// стеками CPU2: FB 1024×600×4 = 0x25C000 → занято 0x5FB40000..0x5FD9C000, а SVC-
// стек CPU2 живёт на 0x5FC01000 (вниз) с банками исключений 0x5FBFD000..0x5FC00000.
// Новый адрес 0x5F400000: конец буфера 0x5F660000 — ниже EMU_FB (0x5F800000),
// ниже HDMI FB (0x5F900000+2.46МБ) и ниже стеков CPU2/CPU1 — пересечений нет.
#define FB_ADDR_BACK 0x5F400000u   // второй FB для direct-писателей (до EMU_FB)

// r1538: режим двойной буферизации снят с игрового DE2-пути (хосты рисуют
// в EMU_FB напрямую). r1647: пункт вернули — fb_flip_set(1) переключает
// программный FB (fb_draw_addr/fb_flush) и убирает разрыв кадра в меню и
// системах, рисующих через emu.c; хосты прямого вывода сами зовут
// fb_flip_set(0).
int g_video_vsync = 0;

void emu_hdmi_flip(void) {
#if CONFIG_HDMI_DOUBLE_BUF
    // Пометка для документации: здесь переключение адреса DE2 UI-канала.
    // Без правильного vsync-гейта (статус TCON1/DE2) flip может показать
    // пустой буфер — проверяется только на живом железе.
    extern void h3_de2_set_ui_addr(uint32_t addr);
    h3_de2_set_ui_addr(FB_ADDR_BACK);
#else
    (void)FB_ADDR_BACK;
#endif
}

// ---- единый nearest-neighbour скейлер ----
// Оптимизация: без делений в пиксельном цикле. Таблица sx[] (индекс исходной
// колонки для каждой целевой) считается один раз — на Cortex-A7 деление ~12-20
// тактов, а при 500К+ пикселей это съедало 5-10 мс/кадр.
// Общий скейлер: RGB565-источник (произвольный шаг строки) -> XRGB8888 в HDMI
// FB. dst_w/dst_h/dst_x считает вызывающий. fill_borders=1 — поля по бокам
// заливаются border (нужно, когда кадр приходит не в 320-пиксельный EMU_FB).
static void scale_rgb565_to_fb(const uint16_t* src, int sw, int sh, int stride,
                               int dst_w, int dst_h, int dst_x,
                               uint32_t border, int fill_borders)
{
    uint32_t* dst = (uint32_t*)(uintptr_t)fb_draw_addr();

    // Предрасчёт исходной колонки для каждой целевой (nearest neighbour):
    // аккумулятор 16.16 — каждая итерация добавляет sw/dst_w, обёртка
    // по sw (амплитуда = sw, шаг = sw/dst_w).
    static int sx_tab[FB_W];
    uint32_t step_x = ((uint32_t)sw << 16) / (uint32_t)dst_w;
    uint32_t acc_x = step_x >> 1;   // округление к ближайшему
    for (int dx = 0; dx < dst_w; dx++) {
        sx_tab[dx] = (int)(acc_x >> 16);
        acc_x += step_x;
        if (acc_x >= ((uint32_t)sw << 16)) acc_x -= (uint32_t)sw << 16;
    }

    // боковые поля
    if (fill_borders && dst_x > 0) {
        for (int dy = 0; dy < dst_h; dy++) {
            uint32_t* row = dst + (uint32_t)dy * FB_W;
            for (int dx = 0; dx < dst_x; dx++) row[dx] = border;
            for (int dx = dst_x + dst_w; dx < FB_W; dx++) row[dx] = border;
        }
    }

    // Построковый проход: исходная строка = аккумулятор 16.16 => без `/`
    uint32_t step_y = ((uint32_t)sh << 16) / (uint32_t)dst_h;
    uint32_t y_acc = step_y >> 1;              // округление к ближайшей строке
    int sy = 0;
    for (int dy = 0; dy < dst_h; dy++) {
        const uint16_t* s = src + (size_t)sy * stride;
        uint32_t* d = dst + (uint32_t)dy * FB_W + (uint32_t)dst_x;
        for (int dx = 0; dx < dst_w; dx++) {
            uint16_t p = s[sx_tab[dx]];
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5) & 0x3F) << 2;
            uint32_t b = (p & 0x1F) << 3;
            d[dx] = (r << 16) | (g << 8) | b;
        }
        y_acc += step_y;
        int nsy = (int)(y_acc >> 16);
        if (nsy > sy) { if (nsy >= sh) nsy = sh - 1; sy = nsy; }
    }
}

// r1401/r1403: прямой вывод RGBA-кадра (0x00RRGGBB) в HDMI FB.
// Два режима, чтобы ][+-кадр оставался хрустящим, а 80-колоночный //e-кадр
// (560×192) не был «маркой в углу»:
//   * целый множитель, если он занимает хотя бы 3/4 ширины экрана
//     (280×192 → ×3 = 840×576, пиксель копируется mul раз, дробных шагов нет);
//   * иначе — 4:3 на всю высоту (560×192 → 800×600) с nearest-шагом.
// Раньше 560×192 шёл ×1 (560×192 из 1024×600) — изображение было мелким.
// r1468: кадр КАК ЕСТЬ, 1:1, без масштаба (PXT 640×480 в центр 1024×600).
// Отличие от emu_show_rgba_int: нет ни множителя, ни шага — построчное копирование,
// поля — бордюр магента, чтобы растр и границы кадра были видны.
void emu_show_rgba_1to1(const uint32_t* src, int sw, int sh, int stride_px,
                        int dx, int dy) {
    if (!src || sw <= 0 || sh <= 0 || stride_px < sw) return;
    if (dx < 0 || dy < 0 || dx + sw > FB_W || dy + sh > FB_H) return;

    const uint32_t border = 0xFFFF00FFu;   // магента: бордюр вокруг кадра
    fb_dirty_all();
    fb_frame_scaled();

    uint32_t* dst = (uint32_t*)(uintptr_t)fb_draw_addr();
    const size_t row_bytes = (size_t)sw * 4u;

    for (int y = 0; y < FB_H; y++) {
        uint32_t* row = dst + (size_t)y * FB_W;
        if (y < dy || y >= dy + sh) {
            for (int x = 0; x < FB_W; x++) row[x] = border;
            continue;
        }
        for (int x = 0; x < dx; x++) row[x] = border;
        memcpy(row + dx, src + (size_t)(y - dy) * (size_t)stride_px, row_bytes);
        for (int x = dx + sw; x < FB_W; x++) row[x] = border;
    }
}

// r1469: то же «как есть» 1:1, но источник — RGB565 (МС1504: 640×400).
void emu_show_rgb565_1to1(const uint16_t* src, int sw, int sh, int stride_px,
                          int dx, int dy) {
    if (!src || sw <= 0 || sh <= 0 || stride_px < sw) return;
    if (dx < 0 || dy < 0 || dx + sw > FB_W || dy + sh > FB_H) return;

    const uint32_t border = 0xFFFF00FFu;   // магента: бордюр вокруг кадра
    fb_dirty_all();
    fb_frame_scaled();

    uint32_t* dst = (uint32_t*)(uintptr_t)fb_draw_addr();

    for (int y = 0; y < FB_H; y++) {
        uint32_t* row = dst + (size_t)y * FB_W;
        if (y < dy || y >= dy + sh) {
            for (int x = 0; x < FB_W; x++) row[x] = border;
            continue;
        }
        for (int x = 0; x < dx; x++) row[x] = border;
        const uint16_t* s = src + (size_t)(y - dy) * (size_t)stride_px;
        uint32_t* d = row + dx;
        for (int x = 0; x < sw; x++) {
            uint16_t p = s[x];
            uint32_t r = (uint32_t)((p >> 11) & 0x1F) << 3;
            uint32_t g = (uint32_t)((p >> 5) & 0x3F) << 2;
            uint32_t b = (uint32_t)(p & 0x1F) << 3;
            d[x] = (r << 16) | (g << 8) | b;
        }
        for (int x = dx + sw; x < FB_W; x++) row[x] = border;
    }
}

void emu_show_rgba_int(const uint32_t* src, int sw, int sh, int stride_px) {
    if (!src || sw <= 0 || sh <= 0 || stride_px < sw) return;
    if (sw > FB_W || sh > FB_H) return;   // не наш случай: кадр больше экрана

    int mul = FB_W / sw;
    int mh  = FB_H / sh;
    if (mh < mul) mul = mh;

    int dst_w, dst_h;
    if (mul >= 1 && sw * mul >= (FB_W * 3) / 4) {
        dst_w = sw * mul;
        dst_h = sh * mul;
    } else {
        // Apple II: и 280×192, и 560×192 — один и тот же экран 4:3.
        dst_h = FB_H;
        dst_w = (FB_H * 4) / 3;
        if (dst_w > FB_W) { dst_w = FB_W; dst_h = (FB_W * 3) / 4; }
    }
    const int dst_x = (FB_W - dst_w) / 2;
    const int dst_y = (FB_H - dst_h) / 2;

    fb_dirty_all();
    fb_frame_scaled();

    uint32_t* dst = (uint32_t*)(uintptr_t)fb_draw_addr();
    const uint32_t border = g_border_color ? g_border_color : 0x00000000u;
    const uint32_t step_x = ((uint32_t)sw << 16) / (uint32_t)dst_w;
    const uint32_t step_y = ((uint32_t)sh << 16) / (uint32_t)dst_h;

    for (int y = 0; y < FB_H; y++) {
        uint32_t* row = dst + (size_t)y * FB_W;
        if (y < dst_y || y >= dst_y + dst_h) {
            for (int x = 0; x < FB_W; x++) row[x] = border;
            continue;
        }
        int sy = (int)(((uint32_t)(y - dst_y) * step_y) >> 16);
        if (sy >= sh) sy = sh - 1;
        const uint32_t* s = src + (size_t)sy * (size_t)stride_px;
        for (int x = 0; x < dst_x; x++) row[x] = border;
        uint32_t* d = row + dst_x;
        uint32_t acc = step_x >> 1;
        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (int)(acc >> 16);
            if (sx >= sw) sx = sw - 1;
            d[dx] = s[sx];
            acc += step_x;
            if (acc >= ((uint32_t)sw << 16)) acc -= (uint32_t)sw << 16;
        }
        for (int x = dst_x + dst_w; x < FB_W; x++) row[x] = border;
    }
}

// r1236: прямой вывод кадра «пиксель в пиксель». Amiga отдаёт 720×574, это
// влезает в HDMI 1024×600 без масштабирования: раньше кадр резался до 640×512
// и растягивался на 750×600 — по горизонтали пропадали края растра.
// Кадр больше экрана — уходим на обычный масштаб с полями.
/* r1271 (CORE-04): геометрия нативного вывода не должна переживать выход из
   ядра — иначе первый кадр меню после смены разрешения не перерисовывается. */
static int g_native_last_w = 0, g_native_last_h = 0;

void emu_native_geom_reset(void)
{
    g_native_last_w = 0;
    g_native_last_h = 0;
}

void emu_show_rgb565_native(const uint16_t* src, int w, int h, int stride_px)
{
    if (!src || w <= 0 || h <= 0 || stride_px < w) return;
    if (w > FB_W || h > FB_H) { emu_scale_rgb565_src(src, w, h, stride_px); return; }

    int dst_x = (FB_W - w) / 2;
    int dst_y = (FB_H - h) / 2;
    uint32_t* dst = (uint32_t*)(uintptr_t)fb_draw_addr();
    uint32_t border = g_border_color ? g_border_color : 0x00000000u;

    // Поля заливаем один раз на смену геометрии: кадр того же размера их не
    // трогает, а при уменьшении старые пиксели иначе остались бы на экране.
    // r1277 (E-01): при пинг-понге кадр уходит в ДРУГОЙ буфер — поля надо
    // залить и в нём, иначе оптимизация «геометрия не менялась» оставит там
    // мусор. При выключенной опции поведение прежнее.
    if (g_native_last_w != w || g_native_last_h != h || fb_flip_active()) {
        for (int y = 0; y < FB_H; y++) {
            uint32_t* row = dst + (uint32_t)y * FB_W;
            for (int x = 0; x < FB_W; x++) row[x] = border;
        }
        g_native_last_w = w; g_native_last_h = h;
    }

    for (int y = 0; y < h; y++) {
        const uint16_t* s = src + (size_t)y * stride_px;
        uint32_t* d = dst + (uint32_t)(dst_y + y) * FB_W + (uint32_t)dst_x;
        for (int x = 0; x < w; x++) {
            uint32_t p = s[x];
            d[x] = (((p >> 11) & 0x1Fu) << 19) | (((p >> 5) & 0x3Fu) << 10) |
                   ((p & 0x1Fu) << 3);
        }
    }
    fb_mark_dirty(0, 0, FB_W, FB_H);
    fb_frame_scaled();
}

void emu_scale(int src_w, int src_h) {
    if (src_w <= 0 || src_h <= 0) return;
    // r0.417 (H3 аудита): буфер EMU_FB фиксирован 320×240 — клампим входные
    // размеры, иначе hi-res режимы (SNES 512 колонок) читают за его конец.
    if (src_w > EMU_W) src_w = EMU_W;
    if (src_h > EMU_H) src_h = EMU_H;
    int dst_w = (src_w * FB_H) / src_h;
    if (dst_w > FB_W) dst_w = FB_W;
    if (dst_w <= 0) return;
    int dst_h = FB_H;
    int dst_x = (FB_W - dst_w) / 2;
    // r1159 (E14): отметить область кадра для fb_flush (dirty-зоны)
    if (g_border_color) fb_mark_dirty(0, 0, FB_W, dst_h);
    else                fb_mark_dirty(dst_x, 0, dst_w, dst_h);
    fb_frame_scaled();
    scale_rgb565_to_fb(EMU_FB, src_w, src_h, EMU_W, dst_w, dst_h, dst_x,
                       g_border_color, g_border_color ? 1 : 0);
}

// Скейл «сырого» RGB565-кадра произвольного размера прямо в HDMI FB.
// Нужен ядрам, которые отдают кадр не в EMU_FB (кадры больше 1024×600 или
// нестандартной геометрии), с сохранением пропорций: dst_w = sw*FB_H/sh,
// ограничение FB_W, поля по бокам — g_border_color или чёрный.
void emu_scale_rgb565_src(const uint16_t* src, int sw, int sh, int stride_px) {
    if (!src || sw <= 0 || sh <= 0 || stride_px < sw) return;
    int cx = 0, cy = 0;
    if (sw > 640 && sh > 512) {
        cx = (sw - 640) / 2;
        cy = (sh - 512) / 2;
        sw = 640;
        sh = 512;
    }
    int dst_h = FB_H;
    int dst_w = (int)(((int64_t)sw * FB_H) / sh);
    if (dst_w > FB_W) dst_w = FB_W;
    if (dst_w <= 0) return;
    int dst_x = (FB_W - dst_w) / 2;
    fb_mark_dirty(0, 0, FB_W, dst_h);
    fb_frame_scaled();
    scale_rgb565_to_fb(src + (size_t)cy * stride_px + cx, sw, sh, stride_px,
                       dst_w, dst_h, dst_x,
                       g_border_color ? g_border_color : 0x00000000u, 1);
}

// r1252: скейл с заданным аспектом кадра. Amiga PAL отдаёт 320x256, но это
// 4:3 при неквадратном пикселе; пропорциональный скейл по источнику даёт 1.25
// и текст выглядит сжатым по горизонтали. Здесь ширину задаёт аспект (num/den),
// высота — всегда FB_H.
void emu_scale_rgb565_src_aspect(const uint16_t* src, int sw, int sh,
                                 int stride_px, int num, int den) {
    if (!src || sw <= 0 || sh <= 0 || stride_px < sw || num <= 0 || den <= 0) return;
    int dst_h = FB_H;
    int dst_w = (int)(((int64_t)dst_h * num) / den);
    if (dst_w > FB_W) dst_w = FB_W;
    if (dst_w <= 0) return;
    int dst_x = (FB_W - dst_w) / 2;
    fb_mark_dirty(0, 0, FB_W, dst_h);
    fb_frame_scaled();
    scale_rgb565_to_fb(src, sw, sh, stride_px, dst_w, dst_h, dst_x,
                       g_border_color ? g_border_color : 0x00000000u, 1);
}

// ---- integer scale (целочисленный множитель) ----
// Портативные системы и Sega 32X (r1170): один пиксель исходника = N×N пикселей
// экрана, поля по периметру. Множитель — максимальный целый, влезающий в 1024×600.
// Формат на выходе — RGB565 → XRGB8888 в FB_ADDR (как emu_scale).
// r1170: геометрия последнего отскейленного кадра. При неизменном размере и
// цвете полей бордюры не перезаливаются каждый кадр — fb_flush чистит D-cache
// только по области картинки (в 2 раза меньше записи/clean, картинка та же).
static int s_dw = -1, s_dh = -1, s_dx = -1, s_dy = -1;
static uint32_t s_border = 0;
static int s_geom_valid = 0;

// Сброс кэша геометрии: экран очищен/перерисован целиком (emu_prepare, выход
// из эмулятора) — бордюры надо залить заново.
void emu_scale_int_invalidate(void) {
    s_geom_valid = 0;
}

void emu_scale_int(int src_w, int src_h) {
    if (src_w <= 0 || src_h <= 0) return;   // r0.198: защита от div-by-0 (как в emu_scale)
    // r0.417 (H3): те же клампы, что в emu_scale — buffer EMU_FB 320×240
    if (src_w > EMU_W) src_w = EMU_W;
    if (src_h > EMU_H) src_h = EMU_H;
    // Выбираем множитель: min(FB_W/src_w, FB_H/src_h), целый
    int mul = FB_W / src_w;
    int mh  = FB_H / src_h;
    if (mh < mul) mul = mh;
    if (mul < 1) { emu_scale(src_w, src_h); return; }  // падаем на stretch

    int dst_w = src_w * mul;
    int dst_h = src_h * mul;
    int dst_x = (FB_W - dst_w) / 2;
    int dst_y = (FB_H - dst_h) / 2;
    // r1170: поля перерисовываем только при смене геометрии/цвета — иначе
    // кадр занимает свой прямоугольник, бордюры на экране уже стоят.
    int repaint_border = fb_flip_active() || (g_border_color &&
        (!s_geom_valid || dst_w != s_dw || dst_h != s_dh ||
         dst_x != s_dx || dst_y != s_dy || g_border_color != s_border));
    // r1159 (E14): отметить область кадра для fb_flush (dirty-зоны)
    if (repaint_border) fb_dirty_all();
    else                fb_mark_dirty(dst_x, dst_y, dst_w, dst_h);
    fb_frame_scaled();
    uint32_t* dst = (uint32_t*)(uintptr_t)fb_draw_addr();

    // Поля (сверху/снизу/слева/справа)
    if (repaint_border) {
        // верх
        for (int y = 0; y < dst_y; y++)
            for (int x = 0; x < FB_W; x++)
                dst[y * FB_W + x] = g_border_color;
        // низ
        for (int y = dst_y + dst_h; y < FB_H; y++)
            for (int x = 0; x < FB_W; x++)
                dst[y * FB_W + x] = g_border_color;
        // левая/правая полоса (в зоне картинки)
        for (int y = dst_y; y < dst_y + dst_h; y++) {
            for (int x = 0; x < dst_x; x++)
                dst[y * FB_W + x] = g_border_color;
            for (int x = dst_x + dst_w; x < FB_W; x++)
                dst[y * FB_W + x] = g_border_color;
        }
    }

    // Масштабирование: каждый исходный пиксель — блок mul×mul
    for (int sy = 0; sy < src_h; sy++) {
        const uint16_t* src = EMU_FB + sy * EMU_W;
        int dy0 = dst_y + sy * mul;
        for (int sx = 0; sx < src_w; sx++) {
            uint16_t p = src[sx];
            uint32_t c = (((p >> 11) & 0x1F) << 3) << 16
                       | (((p >> 5) & 0x3F) << 2) << 8
                       | ((p & 0x1F) << 3);
            int dx0 = dst_x + sx * mul;
            for (int dy = 0; dy < mul; dy++)
                for (int dx = 0; dx < mul; dx++)
                    dst[(dy0 + dy) * FB_W + dx0 + dx] = c;
        }
    }
    // r1170: запоминаем геометрию — следующий кадр красит только картинку
    s_dw = dst_w; s_dh = dst_h; s_dx = dst_x; s_dy = dst_y;
    s_border = g_border_color;
    s_geom_valid = 1;
}

void emu_clear_fb(void) {
    memset((void*)EMU_FB, 0, EMU_W * EMU_H * 2);
}

// r0.256: единая подготовка перед запуском ЛЮБОГО эмулятора.
//  - gb_heap_reset(): bump-пул ядровых malloc — при повторном входе без сброса
//    память эмулятора остаётся от предыдущего запуска (и растёт);
//  - EMU_FB + HDMI-кадр: у систем, чей рендер НЕ покрывает весь EMU_FB
//    (напр. Fuse пишет только 256/320 колонок, а emu_scale читает все 320),
//    на экране оставались куски «загруженного до этого».
extern void gb_heap_reset(void);
extern void newlib_heap_reset(void);
extern void cheats_reset(void);

// r786: Atari 800 (ядро atari800) КЭШИРУЕТ указатели на bump-пул между
// запусками (Screen_atari, atarixe_memory, POKEYSND_process_buffer, FILTER_*,
// ...) и на повторном входе продолжает их использовать. gb_heap_reset()
// обнуляет позицию пула → старые указатели указывают в перезаписанную чужую
// память → чёрный экран/мусор при перезаходе (лечилось только перезагрузкой).
// Хост A8 ставит флаг до emu_prepare: пул для A8 НЕ сбрасывается (растёт
// медленно, лимит 24 МБ; остальные системы сбрасывают при своём emu_prepare).
int g_emu_keep_heap = 0;
// r1241: PUAE держит ядро инициализированным между входами, поэтому _sbrk
// нельзя откатывать на старт — иначе следующая сессия получит память,
// которую держит ядро.
int g_emu_keep_sbrk = 0;
// r1241: сколько раз вызывался emu_prepare(). PUAE сравнивает своё значение с
// текущим: если между его запусками готовился другой эмулятор, тот сбросил
// _sbrk, и «тёплый» вход уже нельзя — нужен холодный (unload + deinit + init).
unsigned g_emu_prepare_gen = 0;

void emu_prepare(void) {
    g_emu_prepare_gen++;
    if (!g_emu_keep_heap) gb_heap_reset();
    if (!g_emu_keep_sbrk)
        newlib_heap_reset();   // r0.417 (H2): newlib-куча (_sbrk) не растёт между запусками игр
    // r0.417 (M14/M15): читы ОТКЛЮЧЕНЫ до послойной доработки — список всегда
    // пуст на входе в любой эмулятор (включая builtin; rom_browser сбрасывал
    // только свои пути). Пустой список = применение в host-слоях — no-op.
    cheats_reset();
    emu_period_us = 16667;   // r622: сброс периода в дефолт — иначе «протечка»
                             // между системами (напр. NGP ставил 16200, а выход
                             // возвращал saved_period от прошлого, не 16667).
    // r1174: база/долг throttle — состояние emu.c, сбрасываем в единой точке
    // входа. Раньше это делал каждый host сам; вход в новый эмулятор не должен
    // наследовать emu_ts0/emu_debt прошлой сессии («владелец времени один»).
    emu_throttle_reset();
    // r908/r909: звук вывода возвращён (r895+) — сбрасываем индексы ring на
    // входе, чтобы хвост прошлой игры не «доезжал» в новый запуск (TX FIFO не
    // трогаем: его посреди потока FLUSH_TX клинит).
    i2s_ring_reset();
    // r910 (Фаза 0): включаем замер только на время эмуляции + чистим счётчики.
    i2s_stats_active(1);
    i2s_stats_reset();
    // r916: флаг «идёт эмуляция» для CPU2 (долив тишины в тихие места).
    i2s_audio_set_active(1);
    // r1141: DC-блокер CPU2 глобален для CPU2 (.coherent). Сбрасываем bypass на
    // входе в ЛЮБОЙ эмулятор (emu_prepare вызывается до host-слоёв) — иначе
    // включённый в GBA/MD bypass «протёк» бы в остальные системы (NES/SNES/Lynx).
    // Хосты GBA/MD включают i2s_dc_bypass(1) уже ПОСЛЕ этой точки (в *_init_game).
    i2s_dc_bypass(0);
    // r1410: сводка инициализации железа/звука перед входом в эмулятор.
    // Печатается раз на сессию: период кадра (после сброса в дефолт),
    // DC-ФВЧ (сдвиг + bypass) и ёмкость/заполнение кольца I2S (SRAM A1).
    if (g_emulog)
        printf("HW: period=%u us  i2s dc_shift=%d bypass=%d ring=%u pairs fill=%u\n",
               (unsigned)emu_period_us, i2s_dc_shift(), i2s_dc_is_bypassed(),
               (unsigned)AUDIO_RING_SIZE, (unsigned)i2s_ring_fill());
    // r1716: фактические ACTLR/SCTLR обоих ядер — проверка гипотезы
    // «ACTLR.SMP=0 глушит L1». Печатается здесь, где UART уже работает.
    if (g_emulog) {
        uint32_t a0, s0;
        __asm volatile("mrc p15, 0, %0, c1, c0, 1" : "=r"(a0));
        __asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(s0));
        printf("CPU: actlr0=%08X sctlr0=%08X actlr1=%08X sctlr1=%08X\n",
               (unsigned)a0, (unsigned)s0,
               (unsigned)(*(volatile uint32_t*)0x9070u),
               (unsigned)(*(volatile uint32_t*)0x904Cu));
    }
    emu_clear_fb();
    fb_clear();
    fb_flush();
    // r1170: экран очищен — бордюры прошлой сессии стёрты, кэш геометрии
    // целочисленного скейла недействителен.
    emu_scale_int_invalidate();
    emu_native_geom_reset();   // r1271 (CORE-04): геометрия кадра прошлой сессии не наследуется
    btn_pad_debounce_reset();  // r1272 (RT-14): дебаунсер кнопок не наследует состояние игры
    // r1647: режим применяется на входе в эмулятор (как в r1183); хосты
    // прямого вывода сами сбрасывают его через fb_flip_set(0).
    fb_flip_set(g_video_vsync);
}

// r910 (Фаза 0): единая точка завершения звукового замера — печатаем итог
// сессии (водяные знаки кольца, дропы/недоливы, всего пар) и снимаем активность,
// чтобы простой в меню не засорял статистику.
// r939: не static — вызывается из host-слоёв (snes/gpgx/fceumm) на выходе.
void emu_audio_stop(void) {
  emu_log_flush_all();   // r1747: добить буфер лога до AUDIO-END
  uint32_t fmax = 0, fmin = 0, drop = 0, under = 0, pairs = 0;
    i2s_stats_get(&fmax, &fmin, &drop, &under, &pairs);
    // r921: добавляем фактические счётчики долива CPU2 (fw=выведено пар в FIFO,
    // skip=раз «FIFO полон») — по ним видно, растёт ли долив при живом beat.
    uint32_t fw = 0, skip = 0, fl = 0, fe = 0;
    i2s_cpu2_diag(&fl, &fw, &skip, NULL);
    (void)fe;
    // r932: размер кольца в логе — фактический размер кольца i2s.c.
    if (i2s_audio_diag())
        printf("AUDIO-END: fill=%u..%u/%u drop=%u under=%u pairs=%u fw=%u skip=%u\n",
               (unsigned)fmin, (unsigned)fmax, (unsigned)AUDIO_RING_SIZE,
               (unsigned)drop, (unsigned)under, (unsigned)pairs,
               (unsigned)fw, (unsigned)skip);
    i2s_stats_active(0);
    i2s_audio_set_active(0);   /* r916: звук из игры ушёл — CPU2 тишину больше не доливает */
    i2s_ring_reset();
    // r1183: сессия закончилась — вернуть DE2 на штатный буфер, иначе меню
    // нарисуется в back-буфер и не будет видно.
    fb_flip_set(0);
}

// r777: звуковой слой вывода удалён — snd_manifest оставлен no-op,
// т.к. host-слои эмуляторов продолжают его звать (их не трогаем).
void snd_manifest(const char* sys, const char* cores) {
    (void)sys; (void)cores;
}

// ---- throttle ----
// C5: throttle от РЕАЛЬНОГО времени, с накоплением «долга» переработки.
// Раньше (r585): emu_ts0 переустанавливался на now каждый кадр — если кадр
// эмуляции был дольше периода (тяжёлая сцена), переработка ТЕРЯЛАСЬ: видео
// «плавало» по реальному времени, а звук (ведомый железным 48 кГц FIFO на
// CPU2) не догонялся → накапливался рассинхрон видео↔звук и «хвосты».
// C5: база сдвигается на +period от СТАРОЙ базы (а не на now), а всё, что
// ушло на эмуляцию сверх периода, засчитывается в emu_debt. Следующий кадр
// ждёт период - debt: мелкая переработка догоняется, дрейф не копится.
static uint32_t emu_ts0 = 0;
static uint32_t emu_last_pairs = 0;   // r1271 (CORE-03): дельта пар прошлой сессии      // следующая «распланированная» граница кадра
static int32_t  emu_debt = 0;     // переработка [мкс], вычтем из следующего wait
static uint32_t emu_period_frac = 0;    // r1350: дробная часть периода (1/65536 мкс)
static uint32_t emu_period_phase = 0;   // r1350: накопитель дробной части

// r989: setter периода кадра — системы с PAL-частотой (C64/C128 50 Гц) ставят
// свой период до цикла кадров, чтобы throttle не «догонял» 60 Гц и не копил
// переработку (emu_debt рос, кадры гнались без пауз → CPU под 100%).
// r1346 (Ш2.4): допуск до 100000 не помещался в uint16_t (обрезался молча,
// например 100000 → 34464). Значение теперь целиком в uint16_t-поле только
// если влезает; иначе клампим к 65535, чтобы период не «прыгал».
void emu_set_period_us(uint32_t us) {
    if (us < 1000u || us > 100000u) return;
    if (us > 65535u) us = 65535u;
    emu_period_us = (uint16_t)us;
    emu_period_frac = 0;   // r1350: целый период — дробной части нет
}

// r1350: период в 16.16 мкс (мкс<<16). Дробная часть копится в throttle
// (emu_period_phase) и раз в несколько кадров добавляет 1 мкс, поэтому СРЕДНИЙ
// период совпадает с реальной частотой кадра (MD/SMS: 16688.44/20120.13 мкс).
// Иначе округление до 16688 даёт ~1.15 пары/с утечки в кольцо I2S — «со
// временем звук тянется» даже после правки целого периода.
void emu_set_period_us_ex(uint32_t us_q16) {
    uint32_t us = us_q16 >> 16;
    if (us < 1000u || us > 100000u) return;
    if (us > 65535u) { us = 65535u; us_q16 = us << 16; }
    emu_period_us = (uint16_t)us;
    emu_period_frac = us_q16 & 0xFFFFu;
    emu_period_phase = 0;
}

// r703 (C4): liveness CPU2. g_audio_beat инкрементится CPU2 в цикле — если он
// перестал расти, аудио-ядро зависло. Мониторинг — инструмент диагностики,
// НЕ включает долив core0 (единственный писатель TX FIFO — CPU2, r739).
// r1747: лог горячего цикла. printf в кадре блокирует UART на миллисекунды;
// копим строки в RAM и сливаем в простое throttle (там же вычитаем budget).
#define EMU_LOG_BUF_SIZE 4096
static char     s_emu_log[EMU_LOG_BUF_SIZE];
static uint32_t s_emu_log_head = 0;
static uint32_t s_emu_log_tail = 0;

void emu_log_printf(const char* fmt, ...) {
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;

    uint32_t used = s_emu_log_head - s_emu_log_tail;
    if ((uint32_t)n > EMU_LOG_BUF_SIZE - used) return;   // переполнение — строку теряем

    for (int i = 0; i < n; i++) {
        s_emu_log[(s_emu_log_head + (uint32_t)i) & (EMU_LOG_BUF_SIZE - 1)] = line[i];
    }
    s_emu_log_head += (uint32_t)n;
}

// r1748: буфер хранит '\n', а UART ждёт CRLF — иначе строки уезжают вправо.
// r1749: каждый buffered-лог начинаем с '\r' — гарантия нулевой колонки,
// даже если перед ним был прямой printf/чужой вывод.
static int s_emu_log_col0 = 1;
static void emu_log_putc(char c) {
    if (c == '\n') {
        uart_putc('\r');
        uart_putc('\n');
        s_emu_log_col0 = 1;
        return;
    }
    if (s_emu_log_col0) {
        uart_putc('\r');
        s_emu_log_col0 = 0;
    }
    uart_putc(c);
}

void emu_log_flush_idle(uint32_t budget_us) {
    if (!budget_us || s_emu_log_tail == s_emu_log_head) return;
    uint32_t t0 = h3_hs_timer_lo_us();
    while (s_emu_log_tail != s_emu_log_head) {
        if ((h3_hs_timer_lo_us() - t0) >= budget_us) break;
        emu_log_putc(s_emu_log[s_emu_log_tail & (EMU_LOG_BUF_SIZE - 1)]);
        s_emu_log_tail++;
    }
}

void emu_log_flush_all(void) {
    while (s_emu_log_tail != s_emu_log_head) {
        emu_log_putc(s_emu_log[s_emu_log_tail & (EMU_LOG_BUF_SIZE - 1)]);
        s_emu_log_tail++;
    }
}

void emu_throttle(void) {
    // r155: мигание alive (PL10, «код жив») убрано с core0 — теперь его делает
    // ЯДРО 1 (led_heartbeat_cpu1), чтобы core0 не писал в PA_DAT (гонка с PA21/CS).

    uint32_t now = h3_hs_timer_lo_us();
    if (!emu_ts0) emu_ts0 = now;   // первый кадр — стартовая база

    // r940: АДАПТИВНЫЙ ТАЙМИНГ. Стенд-лог r939: fill=1..1539/65536 при under=429 —
    // продюсер ждал весь период (кольцо уходило в пустоту, FIFO в underrun).
    // Теперь пауза зависит от заполнения кольца: цель ≈4800 пар (~100 мс хедрума).
    // Если кольцо ниже цели — почти не ждём (продюсер догоняет буфер); если выше
    // цели — ждём больше (не переполняем). Кольцо всегда накоплено → микросбои
    // загрузки/сцены подмелёт буфер, подмелёт подклики.
    uint32_t period = emu_period_us;
    // r1350: компенсация дробной части периода. Для MD/SMS реальный период
    // 16688.44 (NTSC) / 20120.13 (PAL) мкс: каждый кадр добавляем к целому
    // периоду перенос накопителя — средний период точный, дрейф не копится.
    if (emu_period_frac) {
        emu_period_phase += emu_period_frac;
        period += emu_period_phase >> 16;
        emu_period_phase &= 0xFFFFu;
    }
    {
        uint32_t fill = i2s_ring_fill();
        // r944: цель кольца адаптивная — из фактического темпа потока
        // (i2s_ring_observe_frame с дельтой пар за кадр). Это автоматически
        // учитывает региональную частоту кадра (NTSC 60 / PAL/Dendy 50 /
        // A2600-выбор в меню), т.к. источник — реальный поток, а не хардкод.
        // target = 2 кадра текущего EMA-темпа → FIFO не пустеет между кадрами
        // (меньше under-мусора), латентность не растёт.
        uint32_t pairs_now = 0, dummy = 0;
        {
            uint32_t fmax, fmin, drop, under;
            i2s_stats_get(&fmax, &fmin, &drop, &under, &pairs_now);
        }
        (void)dummy;
        if (pairs_now > emu_last_pairs) {
            i2s_ring_observe_frame(pairs_now - emu_last_pairs);
            emu_last_pairs = pairs_now;
        }
        uint32_t target = i2s_ring_target();
        uint32_t elapsed = now - emu_ts0 + (uint32_t)emu_debt;
        emu_debt = 0;

        // r944-диагностика: раз в 120 кадров печатаем время кадра и fill —
        // чтобы отличить «ядро не успевает» (elapsed > period) от «кольцо не
        // накапливается» (fill < target при elapsed малом). Блокирующий printf
        // раз в 120 кадров (~2 с) — допустимая редкость, не портит кольцо.
        // r956: периодический printf УБРАН из горячего пути. Блокирующий UART
        // (~6 мс на строку) останавливал продюсера → кольцо пустело, и сама
        // диагностика создавала провалы, которые потом «показывала». Сводка
        // выводится один раз в emu_audio_stop (AUDIO-END), когда поток стоит.
        (void)elapsed; (void)pairs_now; (void)fill; (void)target;

        // Единый ритм периода независимо от заполнения кольца. r944-FIX:
        // ждать до period/2, а базу сдвигать на целый period было БАГОМ (r944):
        // реально прошло period/2, а следующий кадр считал elapsed = -(period/2)
        // → огромное беззнаковое → ветка «переработка» → кадры гонятся подряд
        // почти без ожидания → эмуляция бежит в ~2 раза быстрее железа
        // (NGP «2×» в стенд-логах). Правильно: Кольцо ниже цели или нет — блюдём
        // честный ритм периода; буфер накапливается за счёт того, что генерация
        // идёт в темп 48к (CPU2), а не за счёт разгона кадров. База всегда
        // сдвигается на period, переработка (elapsed≥period) учитывается в debt.
        if (elapsed < period) {
            uint32_t wait = period - elapsed;
            uint32_t budget = (wait > 2000u) ? 1000u : wait / 2u;
            if (budget) {
                uint32_t f0 = h3_hs_timer_lo_us();
                emu_log_flush_idle(budget);
                uint32_t spent = h3_hs_timer_lo_us() - f0;
                if (spent < wait) udelay(wait - spent);
            } else {
                udelay(wait);
            }
            emu_ts0 += period;
        } else {
            // Кадр не успевает: большого простоя нет, но 200 мкс на лог
            // оставляем — иначе PROF/S16D не дойдут до UART на медленных играх.
            emu_log_flush_idle(200u);
            emu_debt = (int32_t)(elapsed - period);
            if (emu_debt > (int32_t)(period * 3)) emu_debt = (int32_t)(period * 3);
            emu_ts0 += period;
        }
    }

    // r911: периодический UART-дамп УБРАН. Блокирующий printf (~6 мс) каждые
    // 300 кадров останавливал продюсер, кольцо пустело → слышимые «разрывы».
    // Счётчики продолжают копиться; итог печатает emu_audio_stop() на выходе.
}

void emu_throttle_reset(void) {
    emu_ts0 = 0;
    emu_debt = 0;
    emu_last_pairs = 0;   // r1271 (CORE-03): дельта прошлой сессии не должна гасить EMA
    emu_period_phase = 0; // r1350: дробный остаток периода — с чистого листа
}

// ---- единый выход из эмулятора: удержание ~0.9 с ----
// Клавиша ESC (USB) ИЛИ Sega-геймпад Start+Mode вместе.
// Для геймпада не сканируем пад каждый кадр: кэш 50 мс — достаточно,
// чтобы поймать удержание, и не плодим лишние I2C-транзакции на шине.
// r155: armed-предохранитель — удержание НЕ стартует, пока после входа в
// эмулятор не случился хотя бы один «чистый» кадр без нажатий. Иначе
// зажатый при выходе из предыдущей игры ESC/Start+Mode «доезжает» в новую
// и через ~0.9 с выкидывает её обратно в меню («условие выхода осталось»).
static uint32_t g_esc_hold_us = 0;
static uint32_t g_pad_esc_t = 0;
static uint16_t g_pad_esc_val = 0;
static int      g_esc_armed = 0;
static uint32_t g_no_esc_since = 0;   // r155: sticky — отсутствие нажатия выхода
static uint32_t g_esc_arm_t = 0;      // r158: сколько держится «доезд» после входа
static int      g_esc_hold_from_pad = 0; // r590: hold накоплен от Start (геймпад)

void emu_esc_hold_reset(void) {
    g_esc_hold_us = 0;
    g_pad_esc_val = 0;
    g_pad_esc_t   = 0;
    g_esc_armed   = 0;
    g_no_esc_since = 0;
    g_esc_arm_t   = 0;
    g_esc_hold_from_pad = 0;
    usb_kbd_esc3_reset();   // ESC x3 (Low-Speed донгл) не должен «доехать»
}

int emu_esc_hold(void) {
    // Выход по ESC x3 — для Low-Speed донгла (I8 Pro), где длинное удержание
    // ненадёжно. Срабатывает от фронтов, «доезд» залипшей ESC не сгенерирует
    // (повторов не бывает), поэтому арм-предохранитель обходим сознательно.
    if (usb_kbd_esc3_pressed()) {
        return 1;
    }

    uint8_t raw_keys[6];
    // r0.221: НИ ОДНОГО нового USB-чтения здесь! Два usb_kbd_get_raw за кадр
    // (ядро + этот полл) пере-армят interrupt-IN TD, пока HC ещё обрабатывает
    // его → ED-цепочка OHCI рассинхронизируется и клавиатура «замирает» во
    // всех эмуляторах и в меню после выхода. Читаем ТОЛЬКО кэш последнего
    // отчёта, который обновил host-слой ядра на этом кадре.
    int n = usb_kbd_get_last(raw_keys, 6);
    int esc = 0;
    for (int i = 0; i < n; i++)
        if (raw_keys[i] == 41) { esc = 1; break; }

// Геймпад: выход в меню — ТОЛЬКО Start+Select удержанием ~0.9 с.
    // Select: на джойстике 6-btn это Mode (0x0800), на кнопочной плате — X
    // (0x0100, btn_pad «X=Select/Coin»). Чтобы выход работал на ОБОИХ
    // устройствах, комбинация = Start(0x0080) + (Mode|X).
    // Одиночный Start НЕ выходит: он во многих ядрах нужен как игровая кнопка
    // (пауза/старт), и короткие нажатия Start не должны выкидывать из игры.
    uint32_t now = h3_hs_timer_lo_us();
    if (now - g_pad_esc_t > 50000) {
        g_pad_esc_t = now;
        g_pad_esc_val = pad_scan_combined();
    }
    // r590/r624: выход по геймпаду — ТОЛЬКО непрерывное удержание
    // Start+Mode ~1 с. Одиночный Start не накапливает hold.
    int pad_start = 0;
    int pad_esc = (g_pad_esc_val & 0x0080) && (g_pad_esc_val & (0x0800 | 0x0100));
    if (pad_esc) { esc = 1; pad_start = 1; }   // Start+Select (джойстик Mode / плата X)

    if (esc) {
        g_no_esc_since = 0;
        if (!g_esc_armed) {
            // «Доезд» из прошлой игры блокируем, НО не навсегда: если нажатие
            // держится >1.5 с — это не остаток, а залип пада или реальное
            // удержание. Разоружаемся, иначе выход был бы мёртв навсегда
            // (симптом: «не выходит из эмулятора» + спам при esc=1).
            if (!g_esc_arm_t) g_esc_arm_t = now;
            else if (now - g_esc_arm_t > 1500000) { g_esc_armed = 1; g_esc_arm_t = 0; }
            if (!g_esc_armed) return 0;
        }
        if (!g_esc_hold_us) { g_esc_hold_us = now; g_esc_hold_from_pad = pad_start; }
        else if (now - g_esc_hold_us > 900000) { g_esc_hold_us = 0; return 1; }
    } else {
        g_esc_arm_t = 0;
        g_esc_hold_from_pad = 0;   // Start отпущен — любое накопление недействительно
        // r155: клавиатура шлёт отчёты ПАЧКАМИ (между ними «пустые» промежутки),
        // поэтому непрерывное удержание не требуется: отсчёт выхода теряется
        // только если нажатие отсутствует >250 мс подряд. Иначе после
        // «поиграть» пустой пакет клавы каждые ~10-30 мс рвал бы удержание
        // и выход никогда не накапливался.
        if (g_esc_hold_us) {
            if (g_esc_hold_from_pad) {
                // hold от геймпада: отпускание Start сбрасывает hold СРАЗУ
                // (только непрерывное удержание 1 с, без «суммирования»
                // коротких нажатий).
                g_esc_hold_us = 0;
                g_no_esc_since = 0;
            } else {
                if (!g_no_esc_since) g_no_esc_since = now;
                else if (now - g_no_esc_since > 250000) { g_esc_hold_us = 0; g_no_esc_since = 0; }
            }
        } else {
            g_no_esc_since = 0;
            g_esc_armed = 1;   // чистый кадр без удержания — «доезд» разряжен
        }
    }
    return 0;
}

// ---- эмуляторы ----
extern void atari2600_init(const uint8_t* rom, uint32_t size);
extern void atari2600_run_frame(void);
extern void atari2600_set_difficulty(int p1_expert);
extern int a7800_init_game(const uint8_t* rom, uint32_t size);
extern void a7800_run_frame(void);
extern void a7800_render_frame(void);   // r1523: DE2 GSU рендер кадра A7800
extern int a7800_hw_scaler_active(void);   // r1523: DE2 GSU активен (пропустить emu_scale)
extern void a7800_reset_hw_scaler(void);   // r1523: сброс флага GSU при входе
extern int a5200_init_game(const uint8_t* rom, uint32_t size);
extern void a5200_run_frame(void);
extern void a5200_render_frame(void);
extern int a5200_hw_scaler_active(void);
extern void a5200_reset_hw_scaler(void);
extern int sms_init_game(const uint8_t* rom, uint32_t size);
extern void sms_run_frame(void);
extern void sms_render_frame(void);   // r1509: DE2 GSU рендер кадра SMS (256×192 → 800×600)
extern int sms_hw_scaler_active(void);   // r1509: DE2 GSU активен (пропустить emu_scale)
extern int gg_init_game(const uint8_t* rom, uint32_t size);
extern void gg_run_frame(void);
extern int gg_hw_scaler_active(void);   // r1503: DE2 GSU активен (пропустить emu_scale_int)
// r1678: рендер кадра GG на DE2 GSU — в цикле он не вызывался вовсе, поэтому
// флаг всегда 0 и кадр шёл программным emu_scale_int (r1503 дал только код).
extern void gg_render_frame(void);
extern int sg_init_game(const uint8_t* rom, uint32_t size);
extern void sg_run_frame(void);
extern void sg_render_frame(void);   // r1510: DE2 GSU рендер кадра SG-1000 (256×192 → 800×600)
extern int sg_hw_scaler_active(void);   // r1510: DE2 GSU активен (пропустить emu_scale)
extern int portfolio_init_game(const uint8_t* rom, uint32_t size);
extern void portfolio_run_frame(void);
extern int portfolio_exit_requested(void);
extern int gb_init_game(const uint8_t* rom, uint32_t size);
extern void gb_run_frame(void);
extern void gb_render_frame(void);
extern int gb_hw_scaler_active(void);   // r1499: DE2 GSU активен (пропустить emu_scale_int)
extern int gba_init_game(const uint8_t* rom, uint32_t size);
extern void gba_run_frame(void);
extern void gba_render_frame(void);
extern int gba_hw_scaler_active(void);   // r1494: DE2 GSU активен (пропустить emu_scale_int)
extern int lynx_init_game(const uint8_t* rom, uint32_t size);
extern void lynx_run_frame(void);
extern void lynx_render_frame(void);
extern int lynx_hw_scaler_active(void);   // r1497: DE2 GSU активен (пропустить emu_scale_int)
extern int ngp_init_game(const uint8_t* rom, uint32_t size);
extern void ngp_run_frame(void);
extern int ngp_hw_scaler_active(void);   // r1500: DE2 GSU активен (пропустить emu_scale_int)
extern void emu_run_snes(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_nes(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_megadrive(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_msx(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_coleco(const uint8_t* rom, uint32_t size, const char* rom_name);

void emu_run_a7800(const uint8_t* rom, uint32_t size, const char* rom_name) {
    extern uint8_t a7800_tv_mode;
    extern void a7800_set_tv_mode(int mode);
    extern int a7800_region_pal(void);
    emu_prepare();
    // r1184: развертка из меню (NTSC / PAL / Auto) — до init, ядро читает
    // region_type в region_Reset(); эффективный режим известен после init.
    a7800_set_tv_mode(a7800_tv_mode);
    emu_prepare();
    emulog_begin("A7800");
    emulog_field("A7800", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    if (a7800_init_game(rom, size) != 1) {
        emulog_field("A7800", "init", "FAILED");
        emulog_end("A7800");
        return;
    }
    emu_set_border_color(0x00281206);   // тёмно-бордовый
    emu_set_period_us(a7800_region_pal() ? 20000u : 16667u);
    emulog_field("A7800", "video", "320x240");
    emulog_field("A7800", "machine", "%s", a7800_region_pal() ? "PAL" : "NTSC");
    emulog_field("A7800", "init", "OK");
    // r1523: сброс флага аппаратного скейлера ПРИ КАЖДОМ входе (урок r1496):
    // static переживает сессию, без сброса повторный запуск = чёрный экран.
    a7800_reset_hw_scaler();
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        a7800_run_frame(); emu_throttle();
        // r1523: a7800_render_frame вызывается БЕЗУСЛОВНО каждый кадр (эталон
        // r1519) — именно он при первом кадре настраивает DE2 GSU.
        a7800_render_frame();
        if (!a7800_hw_scaler_active())
            emu_scale(320, 240);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    // r1523: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр A7800 и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);

        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    emu_audio_stop(); fb_clear(); fb_flush(); emulog_end("A7800");
}

void emu_run_a5200(const uint8_t* rom, uint32_t size, const char* rom_name) {
    extern uint8_t a5200_tv_pal;
    extern void a5200_set_tv_pal(int pal);
    emu_prepare();
    // r1184: развертка из меню — до init (ядро берёт tv_mode как длину кадра
    // и для GTIA/коллизий). Дефолт NTSC: 5200-картриджи в основном NTSC.
    a5200_set_tv_pal(a5200_tv_pal);
    emu_prepare();
    emulog_begin("A5200");
    emulog_field("A5200", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    if (a5200_init_game(rom, size) != 1) {
        emulog_field("A5200", "init", "FAILED");
        emulog_end("A5200");
        return;
    }
    emu_set_border_color(0x00061428);   // тёмно-синий
    emu_set_period_us(a5200_tv_pal ? 20000u : 16667u);
    emulog_field("A5200", "video", "320x240");
    emulog_field("A5200", "machine", "%s", a5200_tv_pal ? "PAL" : "NTSC");
    emulog_field("A5200", "init", "OK");
    // r1524: сброс флага аппаратного скейлера ПРИ КАЖДОМ входе (урок r1496):
    // static переживает сессию, без сброса повторный запуск = чёрный экран.
    a5200_reset_hw_scaler();
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        a5200_run_frame();
        emu_throttle();
        // r1524: a5200_render_frame вызывается БЕЗУСЛОВНО каждый кадр (эталон
        // r1519) — именно он при первом кадре настраивает DE2 GSU.
        a5200_render_frame();
        if (!a5200_hw_scaler_active())
            emu_scale(320, 240);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    // r1524: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр A5200 и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);

        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    emu_audio_stop(); fb_clear(); fb_flush(); emulog_end("A5200");
}

void emu_run_sms(const uint8_t* rom, uint32_t size, const char* rom_name) {
    extern uint32_t gpgx_frame_us_q16(void);   // r1350: точный период кадра ядра
    emu_prepare();
    emulog_begin("SMS");
    emulog_field("SMS", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    if (sms_init_game(rom, size) != 1) {
        emulog_field("SMS", "init", "FAILED");
        emulog_end("SMS");
        return;
    }
    emulog_field("SMS", "init", "OK");
    emu_set_border_color(0x00081430);   // тёмно-синий (SMS)
    // r1180: PAL-версии SMS идут 50 Гц (иначе ~20% быстрее и звук уплывает).
    // r1350: период — из реального mclk ядра (16688/20120 мкс), иначе звук
    // копится в кольце I2S: ровные 16667/20000 не совпадают с 59.9229/49.7015 Гц.
    emu_set_period_us_ex(gpgx_frame_us_q16());
    emulog_field("SMS", "video", "256x192");
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        sms_run_frame();
        sms_render_frame();
        emu_throttle();
        // r1509: при активном DE2 GSU кадр уже на экране (sms_render_frame
        // конвертирует RGB565→XRGB8888, GSU растягивает на 600) — программный
        // emu_scale не нужен и наложился бы поверх аппаратного.
        if (!sms_hw_scaler_active())
            emu_scale(256, 192);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    // r1509: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр SMS и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    // r1351: gpgx_stop() НЕ вызывался для SMS/GG/SG — blip-буферы ядра
    // переживали выход, следующий emu_prepare сбрасывал bump-пул, и
    // audio_init() новой сессии делал blip_delete/memset по переиспользованной
    // памяти (порча чужих данных — вход в GG после Sega-системы падал).
    {
        extern void gpgx_stop(void);
        gpgx_stop();
    }
    emu_audio_stop(); fb_clear(); fb_flush();
    emulog_end("SMS");
}

void emu_run_gg(const uint8_t* rom, uint32_t size, const char* rom_name) {
    extern uint32_t gpgx_frame_us_q16(void);   // r1350: точный период кадра ядра
    emu_prepare();
    emulog_begin("GG");
    emulog_field("GG", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    if (gg_init_game(rom, size) != 1) {
        emulog_field("GG", "init", "FAILED");
        emulog_end("GG");
        return;
    }
    emu_set_border_color(0x00082030);   // тёмно-синий (GG)
    // r1350: период НЕ выставлялся вовсе — наследовался от предыдущей системы
    // (60/50 Гц от NTSC/PAL-игры или 20 мс от Amiga) → звук GG уезжал по темпу.
    emu_set_period_us_ex(gpgx_frame_us_q16());
    // r1410: GG-игры чаще объявляют разрешение больше настоящего растра.
    // Реальный viewport 160x144 (gg_extra=0); то, что просит ROM, ядро режет
    // этим растром — поле video показывает требуемое/view против реального.
    {
        extern int gpgx_viewport_w(void);
        extern int gpgx_viewport_h(void);
        int vw = gpgx_viewport_w();
        int vh = gpgx_viewport_h();
        if (vw == 160 && vh == 144)
            emulog_field("GG", "video", "160x144 (native)");
        else
            emulog_field("GG", "video", "rom=%dx%d view=160x144%s",
                         vw, vh,
                         (vw > 160 || vh > 144) ? " [cropped]" : "");
    }
    emulog_field("GG", "init", "OK");
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        gg_run_frame();
        gg_render_frame();   // r1678: DE2 GSU (GG переведён с программного скейла)
        emu_throttle();
        // r1503: при активном DE2 GSU кадр уже на экране (gg_render_frame
        // конвертирует RGB565→XRGB8888, GSU растягивает на 600) — программный
        // emu_scale_int не нужен и наложился бы поверх аппаратного.
        if (!gg_hw_scaler_active())
            emu_scale_int(160, 144);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    // r1503: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр GG и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    {
        extern void gpgx_stop(void);   // r1351: снять blip-буферы до сброса пула
        gpgx_stop();
    }
    emu_audio_stop(); fb_clear(); fb_flush();
    emulog_end("GG");
}

void emu_run_sg1000(const uint8_t* rom, uint32_t size, const char* rom_name) {
    // r1190: SG-1000 — отдельный слой на ядре gpgx (TMS99xx VDP, 2 кнопки).
    extern uint32_t gpgx_frame_us_q16(void);   // r1350: точный период кадра ядра
    emu_prepare();
    emulog_begin("SG1000");
    emulog_field("SG1000", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    if (sg_init_game(rom, size) != 1) {
        emulog_field("SG1000", "init", "FAILED");
        emulog_end("SG1000");
        return;
    }
    emu_set_border_color(0x00141830);   // тёмно-синий (SG-1000)
    emu_set_period_us_ex(gpgx_frame_us_q16());
    emulog_field("SG1000", "video", "256x192");
    emulog_field("SG1000", "init", "OK");
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        sg_run_frame();
        sg_render_frame();
        emu_throttle();
        // r1510: при активном DE2 GSU кадр уже на экране (sg_render_frame
        // конвертирует RGB565→XRGB8888, GSU растягивает на 600) — программный
        // emu_scale не нужен и наложился бы поверх аппаратного.
        if (!sg_hw_scaler_active())
            emu_scale(256, 192);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    // r1510: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр SG-1000 и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    {
        extern void gpgx_stop(void);   // r1351: снять blip-буферы до сброса пула
        gpgx_stop();
    }
    emu_audio_stop(); fb_clear(); fb_flush();
    emulog_end("SG1000");
}

// LEGACY: старое A2600-ядро x2600/mcume. Компилируется (atari2600_*), но из
// меню НЕ вызывается — rom_browser запускает emu_run_a2600() (Stella, r1524).
// Оставлено как эталон поведения Reset/Select и запасной путь; не удалять без
// аудита (docs/ARCHITECTURE.md, «a2600: два ядра»).
void emu_run_a2600_mcume(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    emulog_begin("A2600");
    emulog_field("A2600", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    atari2600_init(rom, size);
    atari2600_set_difficulty(a2600_diff_expert);
    emulog_field("A2600", "machine", "diff=%s", a2600_diff_expert ? "Expert" : "Novice");
    emulog_field("A2600", "video", "160x192");
    emulog_field("A2600", "init", "OK");
    emu_set_border_color(0x00201A08);   // тёмно-янтарный (woodgrain A2600)
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        atari2600_run_frame(); emu_throttle(); emu_scale(160, 192); fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit: emu_audio_stop(); fb_clear(); fb_flush(); emulog_end("A2600");
}

void emu_run_portfolio(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    emulog_begin("Portfolio");
    emulog_field("Portfolio", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    if (portfolio_init_game(rom, size) != 1) {
        emulog_field("Portfolio", "init", "FAILED");
        emulog_end("Portfolio");
        return;
    }
    emulog_field("Portfolio", "init", "OK");
    emu_set_border_color(0x00101816);   // тёмно-оливковый
    uint32_t fc = 0;
    emu_throttle_reset();
    for (;;) {
        portfolio_run_frame();
        if (portfolio_exit_requested()) break;
        emu_throttle();
        emu_scale(320, 240);
        fb_flush();
        fc++;
    }
    fb_clear(); fb_flush();
    emu_audio_stop();
    emulog_end("Portfolio");
}

void emu_run_gameboy(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    emulog_begin("GameBoy");
    emulog_field("GameBoy", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    if (gb_init_game(rom, size) != 1) {
        emulog_field("GameBoy", "init", "FAILED");
        emulog_end("GameBoy");
        return;
    }
    emulog_field("GameBoy", "video", "160x144");
    emulog_field("GameBoy", "init", "OK");
    emu_set_border_color(0x000E1A0E);   // тёмно-зелёный (DMG)
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        gb_run_frame();
        gb_render_frame();
        emu_throttle();
        // r1499: при активном DE2 GSU кадр уже на экране (gb_render_frame
        // конвертирует RGBA8888→XRGB8888, GSU делает ×4) — программный
        // emu_scale_int не нужен и наложился бы поверх аппаратного.
        if (!gb_hw_scaler_active())
            emu_scale_int(160, 144);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    // r1499: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр GB и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    emu_audio_stop(); fb_clear(); fb_flush(); emulog_end("GameBoy");
}

void emu_run_gba(const uint8_t* rom, uint32_t size, const char* rom_name) {
    // r1351: указатель ядра не должен переживать сессию — пул сбрасывается
    // следующим emu_prepare(), и deinit по затёртой памяти ронял вход в GBA
    // после любой другой игры. Снимаем на ВЫХОДЕ (память ещё валидна), а
    // страховка в gba_init_game лишь обнуляет непредвиденный остаток.
    emu_prepare();
    emulog_begin("GBA");
    emulog_field("GBA", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    if (gba_init_game(rom, size) != 1) {
        emulog_field("GBA", "init", "FAILED");
        emulog_end("GBA");
        return;
    }
    emulog_field("GBA", "video", "240x160");
    emu_set_border_color(0x000E1A2E);   // тёмно-синий (GBA)
    // r892: жёсткий период кадра 16743 мкс = 59.727 Гц (16.777216 МГц /
    // 280896 тактов GBA). Раньше период считался EMA от числа пар звука
    // (gba_last_pairs) — но это подстраивало период ПОД скорость эмуляции,
    // а не ограничивало её. Если эмулятор быстрее железа (у нас H3 —
    // запредельно быстрая), EMA только закреплял «ускоренный» темп, рендер
    // зависел от сцены → нелинейное ускорение. Без удержания на железе
    // игры «улетают вперёд»; жёсткий период не даёт обогнать GBA.
    uint16_t saved_period = emu_period_us;
    emu_period_us = 16743;   // 59.727 Гц (GBA), не 60
    emulog_field("GBA", "machine", "59.73 Hz (period 16743 us)");
    emulog_field("GBA", "init", "OK");
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        gba_run_frame();
        gba_render_frame();
        emu_throttle();
        // r1494: при активном DE2 GSU кадр уже на экране (gba_render_frame
        // конвертирует RGB565→XRGB8888, GSU делает ×3) — программный
        // emu_scale_int не нужен и наложился бы поверх аппаратного.
        if (!gba_hw_scaler_active())
            emu_scale_int(240, 160);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    // r1494: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр GBA и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    // r1351: снять ядро mGBA, пока память bump-пула ещё валидна (следующий
    // emu_prepare сбросит пул, и deinit по затёртой памяти уронил бы вход).
    {
        extern void gba_release_core(void);
        gba_release_core();
    }
    emu_period_us = saved_period; emu_audio_stop(); fb_clear(); fb_flush();
    emulog_end("GBA");
}

void emu_run_lynx(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    emulog_begin("Lynx");
    emulog_field("Lynx", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    if (lynx_init_game(rom, size) != 1) {
        emulog_field("Lynx", "init", "FAILED");
        emulog_end("Lynx");
        return;
    }
    emulog_field("Lynx", "video", "160x102");
    emu_set_border_color(0x000E0D26);   // тёмно-фиолетовый (Lynx)
    emu_throttle_reset();
    emu_esc_hold_reset();
    // r511: рефреш Lynx ~75 Гц (48000/75 = 640 сэмплов/кадр), а не 60 Гц.
    // Период кадра считаем от фактического числа сэмплов (пара = 21 мкс).
    // r727 (синхронизация кольца): стартовый период СРАЗУ 13333 мкс (75 Гц),
    // а не 16667 с EMA-сходимостью за ~8 кадров. Пока EMA сходилась, кольцо
    // пустело (продюсер медленнее железа) и CPU2 заливал тишину, затем
    // переполнялось — стартовый дрейф/плавание задержки в первые секунды.
    emu_period_us = 13333;   // 75 Гц (48000/640 пар)
    emulog_field("Lynx", "machine", "75 Hz");
    emulog_field("Lynx", "init", "OK");
    for (;;) {
        lynx_run_frame();
        // r645: период кадра от ФАКТИЧЕСКОГО числа пар звука за кадр
        // (убрано слежение по магическому адресу 0x01C22028 — хрупкий прямой
        // доступ к I2S). Lynx: 75 Гц, ~640 пар/кадр при 48000.
        {
            extern uint32_t lynx_last_pairs(void);
            uint32_t p = lynx_last_pairs();
            if (p >= 300 && p <= 1200) {
                uint32_t peri = (uint32_t)(((uint64_t)p * 1000000u) / 48000u);
                if (peri >= 8000 && peri <= 16000)
                    emu_period_us = (uint16_t)(((uint32_t)emu_period_us * 7 + peri) / 8);
            }
        }
        lynx_render_frame();
        emu_throttle();
        // r1497: при активном DE2 GSU кадр уже на экране (lynx_render_frame
        // конвертирует RGB565→XRGB8888, GSU делает ×5) — программный
        // emu_scale_int не нужен и наложился бы поверх аппаратного.
        if (!lynx_hw_scaler_active())
            emu_scale_int(160, 102);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit: {
        // r1497: вернуть DE2 слой на буфер меню (иначе экран держит последний
        // кадр Lynx и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
        // r956: сводка Lynx печатается на выходе (поток остановлен) — до
        // emu_audio_stop, чтобы drop был ещё «за сессию».
        extern void lynx_audio_report(void);
        lynx_audio_report();
        // r1409: выгрузить объект ядра ДО сброса bump-пула след. emu_prepare —
        // иначе старый g_lynx гниёт в переиспользованной памяти и delete висит.
        extern void lynx_shutdown(void);
        lynx_shutdown();
    }
    emu_period_us = 16667; emu_audio_stop(); fb_clear(); fb_flush();
    emulog_end("Lynx");
}

void emu_run_ngp(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    emulog_begin("NGP");
    emulog_field("NGP", "rom", "%s, %u B", rom_name ? rom_name : "?", (unsigned)size);
    if (ngp_init_game(rom, size) != 1) {
        emulog_field("NGP", "init", "FAILED");
        emulog_end("NGP");
        return;
    }
    emulog_field("NGP", "video", "160x152");
    emu_set_border_color(0x000E1A2B);   // тёмно-синий (NGP)
    emu_throttle_reset();
    emu_esc_hold_reset();
    // r0.417 (NGP, п.5 сессии): ядро RACE считает кадр 515×198=101970 тиков
    // при Ticks=6·2^20 → ~61.7 Гц, а throttle стоял на общих 60 Гц — игра шла
    // на ~2.8% медленнее. Подстраиваем период под ядро; возвращаем общий 60 Гц
    // на выходе (паттерн из CPS, r0.390).
    emu_period_us = 16200;   // 61.7 Гц
    emulog_field("NGP", "machine", "61.7 Hz (period 16200 us)");
    emulog_field("NGP", "init", "OK");
    for (;;) {
        ngp_run_frame();
        emu_throttle();
        // r1500: при активном DE2 GSU кадр уже на экране (graphics_paint_impl
        // конвертирует RGB565→XRGB8888, GSU делает ×3) — программный
        // emu_scale_int не нужен и наложился бы поверх аппаратного.
        if (!ngp_hw_scaler_active())
            emu_scale_int(160, 152);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    // r1500: вернуть DE2 слой на буфер меню (иначе экран держит последний
    // кадр NGP и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);
        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    emu_period_us = 16667;   // r645: всегда дефолтный период (нет протечки от NGP 16200)
    emu_audio_stop();
    fb_clear(); fb_flush();
    emulog_end("NGP");
}
