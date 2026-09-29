// pce_host.c — Beetle PCE Fast (mednafen_pce_fast, HuCard) host for H3 bare-metal.
//
// Ядро: h3_bare/cores/pce_fast/mednafen + враппер mednafen libretro.c
// (там же — машинная обвязка PCE: шина/маппер/IO/save-ram).
// Мы НЕ тянем RetroArch/libretro-common: pce_host.c выступает тонким
// libretro-frontend'ом — определяет retro_* callbacks + environment,
// вызывает враппер (retro_init / retro_load_game / retro_run), а кадр RGB565
// из retro_video_refresh переносит в EMU_FB (320x240) -> emu_scale().
// Звук ОТКЛЮЧЁН (audio_batch_cb игнорирует сэмплы).
// CD не поддерживается (HuCard only): ROM подаётся буфером через
// RETRO_ENVIRONMENT_GET_GAME_INFO_EXT — файловой работы нет.
//
// Ввод: крестовина + II (Z) / I (X) / Run (Enter) / Select (S); Sega-пад:
// B=I, C=II, Start=Run, X=Select.

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "libretro_inc/libretro.h"

#include "emu.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "remap.h"
#include "fb_text.h"

// Ядро (враппер mednafen), определено в pce_fast/libretro.c
void retro_set_environment(retro_environment_t);
void retro_set_video_refresh(retro_video_refresh_t);
void retro_set_audio_sample(retro_audio_sample_t);
void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void retro_set_input_poll(retro_input_poll_t);
void retro_set_input_state(retro_input_state_t);
void retro_init(void);
bool retro_load_game(const struct retro_game_info*);
void retro_run(void);
void retro_unload_game(void);
void retro_deinit(void);

#define PCE_SCALE_W 256
#define PCE_SCALE_H 240

#define EMU_FB_ADDR 0x5F800000u
#define EMU_FB_W    320
#define EMU_FB_H    240

// ---- ROM: буфер из rom_browser (ROM_BUF) — отдаётся врапперу через env ----
static const void* g_rom_data = 0;
static size_t      g_rom_size = 0;

// ---- Input: маска RETRO_DEVICE_ID_JOYPAD ----
static uint16_t g_joy = 0;

static void host_update_input(void)
{
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    uint16_t j = 0;

    if (remap_kbd_pressed(REMAP_PLAT_PCE, BTN_UP,     keys, n)) j |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
    if (remap_kbd_pressed(REMAP_PLAT_PCE, BTN_DOWN,   keys, n)) j |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (remap_kbd_pressed(REMAP_PLAT_PCE, BTN_LEFT,   keys, n)) j |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (remap_kbd_pressed(REMAP_PLAT_PCE, BTN_RIGHT,  keys, n)) j |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (remap_kbd_pressed(REMAP_PLAT_PCE, BTN_A,      keys, n)) j |= (1u << RETRO_DEVICE_ID_JOYPAD_A);       // I
    if (remap_kbd_pressed(REMAP_PLAT_PCE, BTN_B,      keys, n)) j |= (1u << RETRO_DEVICE_ID_JOYPAD_B);       // II
    if (remap_kbd_pressed(REMAP_PLAT_PCE, BTN_START,  keys, n)) j |= (1u << RETRO_DEVICE_ID_JOYPAD_START);   // Run
    if (remap_kbd_pressed(REMAP_PLAT_PCE, BTN_SELECT, keys, n)) j |= (1u << RETRO_DEVICE_ID_JOYPAD_SELECT);  // Select

    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) j |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
    if (sp & 0x0002) j |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (sp & 0x0004) j |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (sp & 0x0008) j |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (sp & 0x0010) j |= (1u << RETRO_DEVICE_ID_JOYPAD_A);      // A      -> I
    if (sp & 0x0020) j |= (1u << RETRO_DEVICE_ID_JOYPAD_B);      // B      -> II
    if (sp & 0x0080) j |= (1u << RETRO_DEVICE_ID_JOYPAD_START);  // Start  -> Run
    if (sp & 0x0100) j |= (1u << RETRO_DEVICE_ID_JOYPAD_SELECT); // X      -> Select

    g_joy = j;
}

