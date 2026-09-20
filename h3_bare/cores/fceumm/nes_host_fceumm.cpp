// nes_host_fceumm.cpp — host-слой FCEUmm (FCE Ultra) для bare-metal H3.
// Замена InfoNES: точный CPU/PPU, 250+ мапперов, поддержка клавиатуры
// SuborKB (Сюбор-картриджи с клавиатурой, SIFC_SUBORKB).
//
// Жизненный цикл:
//   emu_run_nes(rom, size, name)
//     -> fceumm_init_game: gb_heap_reset + FCEUI_Initialize + FCEUI_LoadGame
//     -> цикл: build_input() -> FCEUI_Emulate() -> рендер в EMU_FB
//     -> fceumm_stop: FCEUI_CloseGame + FCEUI_Kill

#include <stdint.h>
#include <string.h>

extern "C" {
#include "fceu.h"
#include "driver.h"
#include "video.h"
#include "palette.h"
#include "cart.h"
#include "ppu.h"
#include "x6502.h"
#include "sound.h"
#include "fceu-memory.h"
#include "general.h"
#include "git.h"
#include "usb_kbd.h"
#include "fb_text.h"
#include "emu.h"
#include "cheatdb.h"
#include "cheat.h"
#include "sega_pad.h"
#include "remap.h"
#include "i2s.h"
}

extern "C" int printf(const char* fmt, ...);
extern "C" void gb_heap_reset(void);
#include "h3_hs_timer.h"

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

// ---- состояние host-слоя ----
static int g_loaded = 0;

// буферы ввода (их адреса регистрируются в ядре через FCEUI_SetInput*)
static uint32_t g_joydata = 0;              // 1-я кнопка в младшем байте
static uint8_t  g_suborkb[0x65];            // клавиатура SuborKB
static int      has_suborkb = 0;            // 1 = активна SuborKB/FKB-клавиатура

// палитра 512 записей (FCEUD_SetPalette заполняет; индексы как в libretro.c)
static uint16_t nes_pal_rgb565[512];

// ---- глобалы, которые в оригинале определял libretro.c (драйвер) ----
extern "C" {
unsigned dendy = 0;
unsigned overclocked = 0;
unsigned overclock_enabled = (unsigned)-1;
unsigned skip_7bit_overclocking = 1;
unsigned normal_scanlines = 240;
unsigned totalscanlines = 0;
unsigned vblankscanlines = 0;
unsigned extrascanlines = 0;
unsigned swapDuty = 0;

// Debug-лента: при трансляции линкер просит printf — он уже есть в printf.c

// Заглушки вместо nsf.c (тянет math-библиотеку, не нужна в bare-metal)
int NSFLoad(void* fp) { return 0; }
void DrawNSF(uint8_t* XBuf) { (void)XBuf; }
void DoNSFFrame(void) {}

// Заглушка для boards/transformer.c (ключи не транслируем)
const char* GetKeyboard(void) { return ""; }
}

// ---- FCEUD-колбэки (требует driver.h) ----
extern "C" void FCEUD_SetPalette(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 512)
        nes_pal_rgb565[index] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

extern "C" void FCEUD_PrintError(const char* s) { printf("FCEU: %s\n", s); }
extern "C" void FCEUD_Message(const char* s) { printf("%s", s); }
extern "C" void FCEUD_DispMessage(enum retro_log_level l, unsigned d, const char* s) {
    (void)l; (void)d; (void)s;
}

