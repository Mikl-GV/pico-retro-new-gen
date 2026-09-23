// remap.c — ремап клавиатуры -> кнопок эмуляторов (по платформам).
// Дедолт = текущие встроенные раскладки. Конфиг на SD /retro.cfg.
//
// Значение привязки: uint16_t
//   [15:8] = модификаторы (REMAP_MOD_SHIFT и т.п.)
//   [7:0]  = HID-сканкод клавиатуры (0 = не назначено)
#include <stdint.h>
#include <string.h>
#include "remap.h"
#include "usb_kbd.h"
#include "fb_text.h"
#include "fat.h"
#include "uart.h"
#include "h3_hs_timer.h"

#define KBD_Q 20
#define KBD_W 26
#define KBD_E 8
#define KBD_R 21
#define KBD_T 23
#define KBD_Y 28
#define KBD_U 24
#define KBD_I 12
#define KBD_O 18
#define KBD_P 19
#define KBD_A 4
#define KBD_S 22
#define KBD_D 7
#define KBD_F 9
#define KBD_G 10
#define KBD_H 11
#define KBD_J 13
#define KBD_K 14
#define KBD_L 15
#define KBD_Z 29
#define KBD_X 27
#define KBD_C 6
#define KBD_V 25
#define KBD_B 5
#define KBD_N 17
#define KBD_M 16
#define KBD_SPACE 44
#define KBD_ENTER 40
#define KBD_ESC 41
#define KBD_UP 82
#define KBD_DOWN 81
#define KBD_LEFT 80
#define KBD_RIGHT 79
#define KBD_1 30
#define KBD_2 31
#define KBD_3 32

// ================= ИМЕНА КЛАВИШ =================
// HID-сканкод -> имя для retro.cfg и меню. Имена регистронезависимы.
typedef struct { uint8_t sc; const char* name; } key_name_t;

static const key_name_t key_names[] = {
    {4, "A"}, {5, "B"}, {6, "C"}, {7, "D"}, {8, "E"}, {9, "F"}, {10, "G"},
    {11, "H"}, {12, "I"}, {13, "J"}, {14, "K"}, {15, "L"}, {16, "M"}, {17, "N"},
    {18, "O"}, {19, "P"}, {20, "Q"}, {21, "R"}, {22, "S"}, {23, "T"}, {24, "U"},
    {25, "V"}, {26, "W"}, {27, "X"}, {28, "Y"}, {29, "Z"},
    {30, "1"}, {31, "2"}, {32, "3"}, {33, "4"}, {34, "5"}, {35, "6"},
    {36, "7"}, {37, "8"}, {38, "9"}, {39, "0"},
    {40, "ENTER"}, {41, "ESC"}, {42, "BACKSPACE"}, {43, "TAB"}, {44, "SPACE"},
    {45, "MINUS"}, {46, "EQUALS"}, {47, "LBRACKET"}, {48, "RBRACKET"},
    {49, "BACKSLASH"}, {51, "SEMICOLON"}, {52, "APOSTROPHE"}, {53, "GRAVE"},
    {54, "COMMA"}, {55, "PERIOD"}, {56, "SLASH"}, {57, "CAPSLOCK"},
    {58, "F1"}, {59, "F2"}, {60, "F3"}, {61, "F4"}, {62, "F5"}, {63, "F6"},
    {64, "F7"}, {65, "F8"}, {66, "F9"}, {67, "F10"}, {68, "F11"}, {69, "F12"},
    {70, "PRINTSCREEN"}, {71, "SCROLLLOCK"}, {72, "PAUSE"},
    {73, "INSERT"}, {74, "HOME"}, {75, "PAGEUP"}, {76, "DELETE"},
    {77, "END"}, {78, "PAGEDOWN"},
    {79, "RIGHT"}, {80, "LEFT"}, {81, "DOWN"}, {82, "UP"},
    {83, "NUMLOCK"}, {84, "KP_DIVIDE"}, {85, "KP_MULTIPLY"},
    {86, "KP_MINUS"}, {87, "KP_PLUS"}, {88, "KP_ENTER"},
    {89, "KP1"}, {90, "KP2"}, {91, "KP3"}, {92, "KP4"}, {93, "KP5"},
    {94, "KP6"}, {95, "KP7"}, {96, "KP8"}, {97, "KP9"}, {98, "KP0"},
    {99, "KP_PERIOD"},
    {224, "LCTRL"}, {225, "LSHIFT"}, {226, "LALT"}, {227, "LGUI"},
    {228, "RCTRL"}, {229, "RSHIFT"}, {230, "RALT"}, {231, "RGUI"},
    {0, NULL}
};

