/* System layer for the Atari Portfolio (8088 palmtop) on pico-retro RP2040.
 *
 * Ports: 0x8000 keyboard, 0x8010/11 HD61830 LCD, 0x8020 DTMF, 0x8030 power,
 *        0x8040/41 counter, 0x8050 IRQ, 0x8051 battery/select, 0x8060 contrast.
 * Memory: RAM 128K (reused from InfoNES DoubleFrame), ROM 256K in flash
 *         (cart bank + BIOS), mirrored VRAM window 0xB0000-0xBFFFF.
 *
 * LCD: Hitachi HD61830 (from MAME), 240x64, 2-colour. Rendered centred.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define LCD_OFF_RGB RGB565(17, 48, 21)
#define LCD_ON_RGB  RGB565(8, 17, 18)

#include "pofo_compat_h3.h"
#include "pofo_rom.h"
#include "pofo_chargen.h"
#include "pofo_chargen_ru.h"
#include "intf.h"

/* Static memory pool — the 8088 needs a 128K RAM image.
 * On H3 we simply allocate one contiguous 128K block (plenty of DRAM). */
static uint8_t pofo_ram[0x20000];   /* 128K: 124K main + 4K VRAM tail */
uint8_t *LORAM = pofo_ram;

/* ------------------------------------------------------------------ */
/* ROM image (256K): [0..128K) = cartridge bank, [128K..256K) = BIOS.  */
/* The embedded BIOS stub lives in pofo_rom.h (flash, XIP).            */
/* ------------------------------------------------------------------ */
extern "C" uint8_t pofo_rom_read(uint32_t off)
{
    if (off >= 0x40000) return 0;
    return pofo_rom[off];
}

/* ------------------------------------------------------------------ */
/* RAM / VRAM access (called from cpu.cpp). RAM 0..0x1EFFF -> segments,*/
/* RAM 0x1F000..0x1FFFF (VRAM tail) -> pofo_vram.                      */
/* ------------------------------------------------------------------ */
extern "C" unsigned char read_ram(int address)
{
    if (address >= 0 && address < (int)sizeof(pofo_ram))
        return pofo_ram[address];
    return 0;
}

extern "C" void write_ram(int address, unsigned char val)
{
    if (address >= 0 && address < (int)sizeof(pofo_ram))
        pofo_ram[address] = val;
}

extern "C" uint8_t VRAM_read(uint32_t addr32)
{
    uint32_t off = 0x1F000 + addr32;
    if (off < sizeof(pofo_ram)) return pofo_ram[off];
    return 0;
}

extern "C" void VRAM_write(uint32_t addr32, uint8_t value)
{
    uint32_t off = 0x1F000 + addr32;
    if (off < sizeof(pofo_ram)) pofo_ram[off] = value;
}

/* ------------------------------------------------------------------ */
/* HD61830 LCD controller (simplified from MAME hd61830.cpp)           */
/* ------------------------------------------------------------------ */
enum {
    INSTR_MODE_CONTROL = 0,
    INSTR_CHARACTER_PITCH,
    INSTR_NUMBER_OF_CHARACTERS,
    INSTR_NUMBER_OF_TIME_DIVISIONS,
    INSTR_CURSOR_POSITION,
    INSTR_DISPLAY_START_LOW = 8,
    INSTR_DISPLAY_START_HIGH,
    INSTR_CURSOR_ADDRESS_LOW,
    INSTR_CURSOR_ADDRESS_HIGH,
    INSTR_DISPLAY_DATA_WRITE,
    INSTR_DISPLAY_DATA_READ,
    INSTR_CLEAR_BIT,
    INSTR_SET_BIT
};

#define HD61830_VRAM_SIZE 0x800

#define BIT(x, n) (((x) >> (n)) & 1)

struct hd61830_t {
    uint8_t ir;              /* instruction register */
    uint8_t mcr;             /* mode control register */
    uint8_t dor;             /* data output register */
    uint16_t dsa;            /* display start address */
    uint16_t cac;            /* cursor address counter */
    int vp, hp;              /* vertical / horizontal character pitch */
    int hn;                  /* horizontal number of characters */
    int nx;                  /* number of time divisions */
    int cp;                  /* cursor position */
    int blink, cursor;
    uint8_t vram[HD61830_VRAM_SIZE];
} lcdc;

static uint8_t hd61830_rd(int offset);
static void hd61830_wr(int offset, uint8_t data);

/* ------------------------------------------------------------------ */
/* ASIC ports (from pofo_asic.cpp)                                     */
/* ------------------------------------------------------------------ */
static uint8_t m_ie = 0;          /* interrupt enable */
static uint8_t m_ip = 0;          /* interrupt pending */
static uint32_t m_counter = 0;
static uint8_t m_contrast = 0x80;
static int m_rom_b = 0;           /* ROM bank select */
static bool m_sleep = false;

static uint8_t m_kbd_data = 0xFF;
static uint8_t m_kop_state[8] = { 0 };

/* Keyboard matrix (8 rows x 8 columns). Scancodes:
 * 0x00..0x3F = make (row*8+col), 0x80|code = break. */
static uint8_t kbd_scan = 0xFF;
static uint8_t kbd_pending = 0xFF;

static void key_make(uint8_t row, uint8_t col)
{
    kbd_scan = row * 8 + col;
    kbd_pending = kbd_scan;
    m_kbd_data = kbd_scan;          /* BIOS читает сканкод из порта 0x8000 */
    if (m_sleep) { m_sleep = false; }
    m_ip |= 1 << 1;   /* INT_KEYBOARD — взводим в ASIC */
    doirq(1);         /* → INT 09h через i8259 (Fake86) */
}

static void key_break(uint8_t row, uint8_t col)
{
    kbd_scan = 0x80 | (row * 8 + col);
    kbd_pending = kbd_scan;
    m_kbd_data = kbd_scan;          /* break-сканкод (bit 7=1) */
    m_ip |= 1 << 1;
    doirq(1);
}

static uint8_t asic_keyboard_r(void)
{
    /* порт 0x8000: BIOS INT 16h читает последний сканкод из m_kbd_data */
    uint8_t d = m_kbd_data;
    return d;
}

static uint8_t asic_irq_status_r(void)
{
    /* порт 0x8050: бит 3=NMD1 (1=нет второго дисковода), биты 0-2=запросы IRQ */
    uint8_t d = m_ip;
    d |= 1 << 3;   /* NMD1 high -> no second floppy */
    return d;
}

static void asic_irq_mask_w(uint8_t data)
{
    m_ie = data;
}

static uint8_t asic_battery_r(void)
{
    /* порт 0x8051: батарея + ROM-bank + warm/cold boot.
     * Биты 0,2: m_rom_b (банк), бит 5: периферия, бит 6: LOWB=норма,
     * бит 7: warm boot. Всегда норма — без батарей DIP DOS не стартует. */
    uint8_t d = m_rom_b & 0x05;
    d |= 0 << 5;          /* no peripheral */
    d |= (0x01 & 0x03) << 6;   /* battery normal (LOWB=1) */
    d |= 0x00 << 6;            /* warm boot */
    return d | 0x40;      /* battery OK */
}

static void asic_select_w(uint8_t data)
{
    m_rom_b = data & 0x0f;
}

static void asic_power_w(uint8_t data)
{
    if (data & 0x02) m_sleep = true;
}

static uint8_t asic_counter_r(int offset)
{
    return (offset == 0) ? (m_counter & 0xFF) : ((m_counter >> 8) & 0xFF);
}

static void asic_counter_w(int offset, uint8_t data)
{
    if (offset == 0) m_counter = (m_counter & 0xFFFFFF00) | data;
    else m_counter = (m_counter & 0xFFFF00FF) | ((uint32_t)data << 8);
}

/* ------------------------------------------------------------------ */
/* Keyboard matrix -> scancodes for Portfolio.                         */
/* Layout (from MAME pofo.cpp, Y0..Y7 rows, bit 0..7 columns):        */
/*   Y3: bit5=Up, Y4: bit5=Down, Y5: bit3=Left, bit4=Right,           */
/*   Y6: bit4=Space(Enter), Y1: bit7=Backspace(9), Y7: bit7=Esc,       */
/*   Y3: bit4='[' (Start), Y6: bit6=Fn(Select)                         */
/* We map: d-pad arrows, A=Enter, B=Backspace, Start=Fn, Select=Esc.  */
/* ------------------------------------------------------------------ */
/* Virtual on-screen keyboard (8 buttons -> full text entry for DIP DOS)*/
/* Each key: label char + Portfolio (row,col) scancode + shift flag. */
struct vk_key_t { char label; uint8_t row, col; uint8_t shift; };

#define VK_ROWS 5
#define VK_COLS 12
static const struct vk_key_t vk_keys[VK_ROWS][VK_COLS] = {
    { {'1',0,2,0},{'2',0,3,0},{'3',0,4,0},{'4',4,2,0},{'5',0,6,0},{'6',0,7,0},{'7',1,5,0},{'8',5,6,0},{'9',1,7,0},{'0',3,0,0},{'-',3,2,0},{'=',6,5,0} },
    { {'q',1,2,0},{'w',2,1,0},{'e',2,3,0},{'r',2,4,0},{'t',2,5,0},{'y',2,7,0},{'u',1,3,0},{'i',3,1,0},{'o',1,4,0},{'p',4,1,0},{'[',3,4,0},{']',3,7,0} },
    { {'a',7,6,0},{'s',4,0,0},{'d',0,5,0},{'f',5,0,0},{'g',4,3,0},{'h',5,1,0},{'j',5,2,0},{'k',5,7,0},{'l',4,7,0},{';',6,3,0},{'\'',3,6,0},{'\\',6,0,0} },
    { {'z',6,1,0},{'x',6,7,0},{'c',7,0,0},{'v',7,1,0},{'b',7,2,0},{'n',7,3,0},{'m',7,4,0},{',',4,6,0},{'.',6,4,0},{'/',7,5,0},{':',6,3,1},{'@',0,3,1} },
    { {' ',6,2,0},{' ',6,2,0},{' ',6,2,0},{' ',6,2,0},{' ',6,2,0},{' ',6,2,0},{' ',6,2,0},{' ',6,2,0},{' ',6,2,0},{' ',6,2,0},{' ',6,2,0},{' ',6,2,0} },
};
static const char *vk_func_labels[VK_COLS] = {
    "Ent", "BS", "Spc", "Esc", "Sft", "RUS", "", "", "", "", "", ""
};

static int vk_visible = 0;
static int vk_cur_r = 0;
static int vk_cur_c = 0;
static int vk_shift = 0;      /* 0=none, 1=one-shot (green), 2=caps (blue) */
static int vk_dirty = 0;
static int vk_shift_oneshot = 0;
static int pofo_ru_mode = 0;   /* 0=ASCII, 1=CP866 Cyrillic */

/* Задел для отложенного break (make+exec86 в одном кадре, break в следующем).
 * Сейчас не используется — VK шлёт key_break сразу после exec86. */
static int pend_br_row = -1, pend_br_col = -1, pend_br_shift = 0;

static uint32_t pofo_boot_guard = 0;
static uint8_t pofo_kbd_cand = 0xFF, pofo_kbd_stable = 0xFF;
static uint8_t pofo_kbd_cnt = 0;
static uint8_t pofo_kbd_prev_cols[8] = { 0 };

/* Defined later (built-in apps); declared here for VK routing. */
static int pofo_apps_active = 0;
static int pofo_app_current = -1;
static void pofo_app_uart_char(int c);

