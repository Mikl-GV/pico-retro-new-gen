/* ms1504_host.c — «Электроника МС 1504» (8086/PC-XT, КР1834ВМ86) на ядре
   Fake86 для bare-metal H3.
   Порядок: rom_browser отдаёт файл ПЗУ (дамп BIOS ≤ 64 КБ) → кладём в верх
   RAM (0x100000-n), грузим «железо» (8253/8259/8237, video, диск), reset86().
   Цикл: exec86(execloops) → USB-клавиатура (HID → PC-сканкод, doirq(1))
   → рендер текстового кадра (CGA 80x25, шрифт 8x16) → EMU_FB 320x200.
   Выход: emu_esc_hold. Звук: off (манифест). */

#include <stdint.h>
#include <string.h>

#include "fake86_src/config.h"
#include "fake86_src/cpu.h"
#include "fake86_src/ports.h"
#include "fake86_src/i8253.h"
#include "fake86_src/i8259.h"
#include "fake86_src/i8237.h"
#include "fake86_src/video.h"
#include "fake86_src/bios.h"
#include "fake86_src/disk.h"
#include "fake86_src/sermouse.h"
#include "fake86_src/timing.h"
#include "fake86_src/input.h"
#include "fake86_src/audio.h"   // r735: audbuf для звука PC-спикера

#include "emu.h"
#include "usb_kbd.h"
#include "fb_text.h"
#include "h3_hs_timer.h"
#include "i2s.h"

extern int printf(const char* fmt, ...);

#define EMU_FB      ((volatile uint16_t*)0x5F800000u)
#define EMU_FB_W    320
#define EMU_FB_H    240
#define FB_ADDR     0x5F900000u     // HDMI FB (XRGB8888, 1024×600)
#define FB_W_HDMI   1024
#define FB_H_HDMI   600
#define MS1504_SCR_W 320
#define MS1504_SCR_H 200

// r1050: переключатель ширины линий (жирный/тонкий), Ctrl+F10 (как Fn+→).
volatile uint8_t m15_ms1504_bold = 0;
static uint8_t g_bold_prev = 0;

// ---- HID-usage → PC set-1 сканкод (для МС1504) ----
// (0x04 = 'a' → 0x1E, 0x1E = '1' → 0x02, и т.д.)
static int ms1504_hid_to_pc(uint8_t h) {
    switch (h) {
    case 0x04: return 0x1E; case 0x05: return 0x30; case 0x06: return 0x2E;
    case 0x07: return 0x20; case 0x08: return 0x12; case 0x09: return 0x21;
    case 0x0A: return 0x22; case 0x0B: return 0x23; case 0x0C: return 0x17;
    case 0x0D: return 0x24; case 0x0E: return 0x25; case 0x0F: return 0x26;
    case 0x10: return 0x32; case 0x11: return 0x31; case 0x12: return 0x18;
    case 0x13: return 0x19; case 0x14: return 0x10; case 0x15: return 0x13;
    case 0x16: return 0x1F; case 0x17: return 0x14; case 0x18: return 0x16;
    case 0x19: return 0x2F; case 0x1A: return 0x11; case 0x1B: return 0x2D;
    case 0x1C: return 0x15; case 0x1D: return 0x2C;
    case 0x1E: return 0x02; case 0x1F: return 0x03; case 0x20: return 0x04;
    case 0x21: return 0x05; case 0x22: return 0x06; case 0x23: return 0x07;
    case 0x24: return 0x08; case 0x25: return 0x09; case 0x26: return 0x0A;
    case 0x27: return 0x0B;
    case 0x28: return 0x1C;   /* Enter */
    case 0x29: return 0x01;   /* Esc */
    case 0x2A: return 0x0E;   /* Backspace */
    case 0x2B: return 0x0F;   /* Tab */
    case 0x2C: return 0x39;   /* Space */
    case 0x2D: return 0x0C;   /* - */
    case 0x2E: return 0x0D;   /* = */
    case 0x2F: return 0x1A;   /* [ */
    case 0x30: return 0x1B;   /* ] */
    case 0x31: return 0x2B;   /* \ */
    case 0x33: return 0x27;   /* ; */
    case 0x34: return 0x28;   /* ' */
    case 0x35: return 0x29;   /* ` */
    case 0x36: return 0x33;   /* , */
    case 0x37: return 0x34;   /* . */
    case 0x38: return 0x35;   /* / */
    case 0x39: return 0x3A;   /* CapsLock */
    case 0x3A: return 0x3B; case 0x3B: return 0x3C; case 0x3C: return 0x3D;
    case 0x3D: return 0x3E; case 0x3E: return 0x3F; case 0x3F: return 0x40;
    case 0x40: return 0x41; case 0x41: return 0x42; case 0x42: return 0x43;
    case 0x43: return 0x44; case 0x44: return 0x57; case 0x45: return 0x58;
    case 0x46: return 0x37;   /* PrintScreen */
    case 0x47: return 0x46;   /* ScrollLock */
    case 0x49: return 0x52;   /* Insert */
    case 0x4A: return 0x47;   /* Home */
    case 0x4B: return 0x49;   /* PageUp */
    case 0x4C: return 0x53;   /* Delete */
    case 0x4D: return 0x4F;   /* End */
    case 0x4E: return 0x51;   /* PageDown */
    case 0x4F: return 0x4D;   /* Right */
    case 0x50: return 0x4B;   /* Left */
    case 0x51: return 0x50;   /* Down */
    case 0x52: return 0x48;   /* Up */
    /* Keypad (NumLock on): 54=/ 55=* 56=- 57=+ 58=Enter 59-61=1-3 ... */
    case 0x54: return 0x35;
    case 0x55: return 0x37;
    case 0x56: return 0x4A;
    case 0x57: return 0x4E;
    case 0x58: return 0x1C;
    case 0x59: return 0x4F; case 0x5A: return 0x50; case 0x5B: return 0x51;
    case 0x5C: return 0x4B; case 0x5D: return 0x4C; case 0x5E: return 0x4D;
    case 0x5F: return 0x47; case 0x60: return 0x48; case 0x61: return 0x49;
    case 0x62: return 0x52; case 0x63: return 0x53;
    /* Mods: Ctrl/Shift/Alt — только фронт? нет: подаём как сканкоды */
    case 0xE0: case 0xE4: return 0x1D;   /* Ctrl */
    case 0xE1: return 0x2A;  case 0xE5: return 0x36;   /* Shift */
    case 0xE2: case 0xE6: return 0x38;   /* Alt */
    default: return -1;
    }
}

