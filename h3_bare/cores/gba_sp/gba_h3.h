#ifndef GBA_H3_H
#define GBA_H3_H

#include <stdint.h>

// Интерфейс между gpSP и H3 bare-metal
void *fb_flip_screen(void);
u32 get_screen_pitch(void);
uint16_t *get_screen_pixels(void);
void fb_set_mode(int w, int h, int buffers, int scale, int filter, int filter2);
void fb_wait_vsync(void);
void gpsp_plat_init(void);
void gpsp_plat_quit(void);
u32 gpsp_plat_joystick_read(void);
u32 gpsp_plat_buttons_to_cursor(u32 buttons);

// Точки входа для rom_browser
int gba_init_game(const uint8_t* rom, uint32_t size);
void gba_run_frame(void);
void gba_render_frame(void);
int gba_exit_requested(void);

#endif