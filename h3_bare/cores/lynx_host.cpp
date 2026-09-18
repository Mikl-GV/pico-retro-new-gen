// lynx_host.cpp — host-слой Atari Lynx (Handy) для H3 bare-metal.
#include <stdint.h>
#include <string.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "cheatdb.h"
}

#define EMU_FB ((uint16_t*)0x5F800000)
#define LYNX_W 160
#define LYNX_H 102
#define EMU_W  320
#define EMU_H  240

extern "C" int printf(const char* fmt, ...);

extern "C" void led_set(int on);

#include "lynx/system.h"
#include "lynx/lynxdef.h"

static CSystem* g_lynx = NULL;
static uint16_t lynx_fb[LYNX_W * LYNX_H]; // наш буфер 160x102 RGB565
static volatile int lynx_frame_ready = 0;  // 1 = кадр отрисован (ставит display_callback)

// Callback — вызывается Mikie в конце каждого кадра
static UBYTE* display_callback(ULONG objref) {
    (void)objref;
    lynx_frame_ready = 1;
    return (UBYTE*)lynx_fb;
}

// Ввод: USB-клавиатура + Sega-геймпад -> Lynx кнопки (susie.h:
// BUTTON_UP=0x40, BUTTON_DOWN=0x80, BUTTON_LEFT=0x10, BUTTON_RIGHT=0x20)
// Sega-геймпад: A->A B->B X->Option1 Y->Option2, Start/Mode не мапятся (нет)
static ULONG lynx_buttons_from_kbd(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    ULONG b = 0;

    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) b |= 0x40;   // Up    = BUTTON_UP
    if (sp & 0x0002) b |= 0x80;   // Down  = BUTTON_DOWN
    if (sp & 0x0004) b |= 0x10;   // Left  = BUTTON_LEFT
    if (sp & 0x0008) b |= 0x20;   // Right = BUTTON_RIGHT
    if (sp & 0x0010) b |= 0x01;   // Sega A = A
    if (sp & 0x0020) b |= 0x02;   // Sega B = B
    if (sp & 0x0100) b |= 0x08;   // Sega X = Option 1
    if (sp & 0x0200) b |= 0x04;   // Sega Y = Option 2

    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        if (sc == 82) b |= 0x40;   // Up    = BUTTON_UP
        if (sc == 81) b |= 0x80;   // Down  = BUTTON_DOWN
        if (sc == 80) b |= 0x10;   // Left  = BUTTON_LEFT
        if (sc == 79) b |= 0x20;   // Right = BUTTON_RIGHT
        if (sc == 29) b |= 0x01;   // Z = A
        if (sc == 27) b |= 0x02;   // X = B
        if (sc == 22) b |= 0x08;   // S = Option 1
        if (sc == 40) b |= 0x04;   // Enter = Option 2
    }
    return b;
}

extern "C" int lynx_init_game(const uint8_t* rom, uint32_t size) {
    if (!rom || size == 0) { printf("[lynx] no ROM data\n"); return 0; }
    if (size < 64) { printf("[lynx] ROM too small (%u)\n", (unsigned)size); return 0; }

    if (memcmp(rom, "LYNX", 4) != 0) {
        printf("[lynx] no LYNX header (headerless ROM), size=%u\n", (unsigned)size);
    } else {
        printf("[lynx] LYNX header ver=%u\n", (unsigned)rom[14]);
    }

    extern void gb_heap_reset(void);
    gb_heap_reset();

    g_lynx = new CSystem(NULL, rom, size, NULL, false, NULL);
    if (!g_lynx) { printf("[lynx] new CSystem failed\n"); return 0; }

    if (!g_lynx->mMikie) {
        printf("[lynx] mMikie is NULL, cartridge init failed\n");
        delete g_lynx; g_lynx = NULL;
        return 0;
    }

    g_lynx->DisplaySetAttributes(
        MIKIE_NO_ROTATE,
        MIKIE_PIXEL_FORMAT_16BPP_565,
        LYNX_W * 2,
        display_callback,
        0
    );

    printf("[lynx] cart: '%s' by '%s' mask=%u/%u EEPROM=%d rot=%d\n",
           g_lynx->mCart->CartGetName(),
           g_lynx->mCart->CartGetManufacturer(),
           (unsigned)g_lynx->mCart->mMaskBank0,
           (unsigned)g_lynx->mCart->mMaskBank1,
           (int)g_lynx->mCart->mEEPROMType,
           (int)g_lynx->mCart->CartGetRotate());
    printf("[lynx] init ok, size=%u\n", (unsigned)size);
    return 1;
}

extern "C" void lynx_run_frame(void) {
    if (!g_lynx) return;

    g_lynx->SetButtonData(lynx_buttons_from_kbd());

    // RAW-читы: пишем байт каждый кадр в RAM Lynx (64K)
    int rc = cheats_raw_count();
    for (int i = 0; i < rc; i++) {
        uint32_t a; uint8_t v, c; int hc;
        if (cheats_raw_get(i, &a, &v, &c, &hc)) {
            a &= 0xFFFF;
            if (!hc || g_lynx->Peek_RAM(a) == c) g_lynx->Poke_RAM(a, v);
        }
    }

    // Гоняем Update() пока display_callback не поставит флаг готового кадра.
    // Мигаем PA15 прямо здесь — видно, что функция выполняется.
    static uint32_t led_fc = 0;
    if ((++led_fc & 0xFFFF) == 0) led_set((led_fc >> 16) & 1);

    // Диагностика: проверяем, рисует ли что-то Handy
    static uint32_t frame_cnt = 0;
    frame_cnt++;

    // Страховка от "чёрного экрана": если игра не выставила DISPCTL.DMAEnable
    // (Mikie::DisplayRenderLine при этом сразу выходит, буфер пуст) —
    // принудительно включаем бит DMA через регистр DISPCTL (0xfd92).
    // Срабатывает один раз после ~2 секунд пустого буфера; рабочие кадры не трогает.
    {
        static uint32_t blank_frames = 0;
        int any = 0;
        for (int i = 0; i < LYNX_W * LYNX_H; i++)
            if (lynx_fb[i]) { any = 1; break; }
        if (!any) {
            blank_frames++;
            if (blank_frames == 120) {
                g_lynx->mMikie->Poke(0xfd92, 0x01);   // DISPCTL.DMAEnable = 1
                printf("lynx: forcing DISPCTL.DMAEnable\n");
            }
        } else {
            blank_frames = 0;
        }
    }

    lynx_frame_ready = 0;
    int safety = 0;
    while (!lynx_frame_ready) {
        g_lynx->Update();
        if (++safety > 4000000) {
            printf("lynx: frame timeout (safety)\n");
            lynx_frame_ready = 1; break; // ~4M инструкций на кадр макс
        }
    }
}

extern "C" void lynx_render_frame(void) {
    if (!g_lynx) return;
    for (int y = 0; y < LYNX_H && y < EMU_H; y++)
        for (int x = 0; x < LYNX_W && x < EMU_W; x++)
            EMU_FB[y * EMU_W + x] = lynx_fb[y * LYNX_W + x];
}