// скан-код -> имя (NULL если нет имени)
static const char* key_sc_to_name(uint8_t sc) {
    for (const key_name_t* k = key_names; k->name; k++)
        if (k->sc == sc) return k->name;
    return NULL;
}

// имя -> скан-код (0 если не нашли). Регистронезависимо.
static uint8_t key_name_to_sc(const char* name) {
    if (!name || !*name) return 0;
    for (const key_name_t* k = key_names; k->name; k++) {
        const char* a = name;
        const char* b = k->name;
        for (; *a && *b; a++, b++) {
            char ca = *a, cb = *b;
            if (ca >= 'a' && ca <= 'z') ca -= 32;
            if (cb >= 'a' && cb <= 'z') cb -= 32;
            if (ca != cb) break;
        }
        if (!*a && !*b) return k->sc;
    }
    return 0;
}

// ================= ДЕФОЛТНЫЕ РАСКЛАДКИ (текущие встроенные) =================
// MD 6-кнопочный: D-Pad + A B C X Y Z + Start + Mode
static const uint16_t def_md[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_Z, [BTN_B]=KBD_X, [BTN_C]=KBD_C, [BTN_X]=KBD_A, [BTN_Y]=KBD_S, [BTN_Z]=KBD_D,
    [BTN_START]=KBD_ENTER, [BTN_MODE]=KBD_Q,
};
// SMS: D-Pad + Btn1 A + Btn2 B + Pause (корпусная) + Start
static const uint16_t def_sms[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_Z, [BTN_B]=KBD_X,
    [BTN_START]=KBD_ENTER, [BTN_PAUSE]=KBD_S,   // S = Pause на корпусе SMS
};
// GG: как SMS, но Pause на корпусе нет — Start; Pause = кнопка паузы в ядре
static const uint16_t def_gg[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_Z, [BTN_B]=KBD_X,
    [BTN_START]=KBD_ENTER, [BTN_PAUSE]=KBD_S,
};
// SNES: D-Pad + A B X Y L R + Start Select. L/R — через Shift (реальных кнопок 12).
static const uint16_t def_snes[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_A, [BTN_B]=KBD_Z, [BTN_X]=KBD_S, [BTN_Y]=KBD_X,
    [BTN_L]=KBD_Q, [BTN_R]=KBD_W,
    [BTN_START]=KBD_ENTER, [BTN_SELECT]=KBD_SPACE,
};
// NES: D-Pad + A B + Start Select
static const uint16_t def_nes[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_Z, [BTN_B]=KBD_X,
    [BTN_START]=KBD_ENTER, [BTN_SELECT]=KBD_S,
};
// GB/GBC: D-Pad + A B + Start Select
static const uint16_t def_gb[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_X, [BTN_B]=KBD_Z,
    [BTN_START]=KBD_ENTER, [BTN_SELECT]=KBD_S,
};
// GBA: D-Pad + A B L R + Start Select
static const uint16_t def_gba[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_Z, [BTN_B]=KBD_X, [BTN_L]=KBD_Q, [BTN_R]=KBD_W,
    [BTN_START]=KBD_ENTER, [BTN_SELECT]=KBD_S,
};
// Lynx: D-Pad + A B + Option1/2 + Pause
static const uint16_t def_lynx[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_Z, [BTN_B]=KBD_X,
    [BTN_OPT1]=KBD_S, [BTN_OPT2]=KBD_ENTER, [BTN_PAUSE]=KBD_P,
};
// NGP/NGPC: Stick + A B + Option(Select) + Start
static const uint16_t def_ngp[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_Z, [BTN_B]=KBD_X,
    [BTN_START]=KBD_ENTER, [BTN_SELECT]=KBD_S,
};
// A2600: Stick + Fire + корпусные Reset/Select/Difficulty
static const uint16_t def_a2600[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_FIRE]=KBD_Z, [BTN_SELECT]=KBD_S, [BTN_RESET]=KBD_ENTER, [BTN_DIFF]=KBD_Q,
};
// A5200: Stick + Fire1/Fire2 + Start/Pause/Reset + Keypad
static const uint16_t def_a5200[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_FIRE]=KBD_Z, [BTN_FIRE2]=KBD_X,
    [BTN_START]=KBD_S, [BTN_PAUSE]=KBD_P, [BTN_KEY3]=KBD_ENTER,
};
// A7800: Stick + B1/B2 + Start + Select + корпусные Reset/Diff
static const uint16_t def_a7800[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_Z, [BTN_B]=KBD_X,
    [BTN_START]=KBD_ENTER, [BTN_SELECT]=KBD_S,
};
// Vectrex: аналоговый стик (4 напр.) + 4 кнопки (1/2/3/4)
static const uint16_t def_vectrex[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_Z, [BTN_B]=KBD_X, [BTN_X]=KBD_C, [BTN_Y]=KBD_V,
};
// ColecoVision: D-Pad + Fire1/Fire2 + Start(8) + #(Mode)
static const uint16_t def_coleco[BTN_MAX] = {
    [BTN_UP]=KBD_UP, [BTN_DOWN]=KBD_DOWN, [BTN_LEFT]=KBD_LEFT, [BTN_RIGHT]=KBD_RIGHT,
    [BTN_A]=KBD_Z, [BTN_B]=KBD_X,
    [BTN_START]=KBD_ENTER, [BTN_SELECT]=KBD_S,
};

