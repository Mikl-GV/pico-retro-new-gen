// test_a5200.c — мини-BIOS (2K) + display list setup для 5200.
// Не требует копирайтного BIOS: векторы в $F800, DL в RAM.
#include <string.h>
#include "a5200.h"

// Мини-BIOS 2K ($F800-$FFFF): SEI, CLD, JMP $4000, векторы
__attribute__((aligned(256)))
static const uint8_t test_bios_5200[2048] = {
    [0x000] = 0x78,               // SEI
    [0x001] = 0xD8,               // CLD
    [0x002] = 0x4C, 0x00, 0x40,   // JMP $4000
    [0x3FC] = 0x00, 0xF8,         // RESET -> $F800
    [0x3FE] = 0x00, 0xF8,         // IRQ  -> $F800
};

// Картридж 16K: бесконечный цикл
__attribute__((aligned(256)))
static const uint8_t test_cart_5200[16384] = {
    [0x000] = 0x4C, 0x00, 0x40,
};

const uint8_t* test_a5200_get_bios(void) { return test_bios_5200; }
const uint8_t* test_a5200_get_cart(void) { return test_cart_5200; }

// Настройка display list в RAM: 24× mode 8 (160×2 bitmap, 2bpp), шахматка
void test_a5200_setup_dl(a5200_t* m) {
    uint8_t* ram = m->ram;
    for (int i = 0; i < 24; i++) {
        ram[0x3000 + i*3] = 0x48;   // LMS + mode 8
        uint16_t addr = 0x3100 + i*40;
        ram[0x3000 + i*3 + 1] = addr & 0xFF;
        ram[0x3000 + i*3 + 2] = addr >> 8;
    }
    ram[0x3000 + 72] = 0x41;        // JVB
    ram[0x3000 + 73] = 0x00;
    ram[0x3000 + 74] = 0x30;

    for (int b = 0; b < 24; b++)
        for (int r = 0; r < 40; r++) {
            uint8_t v = 0;
            for (int p = 0; p < 4; p++)
                v |= (((b + r + p) & 1) ? 1 : 2) << (6 - p*2);
            ram[0x3100 + b*40 + r] = v;
        }

    m->gtia[0x1A] = 0x04;  m->gtia[0x16] = 0x20;
    m->gtia[0x17] = 0x34;  m->antic[0x00] = 0x22;
    m->dlist = 0x3000;
}