static void vk_key_press(const struct vk_key_t *k)
{
    int sh_table = k->shift;
    int sh = sh_table || (vk_shift > 0);
    if (pofo_apps_active && pofo_app_current >= 0) {
        char ch = k->label;
        if (sh && ch >= 'a' && ch <= 'z') ch -= 32;
        pofo_app_uart_char(ch);
        if (vk_shift_oneshot) { vk_shift = 0; vk_shift_oneshot = 0; vk_dirty = 1; }
        return;
    }
    /* Прямая запись в BIOS flag byte 0x0040:0x0017 — единственный
     * гарантированный способ установить Shift/Caps без race с INT 09h. */
    uint32_t flag_addr = 0x00417;
    uint8_t flags = read86(flag_addr);
    if (sh) flags |= 0x01;
    else    flags &= ~0x01;
    if (vk_shift == 2) flags |= 0x40;   /* CapsLock */
    else               flags &= ~0x40;
    write86(flag_addr, flags);

    key_make(k->row, k->col);
    exec86(8000);              /* даём BIOS обработать INT 09h (make) */
    key_break(k->row, k->col);
    /* One-shot shift: после одной набранной клавиши сбрасываем */
    if (vk_shift == 1) {
        flags = read86(flag_addr);
        flags &= ~0x01;
        write86(flag_addr, flags);
        vk_shift = 0; vk_shift_oneshot = 0; vk_dirty = 1;
    }
}

static void vk_key_press_rc(int row, int col)
{
    /* make + exec + break в одной функции: VK не держит клавиши зажатыми
     * между кадрами, иначе BIOS залипнет на автоповторе. */
    key_make(row, col);
    exec86(8000);
    key_break(row, col);
}