// ---- ввод: USB-клавиатура -> геймпад + SuborKB ----
// HID scancode → SuborKeyboardData index
static const uint8_t hid_to_subor[128] = {
    0xff, 0xff, 0xff, 0xff, 0x39, 0x4c, 0x4a, 0x3b, 0x26, 0x3c, 0x3d, 0x3e, 0x2b, 0x3f, 0x40, 0x41,
    0x4e, 0x4d, 0x2c, 0x2d, 0x24, 0x27, 0x3a, 0x28, 0x2a, 0x4b, 0x25, 0x49, 0x29, 0x48, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x30, 0x00, 0x1b, 0x23, 0x59, 0x19, 0x1a, 0x2e,
    0x2f, 0x52, 0xff, 0x42, 0x43, 0x0e, 0x4f, 0x50, 0x51, 0x38, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0xff, 0xff, 0x0d, 0x1c, 0x1d, 0x1e, 0x31, 0x32, 0x33, 0x5d,
    0x5b, 0x5c, 0x54, 0x1f, 0x20, 0x21, 0x22, 0x37, 0xff, 0x54, 0x55, 0x56, 0x44, 0x45, 0x46, 0x34,
    0x35, 0x36, 0x5e, 0x5d, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static void build_input(void) {
    // Up=0x10 Down=0x20 Left=0x40 Right=0x80 A=0x01 B=0x02 Sel=0x04 Start=0x08
    g_joydata = 0;
    memset(g_suborkb, 0, sizeof(g_suborkb));

    // Sega-геймпад (PCF8574) -> геймпад NES ВСЕГДА (и при SuborKB:
    // клавиатура уходит в SuborKB, джойстик остаётся геймпадом — разделение ввода)
    uint16_t sp = sega_pad_scan();
    if (sp & 0x0001) g_joydata |= 0x10;
    if (sp & 0x0002) g_joydata |= 0x20;
    if (sp & 0x0004) g_joydata |= 0x40;
    if (sp & 0x0008) g_joydata |= 0x80;
    if (sp & 0x0010) g_joydata |= 0x01;   // Sega A -> NES A
    if (sp & 0x0020) g_joydata |= 0x02;   // Sega B -> NES B
    if (sp & 0x0080) g_joydata |= 0x08;   // Start
    if (sp & 0x0800) g_joydata |= 0x04;   // Mode -> Select

    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        if (!has_suborkb) {
            // Клавиатура -> геймпад NES (ремап через Settings → Keyboard remap)
            if (remap_kbd_pressed(REMAP_PLAT_NES, BTN_UP, keys, n))    g_joydata |= 0x10;
            if (remap_kbd_pressed(REMAP_PLAT_NES, BTN_DOWN, keys, n))  g_joydata |= 0x20;
            if (remap_kbd_pressed(REMAP_PLAT_NES, BTN_LEFT, keys, n))  g_joydata |= 0x40;
            if (remap_kbd_pressed(REMAP_PLAT_NES, BTN_RIGHT, keys, n)) g_joydata |= 0x80;
            if (remap_kbd_pressed(REMAP_PLAT_NES, BTN_A, keys, n))     g_joydata |= 0x01;
            if (remap_kbd_pressed(REMAP_PLAT_NES, BTN_B, keys, n))     g_joydata |= 0x02;
            if (remap_kbd_pressed(REMAP_PLAT_NES, BTN_SELECT, keys, n)) g_joydata |= 0x04;
            if (remap_kbd_pressed(REMAP_PLAT_NES, BTN_START, keys, n)) g_joydata |= 0x08;
            break;   // remap уже перебрал все клавиши — хватит одного прохода
        }
        // SuborKB/FKB — маппинг для клавиатурных картриджей (всегда,
        // чтобы Z/X/S/Enter/стрелки работали и в меню Subor-картриджа)
        if (sc < 128) {
            uint8_t sub = hid_to_subor[sc];
            if (sub < 0x65) g_suborkb[sub] = 1;
        }
    }
    uint8_t mods = usb_kbd_get_mods();
    if (mods & 0x01) g_suborkb[87] = 1;   // LCtrl
    if (mods & 0x02) g_suborkb[71] = 1;   // LShift
    if (mods & 0x20) g_suborkb[95] = 1;   // RShift
    if (mods & 0x04) g_suborkb[88] = 1;   // LAlt
    if (mods & 0x40) g_suborkb[96] = 1;   // RAlt
}
extern "C" int fceumm_init_game(const uint8_t* rom, uint32_t size) {
    printf("FCEUmm: init size=%u\n", (unsigned)size);
    g_loaded = 0;

    // Сброс общего bump-пула (malloc из gameboy_stubs.c): перед каждым
    // запуском NES — иначе повторные init копят XBuf/ROM и упрутся в 3 МБ.
    gb_heap_reset();

    if (!FCEUI_Initialize()) {
        printf("FCEUmm: FCEUI_Initialize failed\n");
        return 0;
    }

    FCEUGI* gi = FCEUI_LoadGame("rom", rom, size, NULL);
    if (!gi) {
        printf("FCEUmm: load failed (bad/missing mapper?)\n");
        FCEUI_Kill();
        return 0;
    }

    // Геймпады 1 и 2
    FCEUI_SetInput(0, SI_GAMEPAD, &g_joydata, 0);
    FCEUI_SetInput(1, SI_GAMEPAD, &g_joydata, 0);

    // FC-порт: по тому, что определил FCEUmm (inputfc из MetaDB по CRC).
    //    -1 = неопределён (обычная игра) → SIFC_NONE
    //     5 = SIFC_SUBORKB (Сюбор, LIKO и т.п.) → SuborKB
    //     4 = SIFC_FKB (Family BASIC) → FKB
    //    Другие девайсы (Arkanoid, Shadow, FTrainer) — не поддерживаем пока.
    if (gi->inputfc == SIFC_SUBORKB) {
        FCEUI_SetInputFC(SIFC_SUBORKB, g_suborkb, 0);
        has_suborkb = 1;
        printf("FCEUmm: SuborKB enabled\n");
    } else if (gi->inputfc == SIFC_FKB) {
        FCEUI_SetInputFC(SIFC_FKB, g_suborkb, 0);
        printf("FCEUmm: FKB enabled\n");
    } else {
        FCEUI_SetInputFC(SIFC_NONE, NULL, 0);
        has_suborkb = 0;
        printf("FCEUmm: no FC keyboard\n");
    }

    // Звук: включаем NES APU на 48000 Гц (I2S → MAX98357A)
    FCEUI_Sound(48000);
    FCEUI_SetSoundVolume(100);

    // Применяем отмеченные в меню читы (их список заполнил rom_browser/cheat_menu_run
    // через cheats_load: NES-коды — 6/8-символьный Game Genie + PAR)
    FCEU_ResetCheats();
    int ccnt = cheats_count();
    for (int i = 0; i < ccnt; i++) {
        if (!cheats_enabled(i)) continue;
        const char* code = cheats_code(i);
        if (!code || !code[0]) continue;
        uint16_t a; uint8_t v; int c; int t;
        if (FCEUI_DecodeGG(code, &a, &v, &c)) {
            FCEUI_AddCheat(cheats_desc(i) ? cheats_desc(i) : "cheat", a, v, c, 1);
        } else if (FCEUI_DecodePAR(code, &a, &v, &c, &t)) {
            FCEUI_AddCheat(cheats_desc(i) ? cheats_desc(i) : "cheat", a, v, c, t);
        } else {
            printf("NEScheat: bad code '%s'\n", code);
        }
    }

    g_loaded = 1;
    printf("FCEUmm: type=%d inputfc=%d\n", gi->type, gi->inputfc);
    return 1;
}