// Метки кнопок для меню (показываем только уместные для платформы)
static const char* btn_labels[BTN_MAX] = {
    "Up", "Down", "Left", "Right",
    "A", "B", "C", "X", "Y", "Z",
    "Start", "Select", "Mode", "L", "R",
    "Reset", "Difficulty", "Pause", "Key 3", "Option 1", "Option 2", "Fire", "Fire 2",
};

// ================= ТАБЛИЦА ПЛАТФОРМ =================
typedef struct {
    const char* name;         // для меню
    const char* key;          // ключ в /retro.cfg
    const uint16_t* def;      // дефолтные привязки (BTN_MAX)
} plat_spec_t;

static const plat_spec_t plat_specs[REMAP_PLAT_COUNT] = {
    [REMAP_PLAT_MD]    = { "Mega Drive / Genesis", "md",     def_md    },
    [REMAP_PLAT_SMS]   = { "SMS",                   "sms",    def_sms   },
    [REMAP_PLAT_GG]    = { "Game Gear",             "gg",     def_gg    },
    [REMAP_PLAT_SNES]  = { "SNES",                  "snes",   def_snes  },
    [REMAP_PLAT_NES]   = { "NES / Famicom",         "nes",    def_nes   },
    [REMAP_PLAT_GB]    = { "Game Boy / GBC",        "gb",     def_gb    },
    [REMAP_PLAT_GBA]   = { "GBA",                   "gba",    def_gba   },
    [REMAP_PLAT_LYNX]  = { "Atari Lynx",            "lynx",   def_lynx  },
    [REMAP_PLAT_NGP]   = { "NGP / NGPC",            "ngp",    def_ngp   },
    [REMAP_PLAT_A2600] = { "Atari 2600",            "a2600",  def_a2600 },
    [REMAP_PLAT_A5200] = { "Atari 5200",            "a5200",  def_a5200 },
    [REMAP_PLAT_A7800] = { "Atari 7800",            "a7800",  def_a7800 },
    [REMAP_PLAT_VECTREX] = { "GCE Vectrex",         "vectrex", def_vectrex },
    [REMAP_PLAT_COLECO]  = { "ColecoVision",         "coleco",  def_coleco },
};

static const char* btn_keys[BTN_MAX] = {
    "up","down","left","right","a","b","c","x","y","z",
    "start","select","mode","l","r","reset","diff","pause","key3","opt1","opt2","fire","fire2"
};

// Текущая конфигурация: [платформа][кнопка] = (mod<<8)|scancode
static uint16_t g_map[REMAP_PLAT_COUNT][BTN_MAX];

// ================= API =================
void remap_defaults(void) {
    for (int p = 0; p < REMAP_PLAT_COUNT; p++)
        memcpy(g_map[p], plat_specs[p].def, sizeof(g_map[p]));
}

uint16_t remap_get(int plat, int btn) {
    if (plat < 0 || plat >= REMAP_PLAT_COUNT) return 0;
    if (btn < 0 || btn >= BTN_MAX) return 0;
    return g_map[plat][btn];
}

uint8_t remap_get_sc(int plat, int btn) {
    return (uint8_t)(remap_get(plat, btn) & 0xFF);
}