/* ------------------------------------------------------------------ */
/* UART keyboard input: type on the PC (PuTTY @ 115200) and the chars  */
/* are injected into DIP DOS as Portfolio key presses.                 */
/* ------------------------------------------------------------------ */
static int pofo_ascii_key(char c, uint8_t *row, uint8_t *col, int *shift)
{
    *shift = 0;
    /* lowercase / digits / punctuation: base key */
    switch (c) {
    case '1': *row=0; *col=2; return 1;
    case '2': *row=0; *col=3; return 1;
    case '3': *row=0; *col=4; return 1;
    case '4': *row=4; *col=2; return 1;
    case '5': *row=0; *col=6; return 1;
    case '6': *row=0; *col=7; return 1;
    case '7': *row=1; *col=5; return 1;
    case '8': *row=5; *col=6; return 1;
    case '9': *row=1; *col=7; return 1;
    case '0': *row=3; *col=0; return 1;
    case 'q': *row=1; *col=2; return 1;
    case 'w': *row=2; *col=1; return 1;
    case 'e': *row=2; *col=3; return 1;
    case 'r': *row=2; *col=4; return 1;
    case 't': *row=2; *col=5; return 1;
    case 'y': *row=2; *col=7; return 1;
    case 'u': *row=1; *col=3; return 1;
    case 'i': *row=3; *col=1; return 1;
    case 'o': *row=1; *col=4; return 1;
    case 'p': *row=4; *col=1; return 1;
    case 'a': *row=7; *col=6; return 1;
    case 's': *row=4; *col=0; return 1;
    case 'd': *row=0; *col=5; return 1;
    case 'f': *row=5; *col=0; return 1;
    case 'g': *row=4; *col=3; return 1;
    case 'h': *row=5; *col=1; return 1;
    case 'j': *row=5; *col=2; return 1;
    case 'k': *row=5; *col=7; return 1;
    case 'l': *row=4; *col=7; return 1;
    case 'z': *row=6; *col=1; return 1;
    case 'x': *row=6; *col=7; return 1;
    case 'c': *row=7; *col=0; return 1;
    case 'v': *row=7; *col=1; return 1;
    case 'b': *row=7; *col=2; return 1;
    case 'n': *row=7; *col=3; return 1;
    case 'm': *row=7; *col=4; return 1;
    case ' ': *row=6; *col=2; return 1;
    case '\r': case '\n': *row=2; *col=6; return 1;   /* Enter */
    case '\b': case 0x7F: *row=1; *col=6; return 1;   /* Backspace */
    case '\t': *row=2; *col=0; return 1;              /* Tab */
    case 0x1B: *row=7; *col=7; return 1;              /* Esc */
    case '.': *row=6; *col=4; return 1;
    case ',': *row=4; *col=6; return 1;
    case '/': *row=7; *col=5; return 1;
    case ';': *row=6; *col=3; return 1;
    case ':': *shift=1; *row=6; *col=3; return 1;
    case '-': *row=3; *col=2; return 1;
    case '=': *row=6; *col=5; return 1;
    case '[': *row=3; *col=4; return 1;
    case ']': *row=3; *col=7; return 1;
    case '\\': *row=6; *col=0; return 1;
    case '\'': *row=3; *col=6; return 1;
    case '`': *row=3; *col=6; return 1;
    case '~': *row=0; *col=0; return 1;              /* dedicated Atari/~ key */
    /* shifted punctuation: Shift + base key */
    case '!': *shift=1; *row=0; *col=2; return 1;   /* Shift+1 */
    case '@': *shift=1; *row=0; *col=3; return 1;   /* Shift+2 */
    case '#': *shift=1; *row=0; *col=4; return 1;   /* Shift+3 */
    case '$': *shift=1; *row=4; *col=2; return 1;   /* Shift+4 */
    case '%': *shift=1; *row=0; *col=6; return 1;   /* Shift+5 */
    case '^': *shift=1; *row=0; *col=7; return 1;   /* Shift+6 */
    case '&': *shift=1; *row=1; *col=5; return 1;   /* Shift+7 */
    case '*': *shift=1; *row=5; *col=6; return 1;   /* Shift+8 */
    case '(': *shift=1; *row=1; *col=7; return 1;   /* Shift+9 */
    case ')': *shift=1; *row=3; *col=0; return 1;   /* Shift+0 */
    case '"': *shift=1; *row=3; *col=6; return 1;   /* Shift+' */
    case '{': *shift=1; *row=3; *col=4; return 1;   /* Shift+[ */
    case '}': *shift=1; *row=3; *col=7; return 1;   /* Shift+] */
    case '|': *shift=1; *row=6; *col=0; return 1;   /* Shift+\ */
    case '<': *shift=1; *row=4; *col=6; return 1;   /* Shift+, */
    case '>': *shift=1; *row=6; *col=4; return 1;   /* Shift+. */
    case '?': *shift=1; *row=7; *col=5; return 1;   /* Shift+/ */
    case '_': *shift=1; *row=3; *col=2; return 1;   /* Shift+- */
    case '+': *shift=1; *row=6; *col=5; return 1;   /* Shift+= */
    /* uppercase letters: explicit cases with shift (no recursion) */
    default:
        switch (c) {
        case 'A': *shift=1; *row=7; *col=6; return 1;   /* a */
        case 'B': *shift=1; *row=7; *col=2; return 1;   /* b */
        case 'C': *shift=1; *row=7; *col=0; return 1;   /* c */
        case 'D': *shift=1; *row=0; *col=5; return 1;   /* d */
        case 'E': *shift=1; *row=2; *col=3; return 1;   /* e */
        case 'F': *shift=1; *row=5; *col=0; return 1;   /* f */
        case 'G': *shift=1; *row=4; *col=3; return 1;   /* g */
        case 'H': *shift=1; *row=5; *col=1; return 1;   /* h */
        case 'I': *shift=1; *row=3; *col=1; return 1;   /* i */
        case 'J': *shift=1; *row=5; *col=2; return 1;   /* j */
        case 'K': *shift=1; *row=5; *col=7; return 1;   /* k */
        case 'L': *shift=1; *row=4; *col=7; return 1;   /* l */
        case 'M': *shift=1; *row=7; *col=4; return 1;   /* m */
        case 'N': *shift=1; *row=7; *col=3; return 1;   /* n */
        case 'O': *shift=1; *row=1; *col=4; return 1;   /* o */
        case 'P': *shift=1; *row=4; *col=1; return 1;   /* p */
        case 'Q': *shift=1; *row=1; *col=2; return 1;   /* q */
        case 'R': *shift=1; *row=2; *col=4; return 1;   /* r */
        case 'S': *shift=1; *row=4; *col=0; return 1;   /* s */
        case 'T': *shift=1; *row=2; *col=5; return 1;   /* t */
        case 'U': *shift=1; *row=1; *col=3; return 1;   /* u */
        case 'V': *shift=1; *row=7; *col=1; return 1;   /* v */
        case 'W': *shift=1; *row=2; *col=1; return 1;   /* w */
        case 'X': *shift=1; *row=6; *col=7; return 1;   /* x */
        case 'Y': *shift=1; *row=2; *col=7; return 1;   /* y */
        case 'Z': *shift=1; *row=6; *col=1; return 1;   /* z */
        default: return 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* PIN cracker — "Terminator 2" style (PIN.EXE / ATM scene).           */
/* Shows the iconic >TEST.0.0` / >ENTRY CODE / WATING... and cycles    */
/* a 4-digit code; ENTER exits back to DOS.                            */
/* ------------------------------------------------------------------ */
static int pofo_pin_active = 0;
static int pofo_pin_frame = 0;

static void pofo_run_pin(void)
{
    pofo_pin_active = 1;
    pofo_pin_frame = 0;
}

/* ------------------------------------------------------------------ */
/* Built-in application hub: `APPS` opens a menu of mini-programs      */
/* (Calculator, Text editor, Spreadsheet, Contacts, Diary) rendered in  */
/* the LCD style via the display API, like PIN. Navigation with   */
/* the joystick: Up/Down move, A/Start select, B/Select back.           */
/* ------------------------------------------------------------------ */
#define APPS_MAX 5
static int pofo_apps_sel = 0;
static int pofo_apps_wait_release = 0;

enum {
    APP_CALC = 0,
    APP_EDIT,
    APP_SHEET,
    APP_CONTACTS,
    APP_DIARY,
};

static const char *const pofo_app_names[APPS_MAX] = {
    "Calculator",
    "Text Editor",
    "Worksheet",
    "Address Book",
    "Diary",
};

/* which app is currently shown (APP_CALC..APP_DIARY) or -1 for the hub */

/* generic debounced pad edges shared by the apps */
/* joypad_buttons(): 0 = pressed. now=1 means "pressed". */
static int pofo_app_edge(uint8_t pad, uint8_t bit, uint32_t *stable, uint32_t *cnt)
{
    enum { DB = 3 };
    uint8_t now = (pad & bit) ? 0 : 1;
    if (now == *stable) {
        if (*cnt < DB) (*cnt)++;
    } else {
        *stable = now;
        *cnt = 0;
    }
    if (*cnt == DB && now) {   /* stable pressed -> one edge */
        *cnt = DB + 1;
        return 1;
    }
    return 0;
}

/* VRAM text helpers (defined later near the renderer); forward decls so the
 * built-in app screens can use them. */
static void pofo_put_text(int row, int col, const char *s);
static void pofo_clear_text(void);

static void pofo_apps_draw_menu(void)
{
    char buf[40];
    pofo_clear_text();
    pofo_put_text(0, 0, "PORTFOLIO APPS");
    for (int i = 0; i < APPS_MAX; i++) {
        snprintf(buf, sizeof(buf), "%c %s", (i == pofo_apps_sel) ? '>' : ' ',
                 pofo_app_names[i]);
        pofo_put_text(1 + i, 0, buf);
    }
    pofo_put_text(7, 0, "UP/DN SEL  A=OPEN  B=EXIT");
}

/* redraw whichever app is currently shown */
static void pofo_app_calc_draw(void);
static void pofo_app_edit_draw(void);
static void pofo_app_sheet_draw(void);
static void pofo_app_contacts_draw(void);
static void pofo_app_diary_draw(void);
static void pofo_apps_draw(void)
{
    switch (pofo_app_current) {
    case APP_CALC:     pofo_app_calc_draw();     break;
    case APP_EDIT:     pofo_app_edit_draw();     break;
    case APP_SHEET:    pofo_app_sheet_draw();    break;
    case APP_CONTACTS: pofo_app_contacts_draw(); break;
    case APP_DIARY:    pofo_app_diary_draw();    break;
    default:           pofo_apps_draw_menu();    break;
    }
}

static void pofo_run_apps(void)
{
    pofo_apps_active = 1;
    pofo_app_current = -1;
    pofo_apps_sel = 0;
    vk_visible = 0;              /* entering from the VK must not keep it open */
    pofo_apps_wait_release = 2;  /* wait a few frames for buttons to settle */
    pofo_apps_draw_menu();
}

/* ------------------------------------------------------------------ */
/* App handlers: each app exposes pofo_app_<name>_handle(pad) and      */
/* pofo_app_<name>_draw(). Edges are debounced via pofo_app_edge().    */
/* ------------------------------------------------------------------ */

/* ---- Calculator: own keypad 4x4, no VK needed. */
static long pofo_calc_acc = 0;
static long pofo_calc_cur = 0;
static int pofo_calc_op = 0;
static int pofo_calc_opset = 0;
static int pofo_calc_error = 0;
static int calc_kb_r = 2, calc_kb_c = 0;
static int calc_prev_r = -1, calc_prev_c = -1;

#define CALC_ROWS 4
#define CALC_COLS 4
/* Labels: top row = 7 8 9 /, then 4 5 6 *, 1 2 3 -, C 0 = + */
static const char *calc_keys[CALC_ROWS][CALC_COLS] = {
    {"7","8","9","/"},
    {"4","5","6","*"},
    {"1","2","3","-"},
    {"C","0","=","+"},
};

static void pofo_app_calc_keypress(const char *label)
{
    char key = label[0];
    if (key >= '0' && key <= '9') {
        if (pofo_calc_error) { pofo_calc_error = 0; pofo_calc_acc = 0; pofo_calc_cur = 0; }
        pofo_calc_cur = pofo_calc_cur * 10 + (key - '0');
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        int op = (key == '+') ? 0 : (key == '-') ? 1 : (key == '*') ? 2 : 3;
        if (pofo_calc_error) { pofo_calc_error = 0; pofo_calc_acc = 0; pofo_calc_cur = 0; return; }
        if (pofo_calc_opset && pofo_calc_cur != 0) {
            switch (pofo_calc_op) {
            case 0: pofo_calc_acc += pofo_calc_cur; break;
            case 1: pofo_calc_acc -= pofo_calc_cur; break;
            case 2: pofo_calc_acc *= pofo_calc_cur; break;
            case 3: if (pofo_calc_cur) pofo_calc_acc /= pofo_calc_cur; else pofo_calc_error = 1; break;
            }
        } else if (!pofo_calc_opset) {
            pofo_calc_acc = pofo_calc_cur;
        }
        pofo_calc_cur = 0; pofo_calc_op = op; pofo_calc_opset = 1;
    } else if (key == '=') {
        if (pofo_calc_error) { pofo_calc_error = 0; pofo_calc_acc = 0; pofo_calc_cur = 0; return; }
        if (!pofo_calc_opset && pofo_calc_cur == 0) return;
        long rhs = pofo_calc_cur;
        if (pofo_calc_opset) {
            switch (pofo_calc_op) {
            case 0: pofo_calc_acc += rhs; break;
            case 1: pofo_calc_acc -= rhs; break;
            case 2: pofo_calc_acc *= rhs; break;
            case 3: if (rhs == 0) pofo_calc_error = 1; else pofo_calc_acc /= rhs; break;
            }
        } else { pofo_calc_acc = rhs; }
        pofo_calc_cur = 0; pofo_calc_opset = 0;
    } else if (key == 'C') {
        pofo_calc_acc = 0; pofo_calc_cur = 0; pofo_calc_op = 0; pofo_calc_opset = 0; pofo_calc_error = 0;
    }
}

static void pofo_app_calc_draw(void)
{
    char buf[40];
    pofo_clear_text();
    pofo_put_text(0, 0, "  CALCULATOR");
    snprintf(buf, sizeof(buf), "%ld", pofo_calc_acc);
    int p = 32 - (int)strlen(buf);
    if (p < 0) p = 0;
    char line[33];
    memset(line, ' ', p);
    line[p] = 0;
    strcat(line, buf);
    pofo_put_text(1, 0, line);
    char opc = "+-*/"[pofo_calc_op];
    snprintf(buf, sizeof(buf), "%c %ld", opc, pofo_calc_cur);
    pofo_put_text(2, 0, buf);
    if (pofo_calc_error)
        pofo_put_text(4, 0, "  ERROR");
}

static void pofo_calc_draw_cell(int r, int c, int sel)
{
    int x = 108 + c * 26;
    int y = 134 + r * 22;
    uint16_t bg = sel ? RGB565(31,31,0) : RGB565(15,18,25);
    uint16_t fg = sel ? RGB565(0,0,0) : RGB565(22,26,32);
    display_fill_rect(x, y, 24, 20, bg);
    display_text_at_nobg(calc_keys[r][c], x + 8, y + 6, 1, fg);
}

static void pofo_app_calc_draw_keys(void)
{
    if (calc_prev_r < 0) {
        display_fill_rect(0, 128, 320, 112, RGB565(0, 0, 6));
        for (int r = 0; r < CALC_ROWS; r++)
            for (int c = 0; c < CALC_COLS; c++)
                pofo_calc_draw_cell(r, c, 0);
        calc_prev_r = calc_kb_r; calc_prev_c = calc_kb_c;
        pofo_calc_draw_cell(calc_kb_r, calc_kb_c, 1);
        display_text_center_nobg("ARROWS A=CALC B=C ST=EXIT", 233, 1, RGB565(31, 63, 31));
    }
    if (calc_prev_r != calc_kb_r || calc_prev_c != calc_kb_c) {
        pofo_calc_draw_cell(calc_prev_r, calc_prev_c, 0);
        pofo_calc_draw_cell(calc_kb_r, calc_kb_c, 1);
        calc_prev_r = calc_kb_r; calc_prev_c = calc_kb_c;
    }
}

static void pofo_app_calc_handle(uint8_t pad)
{
    static uint32_t st_u=0, ct_u=0, st_d=0, ct_d=0, st_l=0, ct_l=0, st_r=0, ct_r=0;
    static uint32_t st_a=0, ct_a=0, st_b=0, ct_b=0, st_t=0, ct_t=0;
    uint8_t edge = 0;
    static uint8_t calc_prev = 0;
    edge = (~pad) & ~calc_prev;
    calc_prev = ~pad;

    if (edge & 0x10) { calc_kb_r = (calc_kb_r + 3) % CALC_ROWS; calc_prev_r = -2; }
    if (edge & 0x20) { calc_kb_r = (calc_kb_r + 1) % CALC_ROWS; calc_prev_r = -2; }
    if (edge & 0x40) { calc_kb_c = (calc_kb_c + 3) % CALC_COLS; calc_prev_r = -2; }
    if (edge & 0x80) { calc_kb_c = (calc_kb_c + 1) % CALC_COLS; calc_prev_r = -2; }
    if (edge & 0x01) pofo_app_calc_keypress(calc_keys[calc_kb_r][calc_kb_c]);
    if (edge & 0x02) { pofo_calc_acc=0; pofo_calc_cur=0; pofo_calc_op=0; pofo_calc_opset=0; pofo_calc_error=0; }
    if (edge & 0x08) { pofo_app_current = -1; vk_visible = 0; pofo_apps_draw_menu(); }
}

/* Typed input for VK */
/* Typed input from VK/UART for the calculator */
static void pofo_calc_uart_char(int c)
{
    if (c >= '0' && c <= '9') {
        if (pofo_calc_error) { pofo_calc_error = 0; pofo_calc_acc = 0; pofo_calc_cur = 0; }
        pofo_calc_cur = pofo_calc_cur * 10 + (c - '0');
    } else if (c == '+' || c == '-' || c == '*' || c == '/') {
        if (pofo_calc_error) return;
        if (pofo_calc_opset && pofo_calc_cur != 0) {
            switch (pofo_calc_op) {
            case 0: pofo_calc_acc += pofo_calc_cur; break;
            case 1: pofo_calc_acc -= pofo_calc_cur; break;
            case 2: pofo_calc_acc *= pofo_calc_cur; break;
            case 3: if (pofo_calc_cur) pofo_calc_acc /= pofo_calc_cur; else pofo_calc_error = 1; break;
            }
        } else if (!pofo_calc_opset) {
            pofo_calc_acc = pofo_calc_cur;
        }
        pofo_calc_cur = 0;
        pofo_calc_op = (c == '+') ? 0 : (c == '-') ? 1 : (c == '*') ? 2 : 3;
        pofo_calc_opset = 1;
    } else if (c == '=' || c == '\r' || c == '\n') {
        if (pofo_calc_error) { pofo_calc_error = 0; pofo_calc_acc = 0; pofo_calc_cur = 0; return; }
        if (!pofo_calc_opset && pofo_calc_cur == 0) return;
        long rhs = pofo_calc_cur;
        if (pofo_calc_opset) {
            switch (pofo_calc_op) {
            case 0: pofo_calc_acc += rhs; break;
            case 1: pofo_calc_acc -= rhs; break;
            case 2: pofo_calc_acc *= rhs; break;
            case 3: if (rhs == 0) pofo_calc_error = 1; else pofo_calc_acc /= rhs; break;
            }
        } else {
            pofo_calc_acc = rhs;
        }
        pofo_calc_cur = 0;
    } else if (c == 'c' || c == 'C') {
        pofo_calc_acc = 0; pofo_calc_cur = 0; pofo_calc_op = 0; pofo_calc_opset = 0; pofo_calc_error = 0;
    } else if (c == '\b' || c == 0x7F) {
        pofo_calc_cur /= 10;
    }
}

/* ---- Text Editor: a tiny note pad. A=enter/type,                     */
/*      Up/Down move line, Left/Right move char, B=backspace,            */
/*      Select=save-note/next-line, Start=exit.                          */
/* All app data shares one union block — only one app is open at a time, */
/* which keeps the RAM footprint small.                                  */
#define POFO_EDIT_ROWS 6
#define POFO_EDIT_COLS 32
static union {
    struct { char lines[POFO_EDIT_ROWS][POFO_EDIT_COLS + 1]; } edit;
    struct { int cells[8][8]; } sheet;
    struct { char name[8][24]; char phone[8][16]; } contacts;
    struct { char entries[8][39]; } diary;
} pofo_app_data;

#define pofo_edit_lines        pofo_app_data.edit.lines
#define pofo_sheet_cells       pofo_app_data.sheet.cells
#define pofo_contacts_name     pofo_app_data.contacts.name
#define pofo_contacts_phone    pofo_app_data.contacts.phone
#define pofo_diary_entries     pofo_app_data.diary.entries

static int pofo_edit_row = 0;
static int pofo_edit_col = 0;

static void pofo_app_edit_draw(void)
{
    char buf[40];
    pofo_clear_text();
    pofo_put_text(0, 0, "TEXT EDITOR");
    for (int i = 0; i < 6; i++) {
        char line[40];
        snprintf(line, sizeof(line), "%c %s", (i == pofo_edit_row) ? '>' : ' ',
                 pofo_edit_lines[i]);
        pofo_put_text(1 + i, 0, line);
    }
    /* cursor: _ marker on the current position */
    char bline[40];
    memset(bline, ' ', 39);
    bline[0] = '>'; bline[2 + pofo_edit_col] = '_'; bline[39] = 0;
    pofo_put_text(1 + pofo_edit_row, 0, bline);
    pofo_put_text(7, 0, "A=TYPE  B=BS  SEL=NL  START=EXIT");
}

/* type one character into the editor (from UART or VK) */
static void pofo_edit_type(int c)
{
    if (c == '\r' || c == '\n') { pofo_edit_row = (pofo_edit_row + 1) % 6; return; }
    if (c == '\b' || c == 0x7F) {
        if (pofo_edit_col > 0) { pofo_edit_col--; pofo_edit_lines[pofo_edit_row][pofo_edit_col] = ' '; }
        return;
    }
    if (c >= 0x20 && c < 0x7f && pofo_edit_col < 37) {
        pofo_edit_lines[pofo_edit_row][pofo_edit_col++] = (char)c;
    }
}

static void pofo_app_edit_handle(uint8_t pad)
{
    static uint32_t st_u=0, ct_u=0, st_d=0, ct_d=0, st_l=0, ct_l=0, st_r=0, ct_r=0;
    static uint32_t st_a=0, ct_a=0, st_b=0, ct_b=0, st_s=0, ct_s=0, st_t=0, ct_t=0;
    if (pofo_app_edge(pad, 0x10, &st_u, &ct_u)) pofo_edit_row = (pofo_edit_row + 7) % 6;
    if (pofo_app_edge(pad, 0x20, &st_d, &ct_d)) pofo_edit_row = (pofo_edit_row + 1) % 6;
    if (pofo_app_edge(pad, 0x40, &st_l, &ct_l)) { if (pofo_edit_col > 0) pofo_edit_col--; }
    if (pofo_app_edge(pad, 0x80, &st_r, &ct_r)) { if (pofo_edit_col < 37) pofo_edit_col++; }
    if (pofo_app_edge(pad, 0x02, &st_b, &ct_b)) pofo_edit_type('\b');
    if (pofo_app_edge(pad, 0x04, &st_s, &ct_s)) pofo_edit_type('\r');
    if (pofo_app_edge(pad, 0x08, &st_t, &ct_t)) { pofo_app_current = -1; pofo_apps_draw_menu(); }
    /* A opens nothing here; typing comes from UART/VK via pofo_edit_type */
}

/* ---- Spreadsheet: 8x8 grid of integers (simple + - * / cell edits).    */
/*      A=+10, B=-10, Select=clear cell, arrows move, Start=exit.          */
static int pofo_sheet_r = 0;
static int pofo_sheet_c = 0;

static void pofo_app_sheet_draw(void)
{
    char buf[40];
    pofo_clear_text();
    pofo_put_text(0, 0, "WORKSHEET");
    snprintf(buf, sizeof(buf), "    A    B    C    D    E    F    G    H");
    pofo_put_text(1, 0, buf);
    for (int r = 0; r < 5; r++) {
        int sel = (r == pofo_sheet_r);
        snprintf(buf, sizeof(buf), "%c", sel ? '>' : ' ');
        for (int c = 0; c < 8; c++) {
            char tmp[12];
            int col_sel = (sel && c == pofo_sheet_c);
            snprintf(tmp, sizeof(tmp), col_sel ? "[%2d]" : " %3d ", pofo_sheet_cells[r][c]);
            strcat(buf, tmp);
        }
        pofo_put_text(2 + r, 0, buf);
    }
    pofo_put_text(7, 0, "ARROWS MOVE  A=+10  B=-10  START=EXIT");
}

static void pofo_app_sheet_handle(uint8_t pad)
{
    static uint32_t st_u=0, ct_u=0, st_d=0, ct_d=0, st_l=0, ct_l=0, st_r=0, ct_r=0;
    static uint32_t st_a=0, ct_a=0, st_b=0, ct_b=0, st_s=0, ct_s=0, st_t=0, ct_t=0;
    if (pofo_app_edge(pad, 0x10, &st_u, &ct_u)) pofo_sheet_r = (pofo_sheet_r + 4) % 5;
    if (pofo_app_edge(pad, 0x20, &st_d, &ct_d)) pofo_sheet_r = (pofo_sheet_r + 1) % 5;
    if (pofo_app_edge(pad, 0x40, &st_l, &ct_l)) pofo_sheet_c = (pofo_sheet_c + 7) % 8;
    if (pofo_app_edge(pad, 0x80, &st_r, &ct_r)) pofo_sheet_c = (pofo_sheet_c + 1) % 8;
    if (pofo_app_edge(pad, 0x01, &st_a, &ct_a)) pofo_sheet_cells[pofo_sheet_r][pofo_sheet_c] += 10;
    if (pofo_app_edge(pad, 0x02, &st_b, &ct_b)) pofo_sheet_cells[pofo_sheet_r][pofo_sheet_c] -= 10;
    if (pofo_app_edge(pad, 0x04, &st_s, &ct_s)) pofo_sheet_cells[pofo_sheet_r][pofo_sheet_c] = 0;
    if (pofo_app_edge(pad, 0x08, &st_t, &ct_t)) { pofo_app_current = -1; pofo_apps_draw_menu(); }
}

/* ---- Address Book: up to 8 contacts, name + phone. A=add/edit,       */
/*      B=delete, arrows scroll, Start=exit.                            */
static int pofo_contacts_count = 0;
static int pofo_contacts_sel = 0;
static int pofo_contacts_edit_phone = 0;   /* 1 = editing phone, 0 = name */

static void pofo_app_contacts_draw(void)
{
    char buf[40];
    pofo_clear_text();
    pofo_put_text(0, 0, "ADDRESS BOOK");
    for (int i = 0; i < 6; i++) {
        if (i < pofo_contacts_count) {
            snprintf(buf, sizeof(buf), "%c %s %s", (i == pofo_contacts_sel) ? '>' : ' ',
                     pofo_contacts_name[i], pofo_contacts_phone[i]);
            pofo_put_text(1 + i, 0, buf);
        } else if (i == pofo_contacts_sel) {
            pofo_put_text(1 + i, 0, "> (empty)");
        }
    }
    pofo_put_text(7, 0, "A=ADD  B=DEL  ARROWS  START=EXIT");
}

static void pofo_contacts_edit_draw(void)
{
    pofo_clear_text();
    pofo_put_text(1, 0, pofo_contacts_edit_phone ? "PHONE:" : "NAME:");
    pofo_put_text(2, 0, pofo_contacts_edit_phone ? pofo_contacts_phone[pofo_contacts_sel]
                                                 : pofo_contacts_name[pofo_contacts_sel]);
    pofo_put_text(7, 0, "TYPE  A=NEXT  B=BS  SEL=SAVE");
}

static int pofo_contacts_editing = 0;
static void pofo_contacts_edit_type(int c)
{
    int idx = pofo_contacts_sel;
    char *dst = pofo_contacts_edit_phone ? pofo_contacts_phone[idx] : pofo_contacts_name[idx];
    int max = pofo_contacts_edit_phone ? 15 : 23;
    if (c == '\b' || c == 0x7F) {
        int l = (int)strlen(dst);
        if (l > 0) dst[l - 1] = 0;
        return;
    }
    if (c >= 0x20 && c < 0x7f && (int)strlen(dst) < max) {
        int l = (int)strlen(dst);
        dst[l] = (char)c;
        dst[l + 1] = 0;
    }
}

static void pofo_app_contacts_handle(uint8_t pad)
{
    static uint32_t st_u=0, ct_u=0, st_d=0, ct_d=0, st_a=0, ct_a=0, st_b=0, ct_b=0, st_s=0, ct_s=0, st_t=0, ct_t=0;
    if (pofo_contacts_editing) {
        if (pofo_app_edge(pad, 0x01, &st_a, &ct_a)) {
            pofo_contacts_edit_phone = !pofo_contacts_edit_phone;
            if (!pofo_contacts_edit_phone) pofo_contacts_editing = 0;
        }
        if (pofo_app_edge(pad, 0x02, &st_b, &ct_b)) pofo_contacts_edit_type('\b');
        if (pofo_app_edge(pad, 0x04, &st_s, &ct_s)) { pofo_contacts_editing = 0; }
        if (pofo_app_edge(pad, 0x08, &st_t, &ct_t)) { pofo_contacts_editing = 0; }
        return;
    }
    if (pofo_app_edge(pad, 0x10, &st_u, &ct_u)) pofo_contacts_sel = (pofo_contacts_sel + 5) % 6;
    if (pofo_app_edge(pad, 0x20, &st_d, &ct_d)) pofo_contacts_sel = (pofo_contacts_sel + 1) % 6;
    if (pofo_app_edge(pad, 0x01, &st_a, &ct_a)) {
        if (pofo_contacts_sel >= pofo_contacts_count) {
            pofo_contacts_count = pofo_contacts_sel + 1;
        }
        pofo_contacts_edit_phone = 0;
        pofo_contacts_editing = 1;
    }
    if (pofo_app_edge(pad, 0x02, &st_b, &ct_b)) {
        if (pofo_contacts_sel < pofo_contacts_count) {
            for (int i = pofo_contacts_sel; i < pofo_contacts_count - 1; i++) {
                strcpy(pofo_contacts_name[i], pofo_contacts_name[i + 1]);
                strcpy(pofo_contacts_phone[i], pofo_contacts_phone[i + 1]);
            }
            pofo_contacts_count--;
        }
    }
    if (pofo_app_edge(pad, 0x08, &st_t, &ct_t)) { pofo_app_current = -1; pofo_apps_draw_menu(); }
}

/* ---- Diary: 8 slots with short text; arrows scroll, A=edit, B=clear. */
static int pofo_diary_sel = 0;
static int pofo_diary_editing = 0;

static void pofo_app_diary_draw(void)
{
    char buf[40];
    pofo_clear_text();
    pofo_put_text(0, 0, "DIARY");
    for (int i = 0; i < 6; i++) {
        snprintf(buf, sizeof(buf), "%c %02d %s", (i == pofo_diary_sel) ? '>' : ' ',
                 i + 1, pofo_diary_entries[i]);
        pofo_put_text(1 + i, 0, buf);
    }
    pofo_put_text(7, 0, "A=EDIT  B=CLEAR  ARROWS  START=EXIT");
}

static void pofo_app_diary_handle(uint8_t pad)
{
    static uint32_t st_u=0, ct_u=0, st_d=0, ct_d=0, st_a=0, ct_a=0, st_b=0, ct_b=0, st_s=0, ct_s=0, st_t=0, ct_t=0;
    if (pofo_diary_editing) {
        if (pofo_app_edge(pad, 0x02, &st_b, &ct_b)) {
            int l = (int)strlen(pofo_diary_entries[pofo_diary_sel]);
            if (l > 0) pofo_diary_entries[pofo_diary_sel][l - 1] = 0;
        }
        if (pofo_app_edge(pad, 0x04, &st_s, &ct_s)) pofo_diary_editing = 0;
        if (pofo_app_edge(pad, 0x08, &st_t, &ct_t)) pofo_diary_editing = 0;
        return;
    }
    if (pofo_app_edge(pad, 0x10, &st_u, &ct_u)) pofo_diary_sel = (pofo_diary_sel + 5) % 6;
    if (pofo_app_edge(pad, 0x20, &st_d, &ct_d)) pofo_diary_sel = (pofo_diary_sel + 1) % 6;
    if (pofo_app_edge(pad, 0x01, &st_a, &ct_a)) pofo_diary_editing = 1;
    if (pofo_app_edge(pad, 0x02, &st_b, &ct_b)) pofo_diary_entries[pofo_diary_sel][0] = 0;
    if (pofo_app_edge(pad, 0x08, &st_t, &ct_t)) { pofo_app_current = -1; pofo_apps_draw_menu(); }
}

/* ---- dispatch joystick input to the hub or the current app ---- */
static void pofo_apps_handle(uint8_t pad)
{
    static uint32_t st_u=0, ct_u=0, st_d=0, ct_d=0, st_a=0, ct_a=0, st_b=0, ct_b=0;
    static uint32_t st_s=0, ct_s=0, st_t=0, ct_t=0;

    if (pofo_app_current < 0) {
        /* hub menu */
/* Wait a few frames with all buttons released after entering APPS,
          * so the button that confirmed the command (A via VK) does not
          * immediately open the first app or exit. */
        if (pofo_apps_wait_release) {
            if (pad == 0xFF) pofo_apps_wait_release--;
            else pofo_apps_wait_release = 2;
            return;
        }
        if (pofo_app_edge(pad, 0x10, &st_u, &ct_u)) pofo_apps_sel = (pofo_apps_sel + APPS_MAX - 1) % APPS_MAX;
        if (pofo_app_edge(pad, 0x20, &st_d, &ct_d)) pofo_apps_sel = (pofo_apps_sel + 1) % APPS_MAX;
        if (pofo_app_edge(pad, 0x01, &st_a, &ct_a) || pofo_app_edge(pad, 0x08, &st_t, &ct_t)) {
            pofo_app_current = pofo_apps_sel;
            vk_visible = 0; vk_dirty = 1;
            if (pofo_apps_sel == APP_CALC) calc_prev_r = -1;
        }
        if (pofo_app_edge(pad, 0x02, &st_b, &ct_b)) {
            pofo_apps_active = 0;
            display_fill(LCD_OFF_RGB);
            key_make(2, 6); exec86(4000); key_break(2, 6);  /* Enter → fresh DOS prompt */
        }
        return;
    }
    switch (pofo_app_current) {
    case APP_CALC:     pofo_app_calc_handle(pad);     vk_visible = 0; break;
    case APP_EDIT:     pofo_app_edit_handle(pad);     break;
    case APP_SHEET:    pofo_app_sheet_handle(pad);    break;
    case APP_CONTACTS: pofo_app_contacts_handle(pad); break;
    case APP_DIARY:    pofo_app_diary_handle(pad);    break;
    }
}

/* UART char routed to the focused app's text field */
static void pofo_app_uart_char(int c)
{
    switch (pofo_app_current) {
    case APP_CALC:
        pofo_calc_uart_char(c);
        break;
    case APP_EDIT:
        pofo_edit_type(c);
        break;
    case APP_CONTACTS:
        if (pofo_contacts_editing) pofo_contacts_edit_type(c);
        break;
    case APP_DIARY:
        if (pofo_diary_editing) {
            char *e = pofo_diary_entries[pofo_diary_sel];
            int l = (int)strlen(e);
            if (c == '\r' || c == '\n') { pofo_diary_editing = 0; }
            else if (c == '\b' || c == 0x7F) { if (l > 0) e[l - 1] = 0; }
            else if (c >= 0x20 && c < 0x7f && l < 38) { e[l] = (char)c; e[l + 1] = 0; }
        }
        break;
    default:
        break;
    }
}

/* one animation step for the PIN screen */
static void pofo_pin_draw(void)
{
    int f = pofo_pin_frame++;
    char line[40];

    pofo_clear_text();

    /* header */
    pofo_put_text(0, 0, "P-CRACK v1.0");
    pofo_put_text(1, 0, ">TEST.0.0`");
    pofo_put_text(2, 0, ">ENTRY CODE: ");

    /* cycling 4-digit code: deterministic pseudo-random from frame counter */
    unsigned code = (unsigned)f * 2654435761u;
    snprintf(line, sizeof(line), "  %04lu", (unsigned long)(code % 10000));
    pofo_put_text(2, 13, line);

    pofo_put_text(3, 0, "WATING...");

    /* "progress" bar drawn with block chars */
    int prog = (f / 8) % 32;
    pofo_put_text(4, 0, ">");
    for (int i = 0; i < 32; i++)
        line[i] = (i < prog) ? '#' : ' ';
    line[32] = 0;
    pofo_put_text(4, 2, line);

    pofo_put_text(5, 0, "PRESS ENTER");
}

/* Track the typed command (from UART or VK) to catch the built-in
 * commands "PIN" / "APPS" / "HELP" + Enter. */
static char pofo_cmd[16];
static int pofo_cmd_len = 0;

/* Built-in HELP screen: shows the DIP DOS commands plus the extra
 * built-in commands (APPS / PIN). ENTER exits back to DOS. */
static int pofo_help_active = 0;

static void pofo_help_draw(void)
{
    static const char *const lines[] = {
        "DIP DOS COMMANDS",
        "DIR DEL REN COPY TYPE",
        "CD MD RD PATH VER VOL",
        "FORMAT FDISK LABEL CHKDSK",
        "CLS DATE TIME PROMPT SET",
        "ECHO IF FOR GOTO SHIFT PAUSE",
        "REM BREAK EXIT RUN HELP",
        "- built-in -",
        "APPS  PIN",
        "ENTER=EXIT",
    };
    pofo_clear_text();
    for (int i = 0; i < 10; i++)
        pofo_put_text(i, 0, lines[i]);
}

/* return 1 if the current line matched a built-in command and it was
 * consumed (the DOS prompt must not receive the Enter). */
static int pofo_exit_req = 0;   /* выход из Portfolio */
static uint32_t pofo_esc_hold_us = 0;  /* 0 = ESC не нажат, иначе время начала */
extern "C" int portfolio_exit_requested(void) { return pofo_exit_req; }

static int pofo_cmd_check(void)
{
    int len = pofo_cmd_len;
    if (len == 3 && (pofo_cmd[0]=='P'||pofo_cmd[0]=='p') && (pofo_cmd[1]=='I'||pofo_cmd[1]=='i') &&
        (pofo_cmd[2]=='N'||pofo_cmd[2]=='n')) {
        pofo_cmd_len = 0;
        pofo_run_pin();
        return 1;
    }
    if (len == 4 && (pofo_cmd[0]=='A'||pofo_cmd[0]=='a') && (pofo_cmd[1]=='P'||pofo_cmd[1]=='p') &&
        (pofo_cmd[2]=='P'||pofo_cmd[2]=='p') && (pofo_cmd[3]=='S'||pofo_cmd[3]=='s')) {
        pofo_cmd_len = 0;
        pofo_run_apps();
        return 1;
    }
    if (len == 4 && (pofo_cmd[0]=='E'||pofo_cmd[0]=='e') && (pofo_cmd[1]=='X'||pofo_cmd[1]=='x') &&
        (pofo_cmd[2]=='I'||pofo_cmd[2]=='i') && (pofo_cmd[3]=='T'||pofo_cmd[3]=='t')) {
        pofo_cmd_len = 0;
        pofo_exit_req = 1;   /* команда EXIT → выход в меню */
        return 1;
    }
    pofo_cmd_len = 0;
    return 0;
}

/* Обработка одного символа (нормализованного) → нажатие клавиши Portfolio.
 * Источники: UART (pofo_uart_input) и USB-клавиатура (pofo_usbkbd_input).
 * Спец-коды: 0x0B = стрелка вверх, 0x0C = стрелка вниз (для APPS-хаба). */
static void pofo_char_input(int c)
{
    /* Built-in APPS hub: стрелки/Enter/Backspace */
    if (pofo_apps_active && pofo_app_current < 0) {
        if (c == 0x0B) pofo_apps_sel = (pofo_apps_sel + APPS_MAX - 1) % APPS_MAX;
        else if (c == 0x0C) pofo_apps_sel = (pofo_apps_sel + 1) % APPS_MAX;
        else if (c == '\r' || c == '\n') pofo_app_current = pofo_apps_sel;
        else if (c == 0x7F || c == '\b') { pofo_apps_active = 0; display_fill(LCD_OFF_RGB); key_make(2,6); exec86(4000); key_break(2,6); }
        return;
    }

    /* Built-in APPS: символ в активное текстовое поле. */
    if (pofo_apps_active && pofo_app_current >= 0) {
        pofo_app_uart_char(c);
        return;
    }

    /* Built-in screens: только ENTER выходит из PIN/HELP. */
    if (pofo_pin_active || pofo_help_active) {
        if (c == '\r' || c == '\n') {
            pofo_pin_active = 0;
            pofo_help_active = 0;
            display_fill(LCD_OFF_RGB);
        }
        return;
    }

    /* Отслеживание команды "PIN" / "APPS" / "HELP". */
    if (c == '\r' || c == '\n') {
        if (pofo_cmd_check()) return;
    } else if (c == 0x7F || c == '\b') {
        if (pofo_cmd_len > 0) pofo_cmd_len--;
        c = 0x08;
    } else if (pofo_cmd_len < 15) {
        pofo_cmd[pofo_cmd_len++] = (char)c;
    }

    uint8_t row, col; int shift = 0;
    if (!pofo_ascii_key((char)c, &row, &col, &shift)) return;

    if (shift) {
        key_make(3, 3);              /* Shift down */
        exec86(4000);                /* BIOS handles Shift INT 09h */
        key_make(row, col);          /* base key */
        exec86(4000);                /* BIOS handles letter INT 09h */
        key_break(row, col);         /* letter up */
        key_break(3, 3);             /* Shift up */
    } else {
        key_make(row, col);
        exec86(4000);
        key_break(row, col);
    }
}

/* Pull any pending UART bytes and turn them into key presses.
 * ANSI-стрелки (ESC[A / ESC[B) конвертируются в 0x0B/0x0C. */
static void pofo_uart_input(void)
{
    while (uart_is_readable(uart0)) {
        int c = uart_getc(uart0);
        if (c < 0) break;
        if (c == 0x1B) {
            int c2 = uart_getc(uart0);
            if (c2 == '[') {
                int c3 = uart_getc(uart0);
                if (c3 == 'A') pofo_char_input(0x0B);
                else if (c3 == 'B') pofo_char_input(0x0C);
                else pofo_char_input(0x1B);
            } else {
                pofo_char_input(0x1B);
                if (c2 >= 0) pofo_char_input(c2);
            }
            continue;
        }
        pofo_char_input(c);
    }
}

/* USB HID scancodes (boot protocol) -> ASCII. base = без Shift, shifted = с Shift. */
static const char pofo_hid_base[57] = {
    0,0,0,0,                             /* 0-3: нет */
    'a','b','c','d','e','f','g','h','i','j','k','l','m',   /* 4-16 */
    'n','o','p','q','r','s','t','u','v','w','x','y','z',   /* 17-29 */
    '1','2','3','4','5','6','7','8','9','0',               /* 30-39 */
    '\r',0x1B,0x7F,'\t',' ',                               /* 40-44 */
    '-','=','[',']','\\',                                  /* 45-49 */
    ';','\'','`',',','.','/'                               /* 51-56 */
};
static const char pofo_hid_shift[57] = {
    0,0,0,0,
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '!','@','#','$','%','^','&','*','(',')',
    '\r',0x1B,0x7F,'\t',' ',
    '_','+','{','}','|',
    ':','"','~','<','>','?'
};

/* Выбор текущей клавиши VK (Enter / клик A). Общий для vk_handle и USB-клавы. */
static void vk_select_current(void)
{
    if (vk_cur_r == VK_ROWS - 1) {
        switch (vk_cur_c) {
        case 0: /* Enter */
            if (pofo_apps_active && pofo_app_current >= 0) pofo_app_uart_char('\r');
            else if (!pofo_cmd_check()) vk_key_press_rc(2, 6);
            break;
        case 1: /* Backspace */
            if (pofo_apps_active && pofo_app_current >= 0) pofo_app_uart_char('\b');
            else { if (pofo_cmd_len > 0) pofo_cmd_len--; vk_key_press_rc(1, 6); }
            break;
        case 2: /* Space */
            if (pofo_apps_active && pofo_app_current >= 0) pofo_app_uart_char(' ');
            else vk_key_press_rc(6, 2);
            break;
        case 3: /* Esc → закрыть VK */
            vk_key_press_rc(7, 7);
            vk_visible = 0;
            if (vk_shift) { write86(0x00417, read86(0x00417) & ~0x41); vk_shift = 0; vk_shift_oneshot = 0; }
            break;
        case 4: /* Shift toggle: 0→1→2→0 */
            if (vk_shift == 0) {
                vk_shift = 1; vk_shift_oneshot = 1;
                write86(0x00417, read86(0x00417) | 0x01);
            } else if (vk_shift == 1) {
                vk_shift = 2; vk_shift_oneshot = 0;
                write86(0x00417, (read86(0x00417) & ~0x01) | 0x40);
            } else {
                vk_shift = 0; vk_shift_oneshot = 0;
                write86(0x00417, read86(0x00417) & ~0x41);
            }
            break;
        case 5: /* RUS toggle */
            pofo_ru_mode = !pofo_ru_mode;
            break;
        }
    } else {
        const struct vk_key_t *k = &vk_keys[vk_cur_r][vk_cur_c];
        if (pofo_cmd_len < 15 && k->label >= 0x20 && k->label < 0x7f) {
            char ch = k->label;
            if (pofo_cmd_len == 0 && ch == ' ') return;
            pofo_cmd[pofo_cmd_len++] = ch;
        }
        vk_key_press(k);
    }
    vk_dirty = 1;
}

/* USB-клавиатура как источник ввода — ПОЛНАЯ клавиатура, без игровых маппингов.
 * VK открывается только по Insert (73). Когда VK открыта:
 *   стрелки двигают курсор, Enter выбирает клавишу, Esc/Insert закрывают,
 *   буквы/цифры всё равно печатаются напрямую в DIP DOS.
 *
 * Edge-детект: usb_kbd_get_raw() возвращает СОСТОЯНИЕ (все зажатые клавиши),
 * а не события. Без трекинга удержание клавиши печатает её каждый кадр
 * (пачки символов). Сравниваем с предыдущим отчётом и обрабатываем
 * только НОВЫЕ нажатия. */
static uint8_t pofo_prev_keys[6] = {0,0,0,0,0,0};
static int pofo_prev_n = 0;

static void pofo_usbkbd_input(void)
{
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    if (n <= 0) { pofo_prev_n = 0; return; }

    uint8_t mods = usb_kbd_get_mods();
    int shift = (mods & 0x02) || (mods & 0x20);
    int hub = pofo_apps_active && pofo_app_current < 0;

    /* Проверка отпускания ESC: если ESC нет в сыром отчёте — сброс таймера */
    int esc_held = 0;
    for (int k = 0; k < n; k++) if (keys[k] == 41) esc_held = 1;
    if (!esc_held) pofo_esc_hold_us = 0;

    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];

        /* Новое ли это нажатие? (не было в предыдущем отчёте) */
        int is_new = 1;
        for (int j = 0; j < pofo_prev_n; j++)
            if (pofo_prev_keys[j] == sc) { is_new = 0; break; }
        if (!is_new) continue;

        /* Стрелки */
        if (sc == 82 || sc == 81 || sc == 80 || sc == 79) {
            if (vk_visible) {
                if (sc == 82) vk_cur_r = (vk_cur_r + VK_ROWS - 1) % VK_ROWS;
                else if (sc == 81) vk_cur_r = (vk_cur_r + 1) % VK_ROWS;
                else if (sc == 80) vk_cur_c = (vk_cur_c + VK_COLS - 1) % VK_COLS;
                else vk_cur_c = (vk_cur_c + 1) % VK_COLS;
                vk_dirty = 1;
            } else if (hub) {
                if (sc == 82) pofo_char_input(0x0B);
                else if (sc == 81) pofo_char_input(0x0C);
            } else {
                /* DIP DOS: стрелки как сканкоды (матрица Portfolio):
                 * Up=Y3/bit5, Down=Y4/bit5, Left=Y5/bit3, Right=Y5/bit4 */
                uint8_t row, col;
                if (sc == 82) { row = 3; col = 5; }
                else if (sc == 81) { row = 4; col = 5; }
                else if (sc == 80) { row = 5; col = 3; }
                else { row = 5; col = 4; }
                key_make(row, col); exec86(4000); key_break(row, col);
            }
            continue;
        }

        /* Enter (40): VK → выбор; APPS-хаб → запуск; DOS → Enter */
        if (sc == 40) {
            if (vk_visible) { vk_select_current(); continue; }
            pofo_char_input('\r');
            continue;
        }

        /* Esc (41): VK → закрыть; DOS → удержание ~1 сек для выхода */
        if (sc == 41) {
            if (vk_visible) {
                vk_visible = 0; vk_dirty = 1; display_fill(LCD_OFF_RGB);
            } else {
                pofo_esc_hold_us = h3_hs_timer_lo_us();   /* начало удержания */
            }
            continue;
        }

        /* Insert (73): открыть/закрыть VK */
        if (sc == 73) {
            vk_visible = !vk_visible;
            vk_dirty = 1;
            pofo_cmd_len = 0;
            display_fill(LCD_OFF_RGB);
            if (vk_visible && vk_shift) { write86(0x00417, read86(0x00417) | 0x01); }
            if (!vk_visible && vk_shift) { write86(0x00417, read86(0x00417) & ~0x41); vk_shift = 0; vk_shift_oneshot = 0; }
            continue;
        }

        /* Остальные клавиши — обычный текст (полная клавиатура) */
        if (sc >= 57) continue;
        char ch = shift ? pofo_hid_shift[sc] : pofo_hid_base[sc];
        if (ch) pofo_char_input(ch);
    }

    /* Запомнить текущий отчёт для edge-детекта в следующем кадре */
    for (int i = 0; i < n && i < 6; i++) pofo_prev_keys[i] = keys[i];
    pofo_prev_n = (n > 6) ? 6 : n;
}

/* Shared single-frame edge detector (active-LOW buttons, bitmask) */
/* Returns bitmask of edges: 0→1 transition on inverted signal = press */
/* State variables live in run_frame's static locals. */
static uint8_t edge_detect(uint8_t pad, uint8_t *prev)
{
    /* детектор фронта: pad=0 значит «нажато», инвертируем и ищем 0→1 */
    uint8_t edge = (~pad) & ~(*prev);   /* 0→1 on inverted = press edge */
    *prev = ~pad;
    return edge;
}

/* handle joystick when the VK is open */
static int vk_handle(uint8_t pad)
{
    static uint8_t vk_prev = 0;
    static int hold_cnt = 0;
    uint8_t edge = edge_detect(pad, &vk_prev);

    if (edge & 0x10) { vk_cur_r = (vk_cur_r + VK_ROWS - 1) % VK_ROWS; vk_dirty = 1; return 1; }
    if (edge & 0x20) { vk_cur_r = (vk_cur_r + 1) % VK_ROWS;           vk_dirty = 1; return 1; }
    if (edge & 0x40) { vk_cur_c = (vk_cur_c + VK_COLS - 1) % VK_COLS; vk_dirty = 1; return 1; }
    if (edge & 0x80) { vk_cur_c = (vk_cur_c + 1) % VK_COLS;           vk_dirty = 1; return 1; }

    if (edge & 0x01) { /* A = select key */
        if (vk_cur_r == VK_ROWS - 1) {
            switch (vk_cur_c) {
            case 0: /* Enter */
                if (pofo_apps_active && pofo_app_current >= 0) pofo_app_uart_char('\r');
                else if (pofo_cmd_check()) {}
                else vk_key_press_rc(2, 6);
                break;
            case 1: /* Backspace */
                if (pofo_apps_active && pofo_app_current >= 0) pofo_app_uart_char('\b');
                else { if (pofo_cmd_len > 0) pofo_cmd_len--; vk_key_press_rc(1, 6); }
                break;
            case 2: /* Space */
                if (pofo_apps_active && pofo_app_current >= 0) pofo_app_uart_char(' ');
                else vk_key_press_rc(6, 2);
                break;
            case 3: vk_key_press_rc(7, 7); vk_visible = 0; if (vk_shift) { write86(0x00417, read86(0x00417) & ~0x41); vk_shift = 0; vk_shift_oneshot = 0; } break; /* Esc → close VK */
            case 5: /* RUS toggle */
            pofo_ru_mode = !pofo_ru_mode;
            vk_dirty = 1;
            break;
        case 4: /* Shift toggle: цикл 0→1→2→0
                  * 0 = off, 1 = one-shot (зелёный, после одной буквы сброс),
                  * 2 = caps (синий, залипание до повторного нажатия).
                  * One-shot через write86 в BIOS flag — без посылки скан-кода
                  * Shift, чтобы не засорять INT 09h. */
        if (vk_shift == 0) {
            vk_shift = 1; vk_shift_oneshot = 1;
            write86(0x00417, read86(0x00417) | 0x01);
        } else if (vk_shift == 1) {
            vk_shift = 2; vk_shift_oneshot = 0;
            write86(0x00417, (read86(0x00417) & ~0x01) | 0x40);
        } else {
            vk_shift = 0; vk_shift_oneshot = 0;
            write86(0x00417, read86(0x00417) & ~0x41);
        }
        vk_dirty = 1;
        break;
            }
        } else {
            const struct vk_key_t *k = &vk_keys[vk_cur_r][vk_cur_c];
            if (pofo_cmd_len < 15 && k->label >= 0x20 && k->label < 0x7f) {
                char ch = k->label;
                if (pofo_cmd_len == 0 && ch == ' ') return 1;
                pofo_cmd[pofo_cmd_len++] = ch;
            }
            vk_key_press(k);
        }
        return 1;
    }
    if (edge & 0x02) { /* B = Backspace */
        if (pofo_cmd_len > 0) pofo_cmd_len--;
        vk_key_press_rc(1, 6);
        return 1;
    }
    if (edge & 0x08) { /* Start = Enter */
        if (pofo_cmd_check()) return 1;
        vk_key_press_rc(2, 6);
        return 1;
    }

    /* Auto-repeat on held directions */
    {
        uint8_t held = (~pad) & 0xF0;
        enum { RD = 30, RR = 8 };
        if (held) {
            hold_cnt++;
            int do_rep = (hold_cnt == RD) || (hold_cnt > RD && ((hold_cnt - RD) % RR) == 0);
            if (do_rep) {
                if (held & 0x10) vk_cur_r = (vk_cur_r + VK_ROWS - 1) % VK_ROWS;
                else if (held & 0x20) vk_cur_r = (vk_cur_r + 1) % VK_ROWS;
                else if (held & 0x40) vk_cur_c = (vk_cur_c + VK_COLS - 1) % VK_COLS;
                else if (held & 0x80) vk_cur_c = (vk_cur_c + 1) % VK_COLS;
                vk_dirty = 1;
            }
        } else { hold_cnt = 0; }
    }
    return 0;
}

/* render the virtual keyboard over the lower part of the screen */

static void vk_render(void)
{
    display_fill_rect(0, 120, 320, 100, RGB565(0, 0, 6));

    for (int r = 0; r < VK_ROWS; r++) {
        for (int c = 0; c < VK_COLS; c++) {
            int is_func = (r == VK_ROWS - 1);
            if (is_func && c >= 6) continue;
            int x = is_func ? (8 + c * 52) : (8 + c * 26);
            int y = 122 + r * 18;
            int w = is_func ? 50 : 24;
            int sel = (r == vk_cur_r && c == vk_cur_c);
            int sh_on = (vk_shift == 1) && is_func && (c == 4);
            int caps_on = (vk_shift == 2) && is_func && (c == 4);
            int rus_on = pofo_ru_mode && is_func && (c == 5);
            uint16_t clr;
            if (caps_on) clr = RGB565(0, 0, 31);
            else if (sh_on || rus_on) clr = RGB565(0, 63, 0);
            else clr = sel ? RGB565(31, 31, 0) : RGB565(15, 18, 25);
            display_fill_rect(x, y, w, 18, clr);
            if (is_func) {
                const char *lab = (caps_on || sh_on) ? "SFT" : (rus_on ? "RUS" : vk_func_labels[c]);
                if (lab && lab[0])
                    display_text_at_nobg(lab, x + 2, y + 5, 1, RGB565(0,0,0));
            } else {
                char ch = vk_keys[r][c].label;
                if (vk_shift && ch >= 'a' && ch <= 'z') ch -= 32;
                if (ch == ' ') {
                    display_text_at_nobg(sel ? "SPC" : "spc", x + 2, y + 5, 1, sel ? RGB565(0,0,0) : RGB565(20,24,30));
                } else {
                    char s[2] = { ch, 0 };
                    display_text_at_nobg(s, x + 9, y + 5, 1, sel ? RGB565(0,0,0) : RGB565(22,26,32));
                }
            }
        }
    }
    display_text_center_nobg("A=KEY  B=BACKSP  START=ENTER  SEL=CLOSE", 233, 1, RGB565(31, 63, 31));
}

/* ------------------------------------------------------------------ */
static void kbd_poll(void)
{
/* Debounce: только когда сырой GPIO стабилен DEBOUNCE кадров подряд.
 * GPIO-дребезг иначе спамит INT 09h → BIOS бросает инициализацию LCD
 * (наблюдалось на железе). Каждый ряд клавиатуры сравнивается с предыдущим
 * состоянием — генерируются make/break только по изменению. */
    enum { DEBOUNCE = 2 };
    uint8_t pad = joypad_buttons();  /* 0 = pressed */

    if (pad == pofo_kbd_cand) {
        if (pofo_kbd_cnt < DEBOUNCE) pofo_kbd_cnt++;
        if (pofo_kbd_cnt == DEBOUNCE && pofo_kbd_stable != pad) {
            pofo_kbd_stable = pad;
            pofo_kbd_cnt = 0;
            uint8_t cols[8] = { 0 };
            if (!(pad & 0x80)) cols[5] |= 0x10;   /* Right */
            if (!(pad & 0x40)) cols[5] |= 0x08;   /* Left */
            if (!(pad & 0x10)) cols[3] |= 0x20;   /* Up */
            if (!(pad & 0x20)) cols[4] |= 0x20;   /* Down */
            if (!(pad & 0x01)) cols[6] |= 0x10;   /* A = Space (Enter) */
            if (!(pad & 0x02)) cols[1] |= 0x80;   /* B = Backspace */
            if (!(pad & 0x08)) cols[6] |= 0x40;   /* Start = Fn */
            /* Select (0x04) is NOT mapped to a key here: it only toggles the
             * on-screen keyboard in run_frame, so opening the VK must not
             * send an Esc (0x3F) that DIP DOS would echo as '\'. */

            for (int row = 0; row < 8; row++) {
                uint8_t change = pofo_kbd_prev_cols[row] ^ cols[row];
                if (change) {
                    for (int col = 0; col < 8; col++) {
                        if (change & (1 << col)) {
                            if (cols[row] & (1 << col)) key_make(row, col);
                            else key_break(row, col);
                        }
                    }
                    pofo_kbd_prev_cols[row] = cols[row];
                }
            }
        }
    } else {
        pofo_kbd_cand = pad;
        pofo_kbd_cnt = 0;
    }
}

/* Keyboard row read (port 0x8000 mirrors). Port uses m_kbd_data, but
 * we return pending scancode for BIOS INT 16h / 61h compatibility. */
extern "C" uint8_t pofo_keyboard_read(void)
{
    return asic_keyboard_r();
}

/* ------------------------------------------------------------------ */
/* Port I/O (C++ linkage, matches cpu.cpp extern declarations)         */
/* ------------------------------------------------------------------ */
void portout(uint16_t portnum, uint16_t value)
{
    switch (portnum) {
    case 0x20: case 0x21: out8259(portnum, value); break;
    case 0x40: case 0x41: case 0x42: case 0x43: out8253(portnum, value); break;
    case 0x8010: case 0x8011:
        hd61830_wr(portnum & 1, (uint8_t)value); break;
    case 0x8020: /* DTMF - ignored */ break;
    case 0x8030: asic_power_w((uint8_t)value); break;
    case 0x8040: case 0x8041: asic_counter_w(portnum & 1, (uint8_t)value); break;
    case 0x8050: asic_irq_mask_w((uint8_t)value); break;
    case 0x8051: asic_select_w((uint8_t)value); break;
    case 0x8060: m_contrast = (uint8_t)value; break;
    default: break;
    }
}

uint16_t portin(uint16_t portnum)
{
    switch (portnum) {
    case 0x20: case 0x21: return in8259(portnum);
    case 0x40: case 0x41: case 0x42: case 0x43: return in8253(portnum);
    case 0x8000: return pofo_keyboard_read();
    case 0x8010: case 0x8011: return hd61830_rd(portnum & 1);
    case 0x8040: case 0x8041: return asic_counter_r(portnum & 1);
    case 0x8050: return asic_irq_status_r();
    case 0x8051: return asic_battery_r();
    case 0x8060: return m_contrast;
    case 0x0061: return 0x61;
    default: return 0xFF;
    }
}

/* ------------------------------------------------------------------ */
/* HD61830 implementation                                             */
/* ------------------------------------------------------------------ */
static void hd61830_busy(void) { /* no-op: CPU is fast enough */ }

static void hd61830_wr(int offset, uint8_t data)
{
    if (offset & 1) {
        lcdc.ir = data;      /* control register */
        return;
    }
    switch (lcdc.ir) {
    case INSTR_MODE_CONTROL:
        lcdc.mcr = data; break;
    case INSTR_CHARACTER_PITCH:
        lcdc.hp = (data & 0x07) + 1;
        lcdc.vp = (data >> 4) + 1; break;
    case INSTR_NUMBER_OF_CHARACTERS:
        lcdc.hn = (data & 0x7f) + 1;
        if (lcdc.hn % 2) lcdc.hn++; break;
    case INSTR_NUMBER_OF_TIME_DIVISIONS:
        lcdc.nx = (data & 0x7f) + 1; break;
    case INSTR_CURSOR_POSITION:
        lcdc.cp = (data & 0x0f) + 1; break;
    case INSTR_DISPLAY_START_LOW:
        lcdc.dsa = (lcdc.dsa & 0xff00) | data; break;
    case INSTR_DISPLAY_START_HIGH:
        lcdc.dsa = (data << 8) | (lcdc.dsa & 0xff); break;
    case INSTR_CURSOR_ADDRESS_LOW:
        /* Если бит 7 CAC был 1, а новый data<0x80 — каретка перешла
         * через 256-байтную границу; инкрементируем старший байт.
         * Это поведение HD61830 (MAME hd61830.c). */
        if (BIT(lcdc.cac, 7) && !BIT(data, 7))
            lcdc.cac = (((lcdc.cac >> 8) + 1) << 8) | data;
        else
            lcdc.cac = (lcdc.cac & 0xff00) | data;
        break;
    case INSTR_CURSOR_ADDRESS_HIGH:
        lcdc.cac = (data << 8) | (lcdc.cac & 0xff); break;
    case INSTR_DISPLAY_DATA_WRITE:
        if (lcdc.cac < HD61830_VRAM_SIZE) { lcdc.vram[lcdc.cac] = data; }
        lcdc.cac++; break;
    case INSTR_CLEAR_BIT:
        if (lcdc.cac < HD61830_VRAM_SIZE)
            lcdc.vram[lcdc.cac] &= ~(1 << (data & 7));
        lcdc.cac++; break;
    case INSTR_SET_BIT:
        if (lcdc.cac < HD61830_VRAM_SIZE)
            lcdc.vram[lcdc.cac] |= 1 << (data & 7);
        lcdc.cac++; break;
    default: break;
    }
    hd61830_busy();
}

static uint8_t hd61830_rd(int offset)
{
    if (offset & 1) return 0x80;   /* status: not busy */
    return lcdc.dor;
}

/* ------------------------------------------------------------------ */
/* Render: 240x64 LCD -> 320x240 display, vertical stretch. When the VK
 * is open the LCD shrinks to 2x and is pinned to the TOP (no empty band
 * above), freeing the lower part for a large on-screen keyboard. */
/* ------------------------------------------------------------------ */
/* LCD is natively 240 wide. Stretch it horizontally to fill the whole
 * 320px display (no side borders): 240 * 4/3 = 320. */
#define LCD_X_OFF 0
#define LCD_W_OUT 320

/* Height of the LCD area: full 240px, or the top 128px when the VK is open
 * (its keyboard occupies 128..240). */
static int lcd_out_h(void)
{
    if (vk_visible) return 120;         /* полосы под VK */
    if (pofo_apps_active && pofo_app_current == APP_CALC) return 120;
    return 224;                         /* 16px внизу для футера */
}

static uint16_t pofo_line_rgb[240];
static uint16_t pofo_line_out[LCD_W_OUT];

/* stretch a 240px LCD line to LCD_W_OUT px and stream it to the display */
static void pofo_stream_line(int y)
{
    for (int x = 0; x < LCD_W_OUT; x++)
        pofo_line_out[x] = pofo_line_rgb[(x * 240) / LCD_W_OUT];
    display_stream_begin(LCD_X_OFF, y, LCD_W_OUT, 1);
    display_stream_pixels16(pofo_line_out, LCD_W_OUT, 1);
    display_stream_end();
}

/* Stream one of the 64 native LCD scanlines stretched onto the whole LCD
 * area height. Fractional mapping: row sy covers screen lines
 * [sy*H/64, (sy+1)*H/64), so all 64 rows fill the area exactly
 * (240 -> mostly 4 copies per row, some 3; VK 128 -> exactly 2). */
static void pofo_stream_lcd_row(int sy)
{
    int H = lcd_out_h();
    int y0 = (sy * H) / 64;
    int y1 = ((sy + 1) * H) / 64;
    for (int y = y0; y < y1; y++)
        pofo_stream_line(y);
}

/* scratch for text-mode "pixel on" flags */
static uint8_t pofo_on_px[240];

/* Write a string into the HD61830 VRAM text buffer (native LCD font, same
 * renderer as DIP DOS). Row/col are character coordinates. This is how the
 * built-in screens are drawn — no direct display API, so no flicker and the
 * correct system font. */
static void pofo_put_text(int row, int col, const char *s)
{
    uint16_t base = (lcdc.dsa & 0xfff) + row * lcdc.hn;
    if (row < 0 || row >= (lcdc.nx / lcdc.vp)) return;
    for (int i = 0; s[i] && col + i < lcdc.hn; i++)
        lcdc.vram[base + col + i] = (uint8_t)s[i];
}

static void pofo_clear_text(void)
{
    for (int r = 0; r < (lcdc.nx / lcdc.vp); r++)
        for (int c = 0; c < lcdc.hn; c++)
            pofo_put_text(r, c, " ");
}

/* HD61830 internal chargen (external CG for Portfolio). MAME: offset =
 * (cl << 12) | md, and hd61830_rd_r returns charrom[((offset&0xff)<<4) |
 * ((offset>>12)&0x0f)] = charrom[(md << 4) | cl]. md = char code, cl =
 * scanline 0..vp-1, each glyph occupies 16 bytes. */
static uint8_t pofo_cg_read(int cl, uint8_t md)
{
    if (pofo_ru_mode)
        return pofo_chargen_ru[((md & 0xff) << 4) | (cl & 0x0f)];
    return pofo_chargen[((md & 0xff) << 4) | (cl & 0x0f)];
}

/* UART echo: вывод VRAM-текста в терминал (без ANSI-escape, построчно).
 * Каждый раз, когда содержимое VRAM меняется, выводим строки как текст. */
static uint32_t pofo_uart_crc = 0xFFFFFFFF;

static void pofo_uart_echo(void)
{
    int rows = lcdc.nx / lcdc.vp;
    if (rows <= 0) rows = 8;
    if (rows > 10) rows = 10;
    int cols = lcdc.hn;
    if (cols > 40) cols = 40;
    int n = cols * rows;

    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < n && i < HD61830_VRAM_SIZE; i++)
        crc = (crc >> 8) ^ ((crc ^ lcdc.vram[i]) * 0x100);

    if (crc == pofo_uart_crc) return;
    pofo_uart_crc = crc;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            uint8_t ch = lcdc.vram[r * lcdc.hn + c];
            if (ch < 0x20) ch = '.';
            if (ch >= 0x7f) ch = '.';
            putchar(ch);
        }
        putchar('\n');
    }
}

