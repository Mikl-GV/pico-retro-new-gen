// nes_host.cpp — хост-слой InfoNES для H3 (bare-metal Orange Pi Lite).
// InfoNES_System.h callbacks: ROM loading, frame buffer 256×240 → EMU_FB, input.

#include "InfoNES_System.h"
#include "InfoNES.h"
#include "InfoNES_Mapper.h"
#include "InfoNES_pAPU.h"
#include "K6502.h"

#include <cstring>
#include <cstdarg>
#include <cstdio>
#include "h3_hs_timer.h"

extern "C" {
extern int printf(const char* fmt, ...);
int usb_kbd_get_raw(uint8_t* buf, int max);
int uart_rx_ready(void);
void fb_flush(void);
void fb_clear(void);
void emu_throttle(void);

extern void emu_scale(int src_w, int src_h, int scale);
}

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// per-line render buffer (set by InfoNES_SetLineBuffer)
static uint16_t line_buf[NES_DISP_WIDTH];
extern WORD *WorkLine;
void InfoNES_SetLineBuffer(WORD *p, WORD size);

static uint8_t screen[NES_DISP_HEIGHT][NES_DISP_WIDTH];

static uint16_t nes_pal_rgb565[64];
static bool pal_ready = false;
static uint32_t frame_cnt = 0;

static void init_palette(void) {
    static const uint8_t pal[64][3] = {
        {84,84,84},{0,30,116},{8,16,144},{48,0,136},{68,0,100},{92,0,48},{84,4,0},{60,24,0},
        {32,42,0},{8,58,0},{0,64,0},{0,60,0},{0,50,60},{0,0,0},{0,0,0},{0,0,0},
        {152,150,152},{8,76,196},{48,50,236},{92,30,228},{136,20,176},{160,20,100},{152,34,32},{120,60,0},
        {84,90,0},{40,114,0},{8,124,0},{0,118,40},{0,102,120},{0,0,0},{0,0,0},{0,0,0},
        {236,238,236},{76,154,236},{120,124,236},{176,98,236},{228,84,236},{236,88,180},{236,106,100},{212,136,32},
        {160,170,0},{116,196,0},{76,208,32},{56,204,108},{56,180,204},{60,60,60},{0,0,0},{0,0,0},
        {236,238,236},{168,204,236},{188,188,236},{212,178,236},{236,174,236},{236,174,212},{236,180,176},{228,196,144},
        {204,210,120},{180,222,120},{168,226,144},{152,226,180},{160,214,228},{160,162,160},{0,0,0},{0,0,0},
    };
    for (int i = 0; i < 64; i++)
        nes_pal_rgb565[i] = (pal[i][0]>>3<<11) | (pal[i][1]>>2<<5) | (pal[i][2]>>3);
    pal_ready = true;
}

// NES palette index (identity mapping → RGB565 via lut)
const BYTE NesPalette[64] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f
};

// ---- InfoNES callbacks ----

void InfoNES_PreDrawLine(int) {}

void InfoNES_PostDrawLine(int line) {
    for (int x = 0; x < NES_DISP_WIDTH; x++)
        screen[line][x] = (uint8_t)(line_buf[x] & 0xFF);
}

int InfoNES_LoadFrame(void) {
    // Render 256×240 in left-top corner of EMU_FB
    for (int y = 0; y < NES_DISP_HEIGHT; y++)
        for (int x = 0; x < NES_DISP_WIDTH; x++)
            EMU_FB[y * EMU_W + x] = nes_pal_rgb565[screen[y][x] & 0x3F];
    emu_throttle();
    emu_scale(256, 240, 2);
    fb_flush();

    if (++frame_cnt % 60 == 0)
        printf("nes f=%u\n", (unsigned)frame_cnt);
    return 0;
}

int InfoNES_Menu(void) { return 0; }
int InfoNES_Video(void) { return 0; }   // нет меню выбора видео — сразу игра
int InfoNES_ReadRom(const char *) { return 0; }
void InfoNES_ReleaseRom(void) {}

void InfoNES_PadState(DWORD *pw1, DWORD *pw2, DWORD *pwSys) {
    *pw1 = 0; *pw2 = 0; *pwSys = 0;
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        // NES $4016 serialisation order: bit0=A bit1=B bit2=Sel bit3=Strt bit4=Up bit5=Dn bit6=Lt bit7=Rt
        if (sc == 82)      *pw1 |= 0x10; // UArrow = Up (bit4)
        if (sc == 81)      *pw1 |= 0x20; // DArrow = Down (bit5)
        if (sc == 80)      *pw1 |= 0x40; // LArrow = Left (bit6)
        if (sc == 79)      *pw1 |= 0x80; // RArrow = Right (bit7)
        if (sc == 29)      *pw1 |= 0x01; // Z = A (bit0)
        if (sc == 27)      *pw1 |= 0x02; // X = B (bit1)
        if (sc == 40)      *pw1 |= 0x08; // Enter = Start (bit3)
        if (sc == 22)      *pw1 |= 0x04; // S = Select (bit2)
        if (sc == 41)      *pwSys = PAD_SYS_QUIT; // ESC = exit
    }
}

void InfoNES_MessageBox(const char *m, ...) { printf("%s", m); }
void InfoNES_Error(const char *m, ...) { printf("%s", m); }
void InfoNES_DebugPrint(const char *m) { printf("%s", m); }
void InfoNES_SoundInit(void) {}
int InfoNES_SoundOpen(int,int) { return 0; }
void InfoNES_SoundClose(void) {}
void InfoNES_SoundOutput(int, const BYTE*, const BYTE*, const BYTE*, const BYTE*, const BYTE*) {}
int InfoNES_GetSoundBufferSize() { return 0; }

extern "C" {

void nes_init(const uint8_t *rom, uint32_t sz) {
    if (!pal_ready) init_palette();
    memset(screen, 0, sizeof(screen));
    InfoNES_SetLineBuffer(line_buf, NES_DISP_WIDTH);
    NesHeader.byID[0]=rom[0]; NesHeader.byID[1]=rom[1];
    NesHeader.byID[2]=rom[2]; NesHeader.byID[3]=rom[3];
    NesHeader.byRomSize=rom[4]; NesHeader.byVRomSize=rom[5];
    NesHeader.byInfo1=rom[6]; NesHeader.byInfo2=rom[7];
    for(int i=0;i<8;i++) NesHeader.byReserve[i]=rom[8+i];
    ROM = (BYTE*)(rom + 16);
    VROM = NesHeader.byVRomSize ? (BYTE*)(rom + 16 + NesHeader.byRomSize * 0x4000) : NULL;
    InfoNES_Init();
    InfoNES_Reset();
    printf("NES init: mapper=%d PRG=%dK CHR=%dK\n",
           (int)MapperNo, (int)NesHeader.byRomSize*16, (int)NesHeader.byVRomSize*8);
}

void nes_frame(void) {
    InfoNES_Cycle();   // крутит кадры до выхода (ESC -> PAD_SYS_QUIT)
}

void nes_stop(void) {
    InfoNES_ReleaseRom();
}

// main loop for NES. InfoNES_Cycle() runs frame loop internally,
// exits on PAD_SYS_QUIT (ESC). Throttle + scale in InfoNES_LoadFrame.
void emu_run_nes(const uint8_t* rom, uint32_t size, const char* rom_name) {
    (void)rom_name;
    fb_clear(); fb_flush();
    nes_init(rom, size);
    nes_frame();
    nes_stop();
    fb_clear(); fb_flush();
}

} // extern "C"