static uint8_t g_kbd_prev[256];
// r1055: тайминги авто повтора (первый повтор/темп) — те же, что в меню
// (usb_kbd.c KBD_REPEAT_DELAY/RATE_US). Храним в микросекундах от первого
// нажатия по кадровому таймеру (frame_t в ms1504_host.c).
#define M15_REPEAT_DELAY_US 350000u
#define M15_REPEAT_RATE_US   90000u
static uint32_t g_kbd_down_t[256];   // микросекунды первого нажатия (0 = не зажата)
static uint32_t g_kbd_rep_t[256];    // микросекунды последнего автоповтора

// Подать сканкод в «клавиатуру» ПК (как input.c): порт 0x60 + BDA.
static void feed_pc_key(uint8_t scan, int down) {
    portram[0x60] = (uint8_t)(scan | (down ? 0 : 0x80));
    portram[0x64] |= 2;                 // input buffer full
    // r1023: клавиша должна попасть в BDA-буфер — BIOS ждёт её через
    // INT 16h (AH=0), а IRQ1-диспатч при internalbios=0 его не кормит.
    // r1054: отпускание модификатора (Shift/Ctrl/Alt/Num/Scroll/Ins) тоже
    // доходит — kbd_set_mod0 с битом 0x80 сбрасывает бит в BDA[0x417].
    extern void ms1504_kbd_feed(uint8_t);
    if (down) {
        ms1504_kbd_feed(scan);
    } else if (scan == 0x2A || scan == 0x36 || scan == 0x1D || scan == 0x38 ||
               scan == 0x45 || scan == 0x46 || scan == 0x52) {
        ms1504_kbd_feed((uint8_t)(scan | 0x80));
    }
    // r1030: НЕ заявляем IRQ1 (doirq(1)) при нажатиях. BIOS PK300 тест PIC
    // (F6270: IN 20; SUB AL,1; JAE error) требует IRR==0 в момент проверки;
    // висящий IRQ1 от клавиши даёт irr=02 → тест падает → бесконечный
    // повтор POST (OUT 63,99). Клавиатура МС1504 опрашивается через
    // INT 16h/порт 0x60 poll (BDA-буфер), IRQ1 на этом BIOS не нужен.
}