/* Справка внизу: компактный футер (LCD-область уже сдвинута на 224,
 * чтобы справка не перекрывала символы DIP DOS) */
static void pofo_draw_footer(void)
{
    display_fill_rect(0, 224, 320, 16, RGB565(0, 0, 6));
    display_text_at_nobg("PIN APPS HELP EXIT   INS:VK   ESC(hold):menu", 4, 225, 1, RGB565(28, 32, 38));
}

/* draw one text scanline (MAME draw_char). Returns pixel columns for row y. */
static void pofo_draw_char_line(int cl, uint8_t md, int x0)
{
    uint8_t data = pofo_cg_read(cl, md);
    for (int cr = 0; cr < lcdc.hp; cr++) {
        int sx = x0 + cr;
        if (sx < 240 && (data & (1 << cr)))
            pofo_on_px[sx] = 1;
    }
}

static void pofo_render_text(void)
{
    int rows = lcdc.nx / lcdc.vp;
    if (rows <= 0) rows = 8;
    uint16_t rac1 = lcdc.dsa & 0xfff;
    uint16_t rac2 = rac1 + (rows * lcdc.hn);

    for (int y = 0; y < rows; y++) {
        for (int cl = 0; cl < lcdc.vp; cl++) {
            int sy = y * lcdc.vp + cl;
            if (sy >= 64) break;
            memset(pofo_on_px, 0, sizeof(pofo_on_px));
            for (int x = 0; x < lcdc.hn; x += 2) {
                uint8_t md1 = (rac1 + x < HD61830_VRAM_SIZE) ? lcdc.vram[rac1 + x] : 0;
                uint8_t md2 = (rac1 + x + 1 < HD61830_VRAM_SIZE) ? lcdc.vram[rac1 + x + 1] : 0;
                pofo_draw_char_line(cl, md1, x * lcdc.hp);
                pofo_draw_char_line(cl, md2, (x + 1) * lcdc.hp);
            }
            for (int x = 0; x < 240; x++)
                pofo_line_rgb[x] = pofo_on_px[x] ? LCD_ON_RGB : LCD_OFF_RGB;
            pofo_stream_lcd_row(sy);
        }
        rac1 += lcdc.hn;   /* next text row reads the next VRAM block */
    }
}

