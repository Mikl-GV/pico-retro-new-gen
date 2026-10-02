// lynx_host.cpp — host-слой Atari Lynx (Handy) для H3 bare-metal.
#include <stdint.h>
#include <string.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "cheatdb.h"
#include "i2s.h"
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
static uint32_t g_lynx_pairs = 800;        // r511: сэмплов (пар) за последний кадр

extern "C" uint32_t lynx_last_pairs(void) { return g_lynx_pairs; }
static ULONG g_lynx_next_cycle = 0;   // r516: стaтик, сбрасывается в lynx_init_game

// Callback — вызывается Mikie в конце каждого кадра
static UBYTE* display_callback(ULONG objref) {
    (void)objref;
    lynx_frame_ready = 1;
    return (UBYTE*)lynx_fb;
}

// Ввод: USB-клавиатура + Sega-геймпад -> Lynx кнопки (susie.h:
// BUTTON_UP=0x40, BUTTON_DOWN=0x80, BUTTON_LEFT=0x10, BUTTON_RIGHT=0x20)
// Sega-геймпад: A->A B->B X->Option1 Y->Option2, Start = Pause (корпусная).
// r155: пауза на Start, а НЕ на Mode (было Mode->Pause, Start не использовался).
static ULONG lynx_buttons_from_kbd(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    ULONG b = 0;

    uint16_t sp = pad_scan_combined();
    if (sp & 0x0001) b |= 0x40;   // Up    = BUTTON_UP
    if (sp & 0x0002) b |= 0x80;   // Down  = BUTTON_DOWN
    if (sp & 0x0004) b |= 0x10;   // Left  = BUTTON_LEFT
    if (sp & 0x0008) b |= 0x20;   // Right = BUTTON_RIGHT
    if (sp & 0x0010) b |= 0x01;   // Sega A = A
    if (sp & 0x0020) b |= 0x02;   // Sega B = B
    if (sp & 0x0100) b |= 0x08;   // Sega X = Option 1
    if (sp & 0x0200) b |= 0x04;   // Sega Y = Option 2
    if (sp & 0x0080) b |= 0x0100; // Sega Start = Pause (корпусная кнопка Lynx)

    // Клавиатура -> Lynx (ремап через Settings → Keyboard remap).
    if (remap_kbd_pressed(REMAP_PLAT_LYNX, BTN_UP, keys, n))    b |= 0x40;   // BUTTON_UP
    if (remap_kbd_pressed(REMAP_PLAT_LYNX, BTN_DOWN, keys, n))  b |= 0x80;   // BUTTON_DOWN
    if (remap_kbd_pressed(REMAP_PLAT_LYNX, BTN_LEFT, keys, n))  b |= 0x10;   // BUTTON_LEFT
    if (remap_kbd_pressed(REMAP_PLAT_LYNX, BTN_RIGHT, keys, n)) b |= 0x20;   // BUTTON_RIGHT
    if (remap_kbd_pressed(REMAP_PLAT_LYNX, BTN_A, keys, n))     b |= 0x01;   // BUTTON_A
    if (remap_kbd_pressed(REMAP_PLAT_LYNX, BTN_B, keys, n))     b |= 0x02;   // BUTTON_B
    if (remap_kbd_pressed(REMAP_PLAT_LYNX, BTN_OPT1, keys, n))  b |= 0x08;   // BUTTON_OPT1
    if (remap_kbd_pressed(REMAP_PLAT_LYNX, BTN_OPT2, keys, n))  b |= 0x04;   // BUTTON_OPT2
    if (remap_kbd_pressed(REMAP_PLAT_LYNX, BTN_PAUSE, keys, n)) b |= 0x0100; // BUTTON_PAUSE
    return b;
}

