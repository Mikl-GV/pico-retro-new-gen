/* Minimal emuapi.h for the Atari 5200 core (pico-retro adaptation).
 * All helpers are prefixed a5_ to avoid clashes with the Atari 2600
 * layer; implementations live in system_a5200.cpp. */
#ifndef EMUAPI_H
#define EMUAPI_H

#define MASK_JOY2_RIGHT 0x0001
#define MASK_JOY2_LEFT  0x0002
#define MASK_JOY2_UP    0x0004
#define MASK_JOY2_DOWN  0x0008
#define MASK_JOY2_BTN   0x0010
#define MASK_KEY_USER1  0x0020
#define MASK_KEY_USER2  0x0040
#define MASK_KEY_USER3  0x0080
#define MASK_JOY1_RIGHT 0x0100
#define MASK_JOY1_LEFT  0x0200
#define MASK_JOY1_UP    0x0400
#define MASK_JOY1_DOWN  0x0800
#define MASK_JOY1_BTN   0x1000
#define MASK_KEY_USER4  0x2000
#define MASK_OSKB       0x8000

void *a5_Malloc(int size);
void a5_Free(void *ptr);
void a5_printf(const char *text);
void a5_printi(int val);
int a5_ReadI2CKeyboard(void);
void a5_PaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index);
void a5_DrawLinePal16(unsigned char *VBuf, int width, int height, int line);
void a5_DrawVsync(void);
int a5_GetPad(void);

#endif