extern "C" void pofo_render(void)
{
    if (lcdc.mcr & 0x02) {
        /* graphics mode — DIP DOS переключается в него для TUI-приложений.
         * Распаковывает HD61830 VRAM в пиксельные строки 240px с учётом
         * character pitch (hp) и горизонтального числа символов (hn). */
        uint16_t rac1 = lcdc.dsa;
        uint16_t rac2 = rac1 + (lcdc.nx * lcdc.hn);
        for (int y = 0; y < lcdc.nx && y < 64; y++) {
            for (int x = 0; x < 240; x++)
                pofo_line_rgb[x] = LCD_OFF_RGB;
            for (int sx = 0; sx < lcdc.hn; sx += 2) {
                uint8_t d1 = (rac1 < HD61830_VRAM_SIZE) ? lcdc.vram[rac1] : 0; rac1++;
                uint8_t d2 = (rac1 < HD61830_VRAM_SIZE) ? lcdc.vram[rac1] : 0; rac1++;
                for (int x = 0; x < lcdc.hp; x++) {
                    int px = sx * lcdc.hp + x;
                    if (px < 240 && (d1 & (1 << x)))
                        pofo_line_rgb[px] = LCD_ON_RGB;
                    int px2 = px + lcdc.hp;
                    if (px2 < 240 && (d2 & (1 << x)))
                        pofo_line_rgb[px2] = LCD_ON_RGB;
                }
            }
            pofo_stream_lcd_row(y);
        }
    } else {
        /* text mode — режим по умолчанию, используется DIP DOS и всеми
         * built-in экранами (PIN, APPS, HELP). */
        pofo_render_text();
    }
}