void remap_set(int plat, int btn, uint16_t val) {
    if (plat < 0 || plat >= REMAP_PLAT_COUNT) return;
    if (btn < 0 || btn >= BTN_MAX) return;
    g_map[plat][btn] = val;
}

// Нажата ли кнопка? Проверяет скан-код и модификатор (Shift и т.п.).
int remap_kbd_pressed(int plat, int btn, const uint8_t* keys, int n) {
    uint16_t v = remap_get(plat, btn);
    if (!v) return 0;
    uint8_t sc = (uint8_t)(v & 0xFF);
    int m = v >> 8;
    // проверка модификатора
    if (m & REMAP_MOD_SHIFT) {
        uint8_t mods = usb_kbd_get_mods();
        if (!(mods & (0x02 | 0x20))) return 0;   // LShift|RShift
    }
    for (int i = 0; i < n; i++)
        if (keys[i] == sc) return 1;
    return 0;
}

// ================= Конфиг /retro.cfg =================
// Формат: 'платформа.кнопка=сканкод' или '...=SHIFT+сканкод'. Строка на привязку.
// Поддерживаются:
//   - пустые строки (пропускаются)
//   - комментарии: '#' до конца строки (можно и на всю строку, и после значения)
//   - пробелы вокруг '=' и вокруг значения (обрезаются)
static void remap_parse_line(char* line) {
    // обрезаем комментарий: '#' до конца строки
    for (char* p = line; *p; p++) if (*p == '#') { *p = 0; break; }

    // ищем '=' (вне пробелов)
    char* eq = 0;
    for (char* p = line; *p; p++) if (*p == '=') { eq = p; break; }
    if (!eq || eq == line) return;

    // правый конец ключа: убрать пробелы перед '='
    char* key_end = eq;
    while (key_end > line && (key_end[-1] == ' ' || key_end[-1] == '\t')) key_end--;
    *key_end = 0;

    // значение: пропустить пробелы после '='
    char* val = eq + 1;
    while (*val == ' ' || *val == '\t') val++;
    if (!*val) return;

    // опционально "SHIFT+"
    int mod = 0;
    if (!memcmp(val, "SHIFT+", 6)) { mod |= REMAP_MOD_SHIFT; val += 6; }
    while (*val == ' ' || *val == '\t') val++;

    // значение: скан-код числом ИЛИ именем клавиши (A, ENTER, UP, F1...)
    int sc = 0;
    if (*val >= '0' && *val <= '9') {
        for (const char* p = val; *p && *p >= '0' && *p <= '9'; p++)
            sc = sc * 10 + (*p - '0');
    } else {
        // имя клавиши до конца строки (или пробела)
        char tmp[16];
        int tl = 0;
        for (const char* p = val; *p && *p != ' ' && *p != '\t' && tl < 15; p++)
            tmp[tl++] = *p;
        tmp[tl] = 0;
        sc = key_name_to_sc(tmp);
    }
    if (sc <= 0 || sc > 255) return;

    // ключ: платформа.кнопка (уже без хвостовых пробелов, key_end=0-terminated)
    const char* key = line;
    const char* dot = 0;
    for (const char* p = key; *p; p++) if (*p == '.') { dot = p; break; }
    if (!dot) return;
    int plen = (int)(dot - key);
    int blen = (int)((key_end - dot) - 1);
    int plat = -1, btn = -1;
    for (int i = 0; i < REMAP_PLAT_COUNT; i++)
        if ((int)strlen(plat_specs[i].key) == plen && !memcmp(key, plat_specs[i].key, plen)) { plat = i; break; }
    for (int i = 0; i < BTN_MAX; i++)
        if ((int)strlen(btn_keys[i]) == blen && !memcmp(dot + 1, btn_keys[i], blen)) { btn = i; break; }
    if (plat >= 0 && btn >= 0) g_map[plat][btn] = (uint16_t)((mod << 8) | sc);
}

void remap_load(void) {
    remap_defaults();
    fat_entry_t f;
    if (!fat_find("/", "retro.cfg", &f)) return;
    if (!f.size || f.size > 4096) return;
    static char cfg[4096];
    int r = fat_read_file(&f, 0, (uint8_t*)cfg, f.size);
    if (r <= 0) return;
    cfg[r] = 0;
    char* line = cfg;
    while (line && *line) {
        char* nl = line;
        while (*nl && *nl != '\n') nl++;
        int has_nl = (*nl == '\n');
        *nl = 0;
        int l = (int)strlen(line);
        if (l > 0 && line[l-1] == '\r') line[l-1] = 0;
        remap_parse_line(line);
        if (!has_nl) break;
        line = nl + 1;
    }
}

