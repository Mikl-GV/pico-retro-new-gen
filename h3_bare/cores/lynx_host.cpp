// lynx_host.cpp — host-слой Atari Lynx (Handy) для H3 bare-metal.
#include <stdint.h>
#include <string.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
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

// Ввод: USB-клавиатура -> Lynx кнопки
static ULONG lynx_buttons_from_kbd(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    ULONG b = 0;
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        if (sc == 82) b |= 0x80;   // Up
        if (sc == 81) b |= 0x40;   // Down
        if (sc == 80) b |= 0x20;   // Left
        if (sc == 79) b |= 0x10;   // Right
        if (sc == 29) b |= 0x01;   // Z = A
        if (sc == 27) b |= 0x02;   // X = B
        if (sc == 22) b |= 0x08;   // S = Option 1
        if (sc == 40) b |= 0x04;   // Enter = Option 2
    }
    return b;
}

extern "C" int lynx_init_game(const uint8_t* rom, uint32_t size) {
    // Создаём CSystem, передаём ROM в gamedata (без файла)
    // useEmu=true — BIOS эмулируется (не нужен lynxboot.img)
    g_lynx = new CSystem(NULL, rom, size, NULL, true, NULL);
    if (!g_lynx) { printf("[lynx] new CSystem failed\n"); return 0; }

g_lynx->DisplaySetAttributes(
        MIKIE_NO_ROTATE,          // без поворота — кадр пишется построчно 160x102
        MIKIE_PIXEL_FORMAT_16BPP_565, // RGB565
        LYNX_W,
        display_callback,
        0
    );

    printf("[lynx] init ok, size=%u\n", (unsigned)size);
    return 1;
}

extern "C" void lynx_run_frame(void) {
    if (!g_lynx) return;

    g_lynx->SetButtonData(lynx_buttons_from_kbd());

    // Гоняем Update() пока display_callback не поставит флаг готового кадра.
    // Мигаем PA15 прямо здесь — видно, что функция выполняется.
    static uint32_t led_fc = 0;
    if ((++led_fc & 0xFFFF) == 0) led_set((led_fc >> 16) & 1);

    // Диагностика: раз в 120 кадров печатаем счётчик и сколько пикселей
    // в буфере ненулевые (0 = Handy ничего не рисует).
    static uint32_t frame_cnt = 0;
    frame_cnt++;
    if ((frame_cnt % 120) == 0) {
        uint32_t nonzero = 0;
        for (int i = 0; i < LYNX_W * LYNX_H; i++)
            if (lynx_fb[i] != 0) nonzero++;
        printf("lynx: f=%u ready=%d nonzero=%u\n", (unsigned)frame_cnt,
               (int)lynx_frame_ready, (unsigned)nonzero);
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

extern "C" int lynx_exit_requested(void) { return 0; }