// ---- libretro callbacks ----
static void host_input_poll(void)
{
    host_update_input();
}

static int16_t host_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (device == RETRO_DEVICE_JOYPAD && port == 0)
        return (g_joy & (1u << id)) ? 1 : 0;
    return 0;
}

static void host_audio_sample(int16_t l, int16_t r)
{
    (void)l; (void)r;
}

static size_t host_audio_sample_batch(const int16_t* data, size_t frames)
{
    (void)data;
    return frames;   // звук отключён
}

// Кадр RGB565 (uint16, pitch в байтах) -> EMU_FB 320x240 (левый верх),
// затем emu_scale() растянет на 1024x600.
static void host_video(const void* data, unsigned width, unsigned height, size_t pitch)
{
    uint16_t* dst = (uint16_t*)EMU_FB_ADDR;
    const uint16_t* src = (const uint16_t*)data;
    unsigned w = width  < PCE_SCALE_W ? width  : PCE_SCALE_W;
    unsigned h = height < PCE_SCALE_H ? height : PCE_SCALE_H;
    size_t sp = pitch >> 1;   // байты -> uint16

    emu_clear_fb();           // чёрные поля справа/снизу

    for (unsigned y = 0; y < h; y++)
        memcpy(dst + (size_t)y * EMU_FB_W, src + (size_t)y * sp, w * sizeof(uint16_t));
}

static bool host_environment(unsigned cmd, void* data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
        {
            static struct retro_game_info_ext ext;
            memset(&ext, 0, sizeof(ext));
            ext.full_path = "pce";
            ext.data = g_rom_data;
            ext.size = g_rom_size;
            ext.ext  = (char*)"pce";
            *(const struct retro_game_info_ext**)data = &ext;
            return g_rom_data ? true : false;
        }
        case RETRO_ENVIRONMENT_GET_CAN_DUPE: { bool* b = (bool*)data; *b = true; return true; }
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return (*(unsigned*)data) == RETRO_PIXEL_FORMAT_RGB565;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
            return false;   // все опции — дефолты
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: { bool* b = (bool*)data; *b = false; return true; }
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:          return true;
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:    return true;
        case RETRO_ENVIRONMENT_SET_GEOMETRY:             return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:         return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE:              return true;
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:    return true;
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:      return true;
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:      return true;
        case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:return true;
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:       return true;
        case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER: return false;
        case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK: return false;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:     return false;
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:       return false;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:        return false;
        default: return false;
    }
}

// ---- точка входа из rom_browser (emu.h) ----
void emu_run_pce(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    emu_prepare();
    snd_manifest("pce", "psg");

    if (!rom || size == 0 || size > 4096u * 1024u) {
        printf("PCE: no rom\n");
        return;
    }

    g_rom_data = rom;
    g_rom_size = size;

    retro_set_environment(host_environment);
    retro_set_video_refresh(host_video);
    retro_set_audio_sample(host_audio_sample);
    retro_set_audio_sample_batch(host_audio_sample_batch);
    retro_set_input_poll(host_input_poll);
    retro_set_input_state(host_input_state);

    retro_init();

    // Враппер берёт данные через GET_GAME_INFO_EXT; path фиктивный.
    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = "pce";
    info.data = (void*)rom;
    info.size = size;
    info.meta = rom_name ? rom_name : "pce";

    if (!retro_load_game(&info)) {
        printf("PCE: load failed\n");
        retro_deinit();
        return;
    }
    printf("PCE: started (%u bytes)\n", (unsigned)size);

    emu_set_border_color(0x000A0F18);   // тёмно-синий (PCE)
    emu_throttle_reset();
    emu_esc_hold_reset();

    for (;;) {
        retro_run();
        emu_throttle();
        emu_scale(PCE_SCALE_W, PCE_SCALE_H);
        fb_flush();
        if (emu_esc_hold()) break;
    }

    retro_unload_game();
    retro_deinit();
    fb_clear(); fb_flush();
}