static void write_uint(char* d, int* pos, int v) {
    if (v >= 100) d[(*pos)++] = '0' + v/100;
    if (v >= 10)  d[(*pos)++] = '0' + (v/10)%10;
    d[(*pos)++] = '0' + v%10;
}

void remap_save(void) {
    char buf[1024];
    int pl = 0;
    for (int p = 0; p < REMAP_PLAT_COUNT; p++) {
        for (int b = 0; b < BTN_MAX; b++) {
            uint16_t v = g_map[p][b];
            if (v == plat_specs[p].def[b]) continue;
            if (!v && !plat_specs[p].def[b]) continue;   // оба пустые — не пишем
            if (pl > 900) break;
            const char* k = plat_specs[p].key; while (*k && pl < 1023) buf[pl++] = *k++;
            buf[pl++] = '.';
            k = btn_keys[b]; while (*k && pl < 1023) buf[pl++] = *k++;
            buf[pl++] = '=';
            int mod = v >> 8;
            if (mod & REMAP_MOD_SHIFT) {
                const char* s = "SHIFT+"; while (*s && pl < 1023) buf[pl++] = *s++;
            }
            // значение: имя клавиши (A, ENTER, UP...) вместо числа — понятнее
            const char* nm = key_sc_to_name((uint8_t)(v & 0xFF));
            if (nm) {
                const char* s = nm; while (*s && pl < 1023) buf[pl++] = *s++;
            } else {
                write_uint(buf, &pl, (int)(v & 0xFF));
            }
            buf[pl++] = '\n';
        }
    }
    if (pl == 0) {
        fat_delete_file("/", "retro.cfg");
        return;
    }
    fat_write_file("/", "retro.cfg", (const uint8_t*)buf, (uint32_t)pl);
}

// ================= Меню настройки =================
#define PHYS_W 1024
#define PHYS_H 600
#define FOOTER_Y (PHYS_H - 30)
#define LIST_TOP 100
#define ROW_H 26

static void draw_plat_list(int sel) {
    fb_draw_stars();
    fb_puts_s(60, 40, "Keyboard remap: platform", 2, 0x00FF0000);
    fb_fill_rect(60, 70, 400, 2, 0x00FFFFFF);
    int y = LIST_TOP;
    for (int p = 0; p < REMAP_PLAT_COUNT; p++) {
        uint32_t clr = (p == sel) ? 0x00FFFF00 : 0x00FFFFFF;
        if (p == sel) fb_fill_rect(50, y - 4, PHYS_W - 100, ROW_H, 0x00181818);
        char line[80]; int n = 0;
        const char* nm = plat_specs[p].name;
        while (*nm && n < 60) line[n++] = *nm++;
        while (n < 44) line[n++] = ' ';
        // счётчик неназначенных кнопок
        int cnt = 0;
        for (int b = 0; b < BTN_MAX; b++) if (g_map[p][b] & 0xFF) cnt++;
        line[n++] = '['; 
        if (cnt >= 10) line[n++] = '0' + cnt/10;
        line[n++] = '0' + cnt%10;
        line[n++] = ']'; line[n] = 0;
        fb_puts_s(70, y, line, 1, clr);
        y += ROW_H;
        if (y > 570) break;
    }
    fb_puts(60, FOOTER_Y, "  ^v: select    Enter: buttons    ESC: back", 0x00888888);
    fb_flush();
}