extern "C" void fceumm_run_frame(void) {
    if (!g_loaded) return;

    build_input();

    uint8_t* gfx = NULL;
    int32_t* snd = NULL;
    int32_t ssize = 0;
    FCEUI_Emulate(&gfx, &snd, &ssize, 0);
    if (!gfx) return;

    // ==== ДИАГНОСТИКА ЗВУКА (1 раз/сек): max|sample| за кадр ====
    {
        static int diag_cnt = 0;
        static long diag_max = 0;
        int mx = 0;
        for (int z = 0; z < ssize && z < 400; z++) {
            int a = snd[z] < 0 ? -snd[z] : snd[z];
            if (a > mx) mx = a;
        }
        if (mx > diag_max) diag_max = mx;
        if (++diag_cnt >= 60) {
            printf("FCEUMM-SND: frames=%d ssize=%d max=%ld\n",
                   diag_cnt, ssize, diag_max);
            diag_cnt = 0; diag_max = 0;
        }
    }

    // Звук: WaveFinal — int32_t моно (стандартный APU NES). Конвертируем
    // в int16_t и разворачиваем L/R одинаково, шлём в I2S (MAX98357A).
    for (int32_t i = 0; i < ssize; i++) {
        int32_t v = snd[i];
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        int16_t s = (int16_t)v;
        i2s_push_sample(s, s);
    }

    // Рендер: gfx = XBuf[256×240] индексов палитры.
    // Деэмфазис строки из XDBuf: база 256 + (deemp&7)<<6, иначе база 0.
    extern uint8_t *XDBuf;
    for (int y = 0; y < 240; y++) {
        uint8_t deemp = XDBuf ? XDBuf[y * 256] : 0;
        uint32_t base = deemp ? (256u + ((unsigned)(deemp & 0x07) << 6)) : 0;
        const uint8_t* src = gfx + y * 256;
        uint16_t* dst = EMU_FB + y * EMU_W;
        for (int x = 0; x < 256; x++)
            dst[x] = nes_pal_rgb565[base + (src[x] & 0xFF)];
    }
}

// ---- пауза/стоп (освобождение) ----
extern "C" void fceumm_stop(void) {
    if (!g_loaded) return;
    FCEU_ResetCheats();
    FCEUI_CloseGame();
    FCEUI_Kill();
    g_loaded = 0;
    printf("FCEUmm: stopped\n");
}

// ---- точка входа emu.c (совместима со старым emu_run_nes) ----
extern "C" void emu_run_nes(const uint8_t* rom, uint32_t size, const char* rom_name) {
    (void)rom_name;
    fb_clear(); fb_flush();
    if (fceumm_init_game(rom, size) != 1) {
        printf("NES(FCEUmm): init failed\n");
        return;
    }
    emu_set_border_color(0x00140612);   // тёмно-бордовый (Dendy/NES)
    printf("NES: AUDIO BEEP TEST 1kHz/300ms at start\n");
    i2s_test_tone(1000, 300);
    uint8_t raw_keys[6];
    uint32_t esc_hold_us = 0;
    emu_throttle_reset();
    for (;;) {
        fceumm_run_frame();
        emu_throttle();
        emu_scale(256, 240);
        fb_flush();
        // ESC: одиночное нажатие НЕ выходит — оно уходит в SuborKB
        // (hid_to_subor[41]=FKB_ESCAPE: Break в Basic и т.п.) или игнорируется.
        // Выход — только по УДЕРЖАНИЮ ~0.9 с (как в Portfolio).
        int nk = usb_kbd_get_raw(raw_keys, 6);
        int esc = 0;
        for (int i = 0; i < nk; i++)
            if (raw_keys[i] == 41) { esc = 1; break; }
        if (esc) {
            if (!esc_hold_us) esc_hold_us = h3_hs_timer_lo_us();
            else if (h3_hs_timer_lo_us() - esc_hold_us > 900000) goto exit;
        } else {
            esc_hold_us = 0;
        }
    }
exit:
    fceumm_stop();
    fb_clear(); fb_flush();
}