static void ms1504_kbd_update(void) {
    uint8_t raw[8];
    int n = usb_kbd_get_raw(raw, 8);
    // r1050: Ctrl+F10 — переключатель «ширины линий» (жирный/тонкий) как
    // Fn+→ на железе МС1504. F10 = HID 0x44; Ctrl нажат — биты 0x01/0x10.
    {
        uint8_t mk = usb_kbd_get_mods();
        if (mk & (0x01 | 0x10)) {
            for (int i = 0; i < n; i++)
                if (raw[i] == 0x44) {
                    extern volatile uint8_t m15_ms1504_bold;
                    if (!g_bold_prev) {
                        g_bold_prev = 1;
                        m15_ms1504_bold ^= 1;
                    }
                }
        } else {
            g_bold_prev = 0;
        }
    }
    // r1016: тот же отчёт клавиатуры идёт в COM1-монитор (ALFA-DEBUG).
    extern void m15_feed_kbd_report(const uint8_t*, int, uint8_t);
    m15_feed_kbd_report(raw, n, usb_kbd_get_mods());
    // r1054: CapsLock — защёлка по фронту HID 0x39 (toggle бита 0x40 в BDA).
    // Поддерживается host'ом, т.к. BIOS обрабатывает CapsLock только как
    // модификатор (зажатие), а аппаратный CapsLock — переключатель.
    {
        static uint8_t g_caps_prev = 0;
        int caps_now = 0;
        for (int i = 0; i < n; i++)
            if (raw[i] == 0x39) { caps_now = 1; break; }
        if (caps_now && !g_caps_prev) {
            extern uint8_t RAM[];
            RAM[0x417] ^= 0x40;
        }
        g_caps_prev = (uint8_t)(caps_now ? 1 : 0);
    }
    // r1067: модификаторы из g_mods подаём ДО обычных клавиш. BIOS в
    // ms1504_kbd_feed решает регистр по BDA (scan2ascii: буквы/знаки
    // поднимаются при Shift|CapsLock). Если сначала подать букву, а Shift
    // потом — BDA-буфер уже получит НИЖНИЙ символ, и BIOS его не перепишет.
    // Раньше блок модов стоял ПОСЛЕ букв (r1055), из-за чего «Shift+A»
    // печатал «a». Порядок: CapsLock (защёлка) → моды → буквы.
    {
        static uint8_t g_mod_prev = 0;
        uint8_t mk = usb_kbd_get_mods();
        // Обходим все шесть HID-модкодов и сверяем бит в mk с прошлым
        // состоянием мода. Изменение — фронт (feed_pc_key решает сам).
        static const uint8_t mod_hid[6] = {0xE0, 0xE1, 0xE2, 0xE4, 0xE5, 0xE6};
        static const uint8_t mod_bit[6] = {0x01, 0x02, 0x04, 0x10, 0x20, 0x40};
        for (int m = 0; m < 6; m++) {
            int on = (mk & mod_bit[m]) != 0;
            int was = (g_mod_prev & mod_bit[m]) != 0;
            if (on != was) {
                int pc = ms1504_hid_to_pc(mod_hid[m]);
                if (pc >= 0) {
                    g_kbd_prev[pc] = (uint8_t)(on ? 1 : 0);
                    feed_pc_key((uint8_t)pc, on);
                }
            }
        }
        g_mod_prev = mk;
    }
    for (int i = 0; i < n; i++) {
        if (raw[i] == 0x39) continue;   // CapsLock уже обработан выше
        int pc = ms1504_hid_to_pc(raw[i]);
        if (pc < 0) continue;
        if (!g_kbd_prev[pc]) {
            g_kbd_prev[pc] = 1;
            feed_pc_key((uint8_t)pc, 1);
        }
    }
    // r1055 (пакет E): автоповтор удержанной клавиши. Донгл молчит при
    // удержании, BIOS-буфер (INT 16h) не повторяет сам — и текст «AaaA»
    // при удержании A не печатается. Здесь, на кадровой частоте, повторяем
    // зажатую клавишу с порогами M15_REPEAT_DELAY/RATE_US (как меню).
    {
        uint32_t now = h3_hs_timer_lo_us();
        for (int i = 0; i < n; i++) {
            int pc = ms1504_hid_to_pc(raw[i]);
            if (pc < 0 || !g_kbd_prev[pc]) continue;
            if (g_kbd_down_t[pc] == 0) { g_kbd_down_t[pc] = now; g_kbd_rep_t[pc] = now; continue; }
            if (now - g_kbd_down_t[pc] >= M15_REPEAT_DELAY_US &&
                now - g_kbd_rep_t[pc] >= M15_REPEAT_RATE_US) {
                g_kbd_rep_t[pc] = now;
                feed_pc_key((uint8_t)pc, 1);
            }
        }
    }
    // отпущенные: сканкод, которого нет в текущем отчёте
    for (int pc2 = 1; pc2 < 0x59; pc2++) {
        if (g_kbd_prev[pc2]) {
            int still = 0;
            for (int i = 0; i < n; i++)
                if (ms1504_hid_to_pc(raw[i]) == pc2) { still = 1; break; }
            if (!still) {
                g_kbd_prev[pc2] = 0;
                g_kbd_down_t[pc2] = 0; g_kbd_rep_t[pc2] = 0;
                // автоповтор USB не переопрашиваем: отпускаем сразу
                feed_pc_key((uint8_t)pc2, 0);
            }
        }
    }
}

