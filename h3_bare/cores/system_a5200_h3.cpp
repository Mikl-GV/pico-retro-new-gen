// system_a5200_h3.cpp — host-слой Atari 5200 для H3.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "cheatdb.h"
}

#define EMU_FB   ((uint16_t*)0x5F800000)
#define EMU_W    320
#define EMU_H    240

extern "C" {
#include "a5200/atari.h"
#include "a5200/atari5200.h"
#include "a5200/antic.h"
#include "a5200/gtia.h"
#include "a5200/cpu.h"
#include "a5200/emucfg.h"
#include "a5200/emuapi.h"
}

// 64K memory + 4K extra
static uint8_t a5_memory_pool[65536 + 4096];
static int a5_pool_used = 0;

// C-глобалов ядра (объявлены extern в atari5200.h и др.). Определения без
// ключевого слова extern внутри extern "C" — иначе warning "initialized
// and declared extern" и двойная линковка.
extern "C" {
unsigned char *memory = 0;
const unsigned char *at5_rom = 0;
int at5_rom_size = 0;
int ik = 0;
}

extern "C" void *a5_Malloc(int size) {
    void *p = a5_memory_pool + a5_pool_used;
    if (a5_pool_used + size > (int)sizeof(a5_memory_pool)) return NULL;
    a5_pool_used += size;
    return p;
}
extern "C" void a5_Free(void*) {}
extern "C" void a5_printf(const char*) {}
extern "C" void a5_printi(int) {}
extern "C" int a5_ReadI2CKeyboard(void) { return 0; }

extern "C" void StateSav_SaveUBYTE(const void*) {}
extern "C" void StateSav_SaveUWORD(const void*) {}
extern "C" void StateSav_SaveINT(const void*) {}
extern "C" void StateSav_ReadUBYTE(const void*, int) {}
extern "C" void StateSav_ReadUWORD(const void*, int) {}
extern "C" void StateSav_ReadINT(const void*, int) {}
extern "C" void MEMORY_StateSave(void) {}
extern "C" void MEMORY_StateRead(void) {}
extern "C" int emu_FileOpen(const char*, const char*) { return 0; }
extern "C" int emu_FileGetc(int) { return 0xFF; }
extern "C" int emu_FileSeek(int, int, int) { return 0; }
extern "C" int emu_FileTell(int) { return 0; }
extern "C" void emu_FileClose(int) {}
extern "C" unsigned int emu_FileSize(const char*) { return 0; }
extern "C" int emu_FileRead(void*, int, int) { return 0; }
extern "C" void SndSave_CloseSoundFile(void) {}
extern "C" void SndSave_WriteToSoundFile(const unsigned char*, int) {}

static uint16_t a5_pal_rgb565[PALETTE_SIZE];

extern "C" void a5_PaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index) {
    if (index < PALETTE_SIZE)
        a5_pal_rgb565[index] = (r >> 3 << 11) | (g >> 2 << 5) | (b >> 3);
}

extern "C" void a5_DrawLinePal16(unsigned char *VBuf, int width, int height, int line) {
    (void)width; (void)height;
    if (!VBuf || line < 0 || line >= EMU_H) return;
    for (int x = 0; x < 320; x++)
        EMU_FB[line * EMU_W + x] = a5_pal_rgb565[VBuf[x] & 0xFF];
}

extern "C" void a5_DrawVsync(void) {}

extern "C" int a5_GetPad(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    int k = 0;

    // Sega-геймпад: крестовина, A=Fire, B=Pause, Start=Start, Mode=Start
    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) k |= 0x0004;   // Up
    if (sp & 0x0002) k |= 0x0008;   // Down
    if (sp & 0x0004) k |= 0x0002;   // Left
    if (sp & 0x0008) k |= 0x0001;   // Right
    if (sp & 0x0010) k |= 0x0010;   // A -> Fire
    if (sp & 0x0020) k |= 0x0020;   // B -> Pause
    if (sp & 0x0080) k |= 0x0040;   // Start
    if (sp & 0x0800) k |= 0x0040;   // Mode -> Start

    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        if (sc == 82) k |= 0x0004;
        if (sc == 81) k |= 0x0008;
        if (sc == 80) k |= 0x0002;
        if (sc == 79) k |= 0x0001;
        if (sc == 29) k |= 0x0010;
        if (sc == 27) k |= 0x0020;
        if (sc == 22) k |= 0x0040;
        if (sc == 40) k |= 0x0080;
    }
    return k;
}

extern "C" int a5200_init_game(const uint8_t *rom, uint32_t size) {
    a5_pool_used = 0;
    for (int i = 0; i < PALETTE_SIZE; i++) a5_pal_rgb565[i] = 0;

    memory = (unsigned char *)a5_Malloc(65536);
    if (!memory) { printf("[a5200] no pool\n"); return 0; }
    memset(memory, 0, 65536);

    at5_rom = rom;
    at5_rom_size = (int)size;
    ik = 0;

    at5_Init();
    at5_Start((char *)"cart");
    printf("[a5200] loaded size=%d\n", (int)size);
    return 1;
}

extern "C" void a5200_run_frame(void) {
    at5_Input(0);
    // RAW-читы: пишем байт каждый кадр в RAM 64K
    int rc = cheats_raw_count();
    for (int i = 0; i < rc; i++) {
        uint32_t a; uint8_t v, c; int hc;
        if (cheats_raw_get(i, &a, &v, &c, &hc)) {
            a &= 0xFFFF;
            if (a < 65536) {
                if (!hc || memory[a] == c) memory[a] = v;
            }
        }
    }
    at5_Step();
}