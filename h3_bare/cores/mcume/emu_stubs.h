#ifndef EMU_STUBS_H
#define EMU_STUBS_H

/*
 * Declarations for the pico-retro emuapi stubs implemented in
 * system_atari_mcume.cpp. Included from the C sources of the core.
 */

/* Joystick/key bit masks (values from MCUME emuapi.h). */
#define MASK_JOY2_RIGHT 0x0001
#define MASK_JOY2_LEFT  0x0002
#define MASK_JOY2_UP    0x0004
#define MASK_JOY2_DOWN  0x0008
#define MASK_JOY2_BTN   0x0010
#define MASK_KEY_USER1  0x0020
#define MASK_KEY_USER2  0x0040
#define MASK_KEY_USER3  0x0080
#define MASK_OSKB       0x8000

void *emu_Malloc(int size);
void emu_Free(void *pt);
unsigned int emu_LoadFile(const char *filepath, void *buf, int size);
int emu_GetPad(void);
int emu_ReadI2CKeyboard(void);
void emu_KeyboardOnUp(int keymodifer, int key);
void emu_KeyboardOnDown(int keymodifer, int key);
void emu_printf(const char *text);
void emu_printi(int val);
void emu_SetPaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index);
void emu_DrawScreenPal16(unsigned char *VBuf, int width, int height, int stride);
void emu_DrawVsync(void);
int emu_FrameSkip(void);
int emu_IsVga(void);
void emu_sndInit(void);
void emu_sndPlaySound(int chan, int volume, int freq);
void emu_sndPlayBuzz(int size, int val);

/* Keyboard input helpers (defined in Keyboard.c, used by Memory.c). */
void vcs_Input(int key);
void keyjoy(void);
void keycons(void);
void keytrig(void);

#endif