// ---- рендер CGA/текст: 640x400 (80x25, шрифт 8x16 1:1) → 320x200 (среднее 2x2) ----
static uint16_t ms1504_rgb565(uint32_t rgb888) {
    uint8_t r = (uint8_t)(rgb888 >> 16), g = (uint8_t)(rgb888 >> 8), b = (uint8_t)rgb888;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// r1050: вывод как на РОДНОМ железе МС1504 — 640×400 напрямую на экран
// (бордюры не проблема). ms1504_scr[640*400] рендерится 1:1, затем
// nearest-масштабом в HDMI FB 1024×600. Bold (Ctrl+F10 / Fn+→) —
// дублирование пикселей шрифта по горизонтали.
#define MS1504_SCR_W_640 640
#define MS1504_SCR_H_400 400

static uint16_t ms1504_scr[MS1504_SCR_W_640 * MS1504_SCR_H_400];

static void ms1504_render_into(uint16_t* dst, int dw, int dh) {
    uint32_t vb = videobase;
    if (vb > RAM_SIZE - 4096) vb = 0xB8000;
    const uint8_t* text = RAM + vb;
    uint16_t ncols = cols ? cols : 80;
    uint16_t nrows = rows ? rows : 25;
    for (int y = 0; y < dh; y++) {
        uint16_t cy = (uint16_t)(y >> 4);
        uint16_t fy = (uint16_t)(y & 15);
        if (cy >= nrows) break;
        const uint8_t* row = text + ((size_t)cy * ncols) * 2;
        // предвычисляем строку шрифта со сдвигом bold (уширенные линии)
        for (int x = 0; x < dw; x++) {
            uint16_t cx = (uint16_t)(x >> 3);
            uint16_t fx = (uint16_t)(x & 7);
            if (cx >= ncols) break;
            uint8_t ch = row[cx * 2];
            uint8_t at = row[cx * 2 + 1];
            uint32_t fg = palettecga[at & 15];
            uint32_t bg = palettecga[(at >> 4) & 7];
            uint8_t dat = fontcga ? fontcga[(size_t)ch * 16 + fy] : 0;
            // bold: к текущему пикселю добавляем соседний слева (толще линии)
            if (m15_ms1504_bold && fx > 0 && (dat & (0x80 >> (fx - 1))))
                dat |= (uint8_t)(0x80 >> fx);
            dst[y * (uint32_t)dw + x] = ms1504_rgb565((dat & (0x80 >> fx)) ? fg : bg);
        }
    }
}

// nearest 640×400 → HDMI FB (1024×600; 960×600 = ×1.5, поля по бокам)
static void ms1504_scale_to_hdmi(void) {
    uint32_t* dst = (uint32_t*)FB_ADDR;   // XRGB8888
    int dst_w = FB_W_HDMI, dst_h = FB_H_HDMI;
    int offset_x = 0;
    if (dst_w > 960) offset_x = (dst_w - 960) / 2;
    int srcdw = 960;
    static int sx_tab[1024];
    for (int dx = 0; dx < srcdw; dx++)
        sx_tab[dx] = ((uint32_t)dx * 640u) / (uint32_t)srcdw;
    for (int dy = 0; dy < dst_h; dy++) {
        int sy = ((uint32_t)dy * 400u) / (uint32_t)dst_h;   // 0..399
        const uint16_t* src = ms1504_scr + (size_t)sy * 640;
        uint32_t* d = dst + (size_t)dy * FB_W_HDMI + (uint32_t)offset_x;
        for (int dx = 0; dx < srcdw; dx++) {
            uint16_t p = src[sx_tab[dx]];
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5) & 0x3F) << 2;
            uint32_t b = (p & 0x1F) << 3;
            d[dx] = (r << 16) | (g << 8) | b;
        }
    }
    // r1166 (E14): картинка 960×600 по центру, поля по бокам не перерисовываются
    // (остаются фоном от fb_clear) — dirty-зона равна картинке.
    fb_mark_dirty(offset_x, 0, srcdw, dst_h);
    fb_frame_scaled();
}

