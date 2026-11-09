#include <string.h>
#include <stdint.h>

#include "rk86_machine.h"
#include "i8080.h"

extern const unsigned char rk86_monitor_rom[2048];
extern const unsigned char rk86_font_rom[1024];

#define RAM_MASK  0x7FFFu
#define RAM2_MASK 0x1FFFu
#define ROM_MASK  0x1FFFu

static uint8_t s_ram[0x8000];
static uint8_t s_ram2[0x2000];
static uint8_t s_rom[0x2000];
static uint8_t s_font[4][1024];

rk86_screen_t rk86_screen;
uint32_t i8080_cycles;

static uint8_t s_scanline;
static uint8_t s_mods = 0xFF;
static uint8_t s_kbd[8];
static uint8_t s_rus;

static uint8_t s_vg_cmd, s_vg_n, s_vg_par[4];
static uint8_t s_ik_n, s_ik_par[4];

// Флаги состояния ВГ75 (порт 1, чтение SREG).
#define SREG_IE 0x40
#define SREG_IR 0x20
#define SREG_LP 0x10
#define SREG_IC 0x08
#define SREG_VE 0x04
#define SREG_DU 0x02
#define SREG_FO 0x01

#define MOD_SS 0x20
#define MOD_US 0x40
#define MOD_RL 0x80

static uint8_t vv55_r(int a)
{
    switch (a) {
    case 0:
        return s_scanline;
    case 1: {
        uint8_t scan = (uint8_t)~s_scanline;
        uint8_t v = 0xFF;
        for (int i = 0; i < 8; i++)
            if (scan & (1 << i)) v &= s_kbd[i];
        return v;
    }
    case 2:
        // Бит 0x10 — вход ленты (не подключён), модификаторы активны низким.
        return (uint8_t)(s_mods & 0xEF);
    default:
        return 0x00;
    }
}

static void vv55_w(int a, uint8_t v)
{
    if (a == 0) {
        s_scanline = v;
    } else if (a == 3) {
        // Порт управления: при активном стробировании бит 1..3 — номер
        // вывода, 0 — значение. Вывод 3 — переключатель РУС/ЛАТ.
        if (!(v & 0x80)) {
            int pin = (v >> 1) & 0x07;
            if (pin == 3) s_rus = (uint8_t)(v & 0x01);
        }
    }
}

static void vg75_w(int a, uint8_t v)
{
    if (a) {
        s_vg_cmd = v;
        s_vg_n = 0;
        if ((v & 0xE0) == 0x20) {
            // Пуск/останов вывода изображения (DMA).
            if (((v >> 2) & 0x07) != 0) {
                rk86_screen.dma_burst = (uint8_t)(1u << (v & 0x03));
                rk86_screen.sreg |= SREG_VE;
            } else {
                rk86_screen.sreg &= (uint8_t)~SREG_VE;
            }
        } else if (v == 0xA0) {
            rk86_screen.sreg |= SREG_IE;
        } else if (v == 0xC0) {
            rk86_screen.sreg &= (uint8_t)~SREG_IE;
        }
        return;
    }

    s_vg_par[s_vg_n & 0x03] = v;
    s_vg_n++;

    if (s_vg_cmd == 0x00 && s_vg_n == 4) {
        rk86_screen.screen_w = (uint8_t)((s_vg_par[0] & 0x7F) + 1);
        rk86_screen.screen_h = (uint8_t)((s_vg_par[1] & 0x3F) + 1);
        rk86_screen.underline_y = (uint8_t)(s_vg_par[2] >> 4);
        rk86_screen.char_h = (uint8_t)((s_vg_par[2] & 0x0F) + 1);
        rk86_screen.cursor_type = (uint8_t)((s_vg_par[3] >> 4) & 0x03);
        rk86_screen.attr_visible = (uint8_t)((s_vg_par[3] & 0x40) != 0);
        if (rk86_screen.screen_w > RK86_COLS) rk86_screen.screen_w = RK86_COLS;
    } else if (s_vg_cmd == 0x80 && s_vg_n == 2) {
        rk86_screen.cursor_x = s_vg_par[0];
        rk86_screen.cursor_y = s_vg_par[1];
    }
}