/* ------------------------------------------------------------------ */
/* Системный таймер 128 Гц → IRQ0 (INT 08h).
 * ASIC-счётчик тикает с реальной частотой 2 Гц (XTAL 32768/16384, см. MAME
 * pofo counter_tick). Синхронизация по wall-clock, не по кадрам — иначе
 * DIP DOS дата/время летят (per-frame ++ делает дни/часы быстрыми). */
static uint32_t pofo_counter_last_us = 0;

extern "C" void pofo_timer_tick(void)
{
    uint32_t now = time_us_32();
    if (!pofo_counter_last_us) pofo_counter_last_us = now;
    if (now - pofo_counter_last_us >= 500000) {   /* 2 Hz */
        pofo_counter_last_us += 500000;
        m_counter++;
    }
    if (m_ip & m_ie) {
        m_ip &= ~1;
        doirq(0);
    }
}

/* ------------------------------------------------------------------ */
/* Entry points used by main.cpp                                       */
/* ------------------------------------------------------------------ */
extern "C" int portfolio_init_game(const uint8_t *cart, uint32_t cart_size)
{
    (void)cart; (void)cart_size;
    pofo_exit_req = 0;
    pofo_prev_n = 0;
    memset(pofo_ram, 0, sizeof(pofo_ram));
    memset(lcdc.vram, 0, HD61830_VRAM_SIZE);
    memset(&lcdc, 0, sizeof(lcdc));
    lcdc.hp = 6;
    lcdc.vp = 1;
    lcdc.hn = 40;
    lcdc.nx = 32;

    m_ie = 0; m_ip = 0; m_contrast = 0x80;
    pofo_counter_last_us = 0;
    /* NOTE: m_counter is NOT reset here — it holds the DIP DOS date/time
     * counter, so a previously entered date survives across Portfolio entries
     * (until the Pico is rebooted). */
    m_rom_b = 0; m_sleep = false;
    m_kbd_data = 0xFF;
    memset(m_kop_state, 0, sizeof(m_kop_state));
    kbd_scan = 0xFF; kbd_pending = 0xFF;
    vk_visible = 0; vk_cur_r = 0; vk_cur_c = 0; vk_shift = 0; vk_dirty = 0;
    pofo_pin_active = 0;
    pofo_pin_frame = 0;
    pofo_apps_active = 0;
    pofo_app_current = -1;
    pofo_apps_sel = 0;
    pofo_apps_wait_release = 0;
    pofo_help_active = 0;
    pend_br_row = -1; pend_br_col = -1; pend_br_shift = 0;

    /* reset all frame-loop state so a second entry starts clean */
    pofo_boot_guard = 0;
    pofo_kbd_cand = 0xFF; pofo_kbd_stable = 0xFF; pofo_kbd_cnt = 0;
    memset(pofo_kbd_prev_cols, 0, sizeof(pofo_kbd_prev_cols));

    /* clear LCD area */
    display_fill(LCD_OFF_RGB);

    init8253();
    init8259();
    reset86();
    return 1;
}