// показать: рендер 640×400 в ms1504_scr + прямой вывод на HDMI
static void ms1504_video_flush(void) {
    ms1504_render_into(ms1504_scr, MS1504_SCR_W_640, MS1504_SCR_H_400);
    ms1504_scale_to_hdmi();
    fb_flush();
}

// ---- init ----
// r1016: COM1 (8250) для ROM-монитора PK300 (см. ms1504_com.c)
void initcom1(void);

static int ms1504_inited = 0;

static void ms1504_hardware_init(void) {
    // r1048: ПОЛНЫЙ сброс слоя при каждом входе. Раньше guard ms1504_inited
    // пропускал повторную инициализацию (ports/8253/8259/8237/COM1) на
    // втором входе в эмулятор из меню → сломанное состояние → мгновенный
    // «MS1504: exit» без POST. Повторные входы теперь чистые.
    memset(RAM, 0, RAM_SIZE);
    memset(readonly, 0, RAM_SIZE);
    ports_init();
    init8253();
    init8259();
    init8237();
    initVideoPorts();
    initcga();
    inittiming();
    initcom1();   // r1016: COM1 (8250) для ROM-монитора PK300 «ALFA-DEBUG»
    ms1504_inited = 1;
}

// ---- COM1 (8250) для ROM-монитора PK300 (r1016, см. ms1504_com.c) ----

// ---- собственный BIOS (вшит) для режима «Встроенное ПО» ----
extern const unsigned char ms1504_bios_pk300[];
extern const unsigned int  ms1504_bios_pk300_len;
extern const unsigned char ms1504_hdd_bios[];
extern const unsigned int  ms1504_hdd_bios_len;

int ms1504_init_game(const uint8_t* rom, uint32_t size) {
    printf("MS1504: init size=%u\n", (unsigned)size);
    if (!rom || size == 0) {
        // r0.399: режим «Встроенное ПО» — родной BIOS (PK300) вшит в прошивку
        rom = ms1504_bios_pk300;
        size = ms1504_bios_pk300_len;
        printf("MS1504: builtin BIOS (PK300, вшит, %u Б)\n", (unsigned)size);
    } else if (size > 0x10000) {
        printf("MS1504: файл %u Б слишком большой — это не образ ПЗУ МС-1504 (нужен ≤64К)\n",
               (unsigned)size);
        return 0;
    }

    ms1504_hardware_init();
    memset(RAM, 0, RAM_SIZE);
    memset(readonly, 0, RAM_SIZE);

    // BIOS вверх RAM: F0000-FFFFF (как loadbios в main.c)
    memcpy(RAM + 0x100000 - size, rom, size);
    memset(readonly + 0x100000 - size, 1, size);
    printf("MS1504: BIOS %u Б -> 0x%05X, readonly\n", (unsigned)size,
           (unsigned)(0x100000 - size));

    // r1021: BIOS PK300 в начале self-test ждёт, что BDA [0x40:0x72] == 0x1234
    // (маркер «warm boot»). См. дизассемблер: F618B MOV SI,[72];
    // F619C REP STOS (обнуляет IVT/BDA); F619E CMP SI,0x1234; JNE -> вечный
    // повтор шага 06 (лог: OUT 378,06). На «холодном» железе это слово
    // неинициализировано, но у ПК300 оно живёт весь сеанс; мы стартуем с
    // нулевого RAM, поэтому принудительно выставляем 0x1234 (как после
    // первого прохода BIOS по его же логике).
    RAM[0x472] = 0x34;   // 0x40:0x72 → адрес 0x472 (little-endian)
    RAM[0x473] = 0x12;
    // r1023: инициализация BDA-буфера клавиатуры (как fake86 initbios,
    // bios.c:285-292): head=tail=0x1E, границы кольца 0x480/0x482.
    RAM[0x41A] = 0x1E;   RAM[0x41B] = 0x00;
    RAM[0x41C] = 0x1E;   RAM[0x41D] = 0x00;
    RAM[0x480] = 0x1E;   RAM[0x481] = 0x00;
    RAM[0x482] = 0x3E;   RAM[0x483] = 0x00;

    running = 1;
    reset86();
    printf("MS1504: cpu reset (4.77 МГц КР1834ВМ86), boot\n");

    // r1027: монтируем вшитый загрузочный диск (mc1504BOOT.IMA, 720 КБ)
    // как A: — BIOS PK300 на INT 19h грузит с него МС1504-DOS (TBIOS/SYSTDOS)
    // вместо «Non-System disk»/зависания. hostfs (ms1504_shim.c) отдаёт
    // образ из ROM-массива (readonly). bootdrive=0 → fake86 INT19h-путь.
    // (имена с m15_: глобалы fake86-набора после rename-скрипта)
    extern uint8_t m15_insertdisk(uint8_t, const char*);
    extern uint8_t m15_bootdrive;
    extern const unsigned int  ms1504_boot_ima_len;
    // r1028: загрузчик МС1504 (0x7C71-0x7C7A) определяет привод по BPB-типу
    // из BDA[0x43F] (ищет установленный бит; для типа 1 получает DL=1) и
    // читает диск с DL=1. Без инициализации [0x43F]=0 загрузчик висит в
    // вечном цикле (SHR AL,1 даёт CF=0). Ставим тип 1 (360К/720К A:) и
    // вставляем образ и как привод 1 (загрузочный) и как привод 0.
    RAM[0x43F] = 0x01;
    if (m15_insertdisk(0, "mc1504BOOT.IMA")) {
        printf("MS1504: WARNING: boot disk (unit 0) not inserted\n");
    }
    if (m15_insertdisk(1, "mc1504BOOT.IMA")) {
        printf("MS1504: WARNING: boot disk (unit 1) not inserted\n");
    } else {
        m15_bootdrive = 1;
        printf("MS1504: boot disk A: inserted (mc1504BOOT.IMA, %u Б)\n",
               (unsigned)ms1504_boot_ima_len);
    }
    return 1;
}