static void draw_btn_list(int plat, int sel) {
    fb_draw_stars();
    char title[80]; int tn = 0;
    const char* nm = plat_specs[plat].name;
    while (*nm && tn < 60) title[tn++] = *nm++;
    while (tn < 64) title[tn++] = ' ';
    const char* h = " [keys]"; while (*h && tn < 76) title[tn++] = *h++;
    title[tn] = 0;
    fb_puts_s(60, 40, title, 2, 0x00FF0000);
    fb_fill_rect(60, 70, 400, 2, 0x00FFFFFF);

    int y = LIST_TOP;
    for (int b = 0; b < BTN_MAX; b++) {
        if (!btn_labels[b]) continue;
        uint16_t v = g_map[plat][b];
        // пропускаем совсем неиспользуемые (нет дефолта и не назначено и нет метки "Fire2" для не-5200)
        if (!v && !plat_specs[plat].def[b]) continue;
        uint32_t clr = (b == sel) ? 0x00FFFF00 : 0x00FFFFFF;
        if (b == sel) fb_fill_rect(50, y - 4, PHYS_W - 100, ROW_H, 0x00181818);
        char line[80]; int n = 0;
        const char* lb = btn_labels[b];
        while (*lb && n < 30) line[n++] = *lb++;
        while (n < 34) line[n++] = ' ';
        line[n++] = ':';
        uint8_t sc = (uint8_t)(v & 0xFF);
        if (v >> 8 & REMAP_MOD_SHIFT) {
            const char* s = " Shift+"; while (*s && n < 70) line[n++] = *s++;
        } else line[n++] = ' ';
        if (sc) {
            line[n++] = '(';
            const char* kn = key_sc_to_name(sc);
            if (kn) {
                const char* s = kn; while (*s && n < 70) line[n++] = *s++;
            } else {
                if (sc >= 100) line[n++] = '0' + sc/100;
                if (sc >= 10)  line[n++] = '0' + (sc/10)%10;
                line[n++] = '0' + sc%10;
            }
            line[n++] = ')';
        } else {
            line[n++] = '-'; line[n++] = '-';
        }
        line[n] = 0;
        fb_puts_s(70, y, line, 1, clr);
        y += ROW_H;
        if (y > 570) break;
    }
    fb_puts(60, FOOTER_Y, "  ^v: select    Enter: set key    ESC: back", 0x00888888);
    fb_flush();
}

// Ждать отпускания всех клавиш, затем прочитать нажатие. Возвращает (mod<<8)|sc, 0 = ESC.
static uint16_t capture_key(void) {
    uint8_t keys[8];
    for (uint32_t g = 0; g < 500000; g++) {
        int n = usb_kbd_get_raw(keys, 8);
        int any = 0;
        for (int i = 0; i < n && i < 8; i++) if (keys[i]) { any = 1; break; }
        if (!any) break;
        udelay(5000);
    }
    for (;;) {
        int k = usb_kbd_poll();
        if (k) {
            uint8_t mods = usb_kbd_get_mods();
            if (k == KBD_ESC) return 0;
            return (uint16_t)(((mods & (0x02|0x20)) ? REMAP_MOD_SHIFT : 0) << 8 | k);
        }
        udelay(5000);
    }
}

void remap_menu(void) {
    int sel_plat = 0;
    int mode = 0;   // 0 = список платформ, 1 = список кнопок
    int sel_btn = 0;

    for (;;) {
        if (!mode) {
            draw_plat_list(sel_plat);
            int k = usb_kbd_poll();
            if (!k) { udelay(16000); continue; }
            if (k == KBD_UP && sel_plat > 0) sel_plat--;
            else if (k == KBD_DOWN && sel_plat < REMAP_PLAT_COUNT - 1) sel_plat++;
            else if (k == KBD_ENTER) { mode = 1; sel_btn = 0; }
            else if (k == KBD_ESC) return;
        } else {
            draw_btn_list(sel_plat, sel_btn);
            int k = usb_kbd_poll();
            if (!k) { udelay(16000); continue; }
            if (k == KBD_UP) { do { sel_btn--; } while (sel_btn > 0 && !btn_labels[sel_btn]); if (sel_btn < 0) sel_btn = 0; }
            else if (k == KBD_DOWN) { do { sel_btn++; } while (sel_btn < BTN_MAX - 1 && !btn_labels[sel_btn]); }
            else if (k == KBD_ENTER) {
                fb_clear();
                fb_puts_s(60, 200, "Press key (Shift = Shift+key)", 2, 0x00FFAA00);
                char line[80]; int n = 0;
                const char* lb = btn_labels[sel_btn];
                while (*lb && n < 60) line[n++] = *lb++;
                fb_puts_s(60, 240, line, 2, 0x00FFFFFF);
                fb_puts_s(60, 300, "ESC = clear", 1, 0x00888888);
                fb_flush();
                uint16_t v = capture_key();
                if (v) g_map[sel_plat][sel_btn] = v;
                else   g_map[sel_plat][sel_btn] = 0;
                remap_save();
            }
            else if (k == KBD_ESC) mode = 0;
        }
        udelay(40000);
    }
}