extern "C" void portfolio_run_frame(void)
{
    /* Boot guard */
    if (pofo_boot_guard < 30) {
        pofo_boot_guard++;
        pofo_timer_tick();
        exec86(20000);
        pofo_render();
        return;
    }

    /* APPS mode — отдельная ветка */
    if (pofo_apps_active) {
        pofo_uart_input();
        pofo_usbkbd_input();
        pofo_apps_draw();
        pofo_render();
        if (vk_visible && vk_dirty) { vk_render(); vk_dirty = 0; }
        if (!vk_visible && pofo_app_current == APP_CALC) pofo_app_calc_draw_keys();
        return;
    }

    /* Проверка удержания ESC ~1 сек */
    if (pofo_esc_hold_us && (h3_hs_timer_lo_us() - pofo_esc_hold_us > 900000))
        pofo_exit_req = 1;

    /* Normal DOS mode — ТОЛЬКО USB-клавиатура и UART, без joypad */
    pofo_uart_input();
    pofo_usbkbd_input();

    if (vk_visible) {
        /* VK-навигация стрелками с USB-клавиатуры (из pofo_usbkbd_input уже обработаны) */
        vk_render();
        vk_dirty = 0;
    }

    pofo_timer_tick();
    exec86(20000);

    pofo_render();
    if (!vk_visible) pofo_draw_footer();
    pofo_uart_echo();
}