extern "C" int lynx_init_game(const uint8_t* rom, uint32_t size) {
    if (!rom || size == 0) { printf("[lynx] no ROM data\n"); return 0; }
    if (size < 64) { printf("[lynx] ROM too small (%u)\n", (unsigned)size); return 0; }

    i2s_dc_shift_set(6);   // r590: Lynx — ~120 Гц (непрерывный поток, по стенду)

    if (memcmp(rom, "LYNX", 4) != 0) {
        printf("[lynx] no LYNX header (headerless ROM), size=%u\n", (unsigned)size);
    } else {
        printf("[lynx] LYNX header ver=%u\n", (unsigned)rom[14]);
    }

    extern void gb_heap_reset(void);
    gb_heap_reset();
    memset(lynx_fb, 0, sizeof(lynx_fb));   // свой кадр-буфер не чистится emu_prepare

    g_lynx = new CSystem(NULL, rom, size, NULL, false, NULL);
    if (!g_lynx) { printf("[lynx] new CSystem failed\n"); return 0; }

    // r516: счётчик кадрового цикла — НЕЛЬЗЯ нести между играми (иначе target
    // уходит вперёд от прошлой сессии и следующий запуск упирается в safety).
    g_lynx_next_cycle = 0;

    // r507: звук. gAudioEnabled в Handy по умолчанию FALSE — без TRUE
    // Mikie::Update НЕ обновляет аудио-подсистему (mikie.cpp:3224) и
    // blip остаётся пустым. В оригинале libretro выставляется в retro_load_game.
    gAudioEnabled = TRUE;

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

    // Гоняем Update() до готовности кадра.
    // r125: мигание alive (PL10) делает CPU1 (led_heartbeat_cpu1).

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

    // r514-r516: кадр = 213333 виртуальных цикла (16 МГц / 75 Гц, как libretro).
    // Выход по display_callback допускается ТОЛЬКО когда осталось <12.5% кадра,
    // иначе кадр обрывается слишком рано (у игр с быстрой развёрткой пары
    // падали до 35-133 → рывки). Batman не набирает target без кадра — выходит
    // по callback в конце кадра.
    extern ULONG gSystemCycleCount;
    if (!g_lynx_next_cycle) g_lynx_next_cycle = gSystemCycleCount;
    ULONG target = g_lynx_next_cycle + (HANDY_SYSTEM_FREQ / 75);
    const int32_t EARLY_OK = (int32_t)((HANDY_SYSTEM_FREQ / 75) / 8);
    lynx_frame_ready = 0;
    int safety = 0;
    while ((int32_t)(target - gSystemCycleCount) > 0) {
        g_lynx->Update();
        if (++safety > 4000000) {
            printf("lynx: frame timeout (safety)\n");
            break;
        }
        if (lynx_frame_ready && (int32_t)(target - gSystemCycleCount) <= EARLY_OK)
            break;
    }
    g_lynx_next_cycle = target;

    // Звук (r507): СНАЧАЛА собираем сэмплы кадра — без FetchAudioSamples()
    // AudioEndOfFrame() не вызывается и gAudioBufferPointer остаётся нулём
    // (это и был обрыв в r506: буфер читался до сборки).
    // Формат: gAudioBuffer = стерео int16 (L,R чередуются; blip выдаёт
    // mix_stereo/mix_mono с парами), gAudioBufferPointer = число int16 => пары = /2.
    g_lynx->FetchAudioSamples();
    {
        extern ULONG gAudioBufferPointer;
        ULONG np = gAudioBufferPointer / 2;
        if (np > (HANDY_AUDIO_BUFFER_SIZE / 2) / 2) np = (HANDY_AUDIO_BUFFER_SIZE / 2) / 2;
        g_lynx_pairs = np;   // r511: для синхронизации периода кадра
        const int16_t* p = (const int16_t*)gAudioBuffer;
        for (ULONG i = 0; i < np; i++)
            i2s_push_sample(p[2 * i], p[2 * i + 1]);
    }
}

extern "C" void lynx_render_frame(void) {
    if (!g_lynx) return;
    for (int y = 0; y < LYNX_H && y < EMU_H; y++)
        for (int x = 0; x < LYNX_W && x < EMU_W; x++)
            EMU_FB[y * EMU_W + x] = lynx_fb[y * LYNX_W + x];
}