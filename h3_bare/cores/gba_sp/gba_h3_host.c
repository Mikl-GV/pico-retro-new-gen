// gba_h3_host.c — host-слой gpSP для H3 (Orange Pi Lite, bare-metal).
// Инициализирует ядро gpSP, грузит ROM из буфера rom_browser и гоняет
// кадры через update_gba(). Рендер: gpSP пишет в свой буфер (screen_pixels,
// получает его из fb_flip_screen()), мы копируем в EMU_FB с масштабом 2x.

#include <stdint.h>
#include <string.h>

#include "gba_h3_config.h"
#include "common.h"

// EMU_FB (320x240) — целевой буфер для emu_scale
#define EMU_FB ((uint16_t*)0x5F800000)
#define EMU_W   320
#define GBA_W   240
#define GBA_H   160

static uint16_t gba_fb[GBA_W * GBA_H];
static int gba_initialized = 0;
static int gba_exit = 0;

// ---- API для rom_browser / emu.c ----
extern "C" int gba_init_game(const uint8_t* rom, uint32_t size) {
    extern u8 *gamepak_rom;
    extern u32 gamepak_size;
    extern u32 gamepak_ram_buffer_size;
    extern u8 bios_rom[0x4000];
    extern void init_gamepak_buffer(void);
    extern void init_main(void);
    extern void init_sound(int);
    extern void init_input(void);
    extern void init_cpu(void);
    extern void init_memory(void);
    extern void init_memory_gamepak(void);
    extern void set_gba_resolution(int);
    extern void bios_region_read_allow(void);

    init_gamepak_buffer();

    if (size > gamepak_ram_buffer_size)
        size = gamepak_ram_buffer_size;
    memcpy(gamepak_rom, rom, size);
    gamepak_size = (size + 0x7FFF) & ~0x7FFF;

    // BIOS пока пустой (нужен оригинальный gba_bios.bin для полной скорости).
    memset(bios_rom, 0, 0x4000);
    bios_rom[0] = 0x18;   // ARM branch — обходит проверку в main.c

    init_main();
    init_sound(0);
    init_input();

    set_gba_resolution(1);

    init_cpu();
    init_memory();
    init_memory_gamepak();
    bios_region_read_allow();

    gba_initialized = 1;
    gba_exit = 0;
    return 1;
}

extern "C" void gba_run_frame(void) {
    if (!gba_initialized) return;
    extern u32 update_gba(void);
    update_gba();
}

extern "C" void gba_render_frame(void) {
    if (!gba_initialized) return;
    // gpSP записал кадр в gba_fb (через fb_flip_screen). Копируем 2x в EMU_FB.
    for (int y = 0; y < GBA_H; y++) {
        for (int x = 0; x < GBA_W; x++) {
            uint16_t p = gba_fb[y * GBA_W + x];
            EMU_FB[(y * 2) * EMU_W + (x * 2)] = p;
            EMU_FB[(y * 2) * EMU_W + (x * 2 + 1)] = p;
            EMU_FB[(y * 2 + 1) * EMU_W + (x * 2)] = p;
            EMU_FB[(y * 2 + 1) * EMU_W + (x * 2 + 1)] = p;
        }
    }
}

extern "C" int gba_exit_requested(void) { return gba_exit; }
extern "C" void gba_set_exit(int e) { gba_exit = e; }

// ---- платформенные функции, которые ждёт ядро gpSP ----

void gpsp_plat_init(void) {}
void gpsp_plat_quit(void) {}

void *fb_flip_screen(void) { return gba_fb; }
u32 get_screen_pitch(void) { return GBA_W; }
void fb_set_mode(int w, int h, int b, int s, int f, int f2) {}
void fb_wait_vsync(void) {}
u32 gpsp_plat_joystick_read(void) { return 0; }
u32 gpsp_plat_buttons_to_cursor(u32 b) { (void)b; return 0; }

// ядро всё равно дергает get_screen_pixels()/get_screen_pitch() как макросы,
// но на всякий случай дадим и функции:
uint16_t *get_screen_pixels(void) { return gba_fb; }

// ---- заглушки main.c, без которых не соберётся линковка ----
// (вся настоящая работа с кадрами идёт из gba_run_frame)

void quit(void) { gba_exit = 1; }
void delay_us(u32 us) { (void)us; }

u32 menu(u16 *screen) { (void)screen; return 0; }
void debug_screen_start(void) {}
void debug_screen_end(void) {}
void debug_screen_update(void) {}
void debug_screen_printl(const char *s) { (void)s; }
void debug_screen_printf(const char *f, ...) { (void)f; }
void debug_screen_clear(void) {}
void debug_screen_newline(u32 n) { (void)n; }
void debug_off(u32 state) { (void)state; }
void debug_on(void) {}
void print_string(const char *s, u16 fg, u16 bg, u32 x, u32 y) {
    (void)s; (void)fg; (void)bg; (void)x; (void)y;
}
void print_string_pad(const char *s, u16 fg, u16 bg, u32 x, u32 y, u32 p) {
    (void)s; (void)fg; (void)bg; (void)x; (void)y; (void)p;
}
u16 *copy_screen(void) {
    u16 *c = (u16*)malloc(GBA_W * GBA_H * 2);
    if (c) memcpy(c, gba_fb, GBA_W * GBA_H * 2);
    return c;
}
void load_state(char *fn) { (void)fn; }
void save_state(char *fn, u16 *sc) { (void)fn; (void)sc; }
void get_savestate_filename_noshot(u32 slot, char *buf) { (void)slot; buf[0] = 0; }
void get_savestate_filename(u32 slot, char *buf) { (void)slot; buf[0] = 0; }
void synchronize(void) {}
void update_backup(void) {}
void process_cheats(void) {}
u32 adjust_frameskip(u32 b) { (void)b; return 0; }
void change_ext(const char *src, char *dst, const char *ext) {
    (void)ext; strcpy(dst, src);
}
void make_rpath(char *buf, size_t sz, const char *ext) { (void)ext; buf[0] = 0; }
s32 save_game_config_file(void) { return -1; }
s32 load_game_config_file(void) { return -1; }
s32 save_config_file(void) { return -1; }
s32 load_config_file(void) { return -1; }
s32 load_file(const char **wc, char *result) { (void)wc; result[0] = 0; return -1; }
s32 load_game_config(char *t, char *c, char *m) { (void)t; (void)c; (void)m; return -1; }
u32 gpsp_exit_game_state(void) { return 0; }
void *malloc_aligned(size_t sz, int align) { (void)align; return malloc(sz); }
char main_path[512];
u32 savestate_slot = 0;