static uint8_t vg75_r(int a)
{
    if (a) {
        uint8_t v = rk86_screen.sreg;
        // Строб-биты сбрасываются после чтения.
        rk86_screen.sreg &= (uint8_t)~(SREG_IR | SREG_LP | SREG_IC | SREG_DU | SREG_FO);
        return v;
    }
    return 0x00;
}

// КР580ВТ57: программируемый контроллер ПДП, задаёт адрес видео-ОЗУ.
static void ik57_w(int a, uint8_t v)
{
    if (a == 0x08) {
        s_ik_n = 0;
    } else if (a == 0x04 || a == 0x05) {
        s_ik_par[s_ik_n & 0x03] = v;
        s_ik_n++;
        if (s_ik_n == 4) {
            uint16_t at = (uint16_t)((s_ik_par[1] << 8) | s_ik_par[0]);
            rk86_screen.vram = s_ram + (at & RAM_MASK);
            s_ik_n = 0;
        }
    }
}

int i8080_hal_memory_read_byte(int addr)
{
    addr &= 0xFFFF;
    if (!(addr & 0x8000)) return s_ram[addr & RAM_MASK];

    switch ((addr >> 12) & 0x0F) {
    case 0x8:
    case 0x9:
        return vv55_r(addr & 0x03);

    case 0xA:
    case 0xB:
        return s_ram2[addr & RAM2_MASK];

    case 0xC:
    case 0xD:
        if (addr & (1 << 10)) {
            // Знакогенератор: A12..A11 — страница, адрес внутри —
            // построчный (символ c, строка l → l*128 + c).
            int n = (addr >> 11) & 0x03;
            int off = addr & 0x3FF;
            off = ((off & 0x07) << 7) | (off >> 3);
            return s_font[n][off];
        }
        return vg75_r(addr & 0x01);

    default:
        return s_rom[addr & ROM_MASK];
    }
}

void i8080_hal_memory_write_byte(int addr, int byte)
{
    addr &= 0xFFFF;
    uint8_t v = (uint8_t)byte;

    if (!(addr & 0x8000)) {
        s_ram[addr & RAM_MASK] = v;
        return;
    }

    switch ((addr >> 12) & 0x0F) {
    case 0x8:
    case 0x9:
        vv55_w(addr & 0x03, v);
        break;

    case 0xA:
    case 0xB:
        s_ram2[addr & RAM2_MASK] = v;
        break;

    case 0xC:
    case 0xD:
        if (addr & (1 << 10)) {
            int n = (addr >> 11) & 0x03;
            int off = addr & 0x3FF;
            off = ((off & 0x07) << 7) | (off >> 3);
            if (n != 0) s_font[n][off] = v;
        } else {
            vg75_w(addr & 0x01, v);
        }
        break;

    case 0xE:
        ik57_w(addr & 0x0F, v);
        break;

    default:
        // ПЗУ — запись игнорируется.
        break;
    }
}

int i8080_hal_memory_read_word(int addr)
{
    return (i8080_hal_memory_read_byte(addr + 1) << 8) |
            i8080_hal_memory_read_byte(addr);
}

void i8080_hal_memory_write_word(int addr, int word)
{
    i8080_hal_memory_write_byte(addr, word & 0xFF);
    i8080_hal_memory_write_byte(addr + 1, (word >> 8) & 0xFF);
}

int i8080_hal_io_input(int port)
{
    (void)port;
    return 0;
}

void i8080_hal_io_output(int port, int value)
{
    (void)port;
    (void)value;
}

void i8080_hal_iff(int on)
{
    // Звука и ленты нет: вывод ленты (ТЧ) не подключён.
    (void)on;
}

unsigned char* i8080_hal_memory(void)
{
    return s_ram;
}

unsigned char* i8080_hal_rom(void)
{
    return s_rom;
}

void rk86_kbd_press(uint16_t code)
{
    if (code >> 12) {
        s_mods &= (uint8_t)~((code >> 8) & 0xF0);
    } else {
        s_kbd[(code >> 8) & 0x07] &= (uint8_t)~(code & 0xFF);
    }
}

void rk86_kbd_release(uint16_t code)
{
    if (code >> 12) {
        s_mods |= (uint8_t)((code >> 8) & 0xF0);
    } else {
        s_kbd[(code >> 8) & 0x07] |= (uint8_t)(code & 0xFF);
    }
}