// ---- точка входа из rom_browser (emu.h) ----
// r735/r754: звук МС1504 (PC-спикер) в I2S. Генерация — m15_tick_audio() в
// shim (не fake86/audio.c — его нет в MS1504_BONES, стоковые глобалы
// usesamplerate/audbuf/… не существуют в сборке). Формат: m15_audbuf — s8,
// фиксированные 44100 Гц (генерит shim). Ресемплим 44100→48000, моно→стерео.
    static void ms1504_audio_flush(void) {
        extern void m15_tick_audio(int n);
        extern int8_t m15_audbuf[96000];
        extern int32_t m15_audbufptr;
        static uint32_t rs_phase = 0;
        m15_tick_audio(735);   // 44100/60 ≈ 735 сэмплов за кадр
        int n = m15_audbufptr;
        if (n > 2048) n = 2048;
        static int16_t out[2048];
        /* s8 → s16 */
        for (int i = 0; i < n; i++)
            out[i] = (int16_t)((int)m15_audbuf[i] << 8);
        /* линейный upsampler 44100→48000 */
        for (uint32_t i = 0; (long)i < (long)n - 1;) {
            uint32_t f = rs_phase & 0xFFFFu;
            int32_t s = (int32_t)out[i] + (int32_t)(((int64_t)(out[i+1] - out[i]) * (int32_t)f) >> 16);
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            i2s_push_sample((int16_t)s, (int16_t)s);
            rs_phase += 60211u;
            uint32_t adv = rs_phase >> 16;
            if (adv) { i += adv; rs_phase &= 0xFFFF; }
        }
        m15_audbufptr = 0;
    }

void emu_run_ms1504(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("ms1504", "spkr");
    if (!ms1504_init_game(rom, size)) return;
    emu_set_border_color(0x00000000);
    emu_throttle_reset();
    emu_esc_hold_reset();
    memset(g_kbd_prev, 0, sizeof(g_kbd_prev));

    printf("MS1504: \"%s\" (%u Б) — execute\n", rom_name ? rom_name : "?", (unsigned)size);
    for (;;) {
        ms1504_kbd_update();
        exec86(120000);
        // r1050: video_flush рендерит 640×400 напрямую в HDMI FB и флашит
        ms1504_video_flush();
        ms1504_audio_flush();   // r735: PC-спикер → I2S
        emu_throttle();
        if (emu_esc_hold()) break;
    }

    running = 0;
    fb_clear(); fb_flush();
    printf("MS1504: exit\n");
}