void rk86_kbd_release_all(void)
{
    memset(s_kbd, 0xFF, sizeof(s_kbd));
    s_mods = 0xFF;
}

void rk86_kbd_set_mods(int ss, int us, int rl)
{
    if (ss) s_mods &= (uint8_t)~MOD_SS; else s_mods |= MOD_SS;
    if (us) s_mods &= (uint8_t)~MOD_US; else s_mods |= MOD_US;
    if (rl) s_mods &= (uint8_t)~MOD_RL; else s_mods |= MOD_RL;
}

void rk86_init(const uint8_t* rom, uint32_t size)
{
    memset(s_ram, 0, sizeof(s_ram));
    memset(s_ram2, 0, sizeof(s_ram2));
    memset(s_rom, 0xFF, 0x1800);
    memcpy(s_rom + 0x1800, rk86_monitor_rom, 0x800);

    memcpy(s_font[0], rk86_font_rom, sizeof(s_font[0]));
    memset(s_font[1], 0, sizeof(s_font[1]));
    memset(s_font[2], 0, sizeof(s_font[2]));
    memset(s_font[3], 0, sizeof(s_font[3]));

    if (rom && size) {
        if (size <= 0x800) {
            memcpy(s_rom + 0x1800, rom, size);      // образ монитора
        } else if (size <= sizeof(s_ram)) {
            memcpy(s_ram, rom, size);               // образ ОЗУ/программа
        }
    }

    memset(&rk86_screen, 0, sizeof(rk86_screen));
    rk86_screen.screen_w = RK86_COLS;
    rk86_screen.screen_h = RK86_ROWS;
    rk86_screen.underline_y = 7;
    rk86_screen.char_h = 8;
    rk86_screen.dma_burst = 1;
    rk86_screen.vram = s_ram;

    s_scanline = 0;
    s_mods = 0xFF;
    memset(s_kbd, 0xFF, sizeof(s_kbd));
    s_rus = 0;
    s_vg_cmd = 0;
    s_vg_n = 0;
    s_ik_n = 0;
    i8080_cycles = 0;

    i8080_init();
    i8080_jump(0xF800);
}

void rk86_run(uint32_t cycles)
{
    uint32_t done = 0;
    while (done < cycles) done += (uint32_t)i8080_instruction();
}

void rk86_vg75_vsync(void)
{
    rk86_screen.sreg |= SREG_IR;
}

const uint8_t* rk86_font_page(int n)
{
    return s_font[n & 0x03];
}

// Расширение в конце имени (без учёта регистра), "" — нет точки.
static int ext_is(const char* name, const char* ext)
{
    if (!name) return 0;
    const char* dot = strrchr(name, '.');
    const char* e = dot ? dot + 1 : name + strlen(name);
    size_t n = strlen(ext);
    if (strlen(e) != n) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = e[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != ext[i]) return 0;
    }
    return dot != NULL;
}

uint32_t rk86_load_program(const uint8_t* data, uint32_t size, const char* name)
{
    if (!data || size < 4) return 0;

    uint32_t start, len, entry, off;

    // .bin и файлы без расширения — сырой образ с адреса 0.
    if (ext_is(name, "bin") || (!strrchr(name ? name : "", '.'))) {
        start = 0;
        off = 0;
        len = size > sizeof(s_ram) ? (uint32_t)sizeof(s_ram) : size;
        entry = 0;
    } else {
        // .rk/.rkr/.gam/.pki: [0xE6] + start(BE) + end(BE) + данные.
        off = (data[0] == 0xE6) ? 1 : 0;
        if (size < off + 4) return 0;
        start = ((uint32_t)data[off] << 8) | data[off + 1];
        uint32_t end = ((uint32_t)data[off + 2] << 8) | data[off + 3];
        off += 4;
        if (end < start || off >= size) return 0;
        len = end - start + 1;
        if (len > size - off) len = size - off;
        entry = start;
    }

    if (start >= sizeof(s_ram)) return 0;
    if (len > sizeof(s_ram) - start) len = (uint32_t)sizeof(s_ram) - start;

    memcpy(s_ram + start, data + off, len);
    i8080_init();
    i8080_jump((int)entry);
    return entry + 1;
}
