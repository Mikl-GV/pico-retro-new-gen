// gba_mgba_host.c — host-слой Game Boy Advance (ядро mGBA) для H3 bare-metal.
//
// r1132: ПОЛНАЯ замена gpsp (gba_sp) на mGBA-core (h3_bare/cores/gba_mgba).
// Жизненный цикл (тот же контракт, что ожидает emu.c):
//   gba_init_game(rom, size)  — mCoreCreate(GBA) → loadROM → setVideoBuffer
//   gba_run_frame()           — ввод + runFrame (mGBA сам до VBlank)
//   gba_render_frame()        — mGBA software-рендер уже рисует прямо в EMU_FB
//
// Различия с gpSP-хостом:
//   - видео: mGBA software-рендер пишет RGB565 buf (setVideoBuffer) напрямую,
//     gba_render_frame() больше не копирует построчно — буфер кадра и есть
//     EMU_FB (stride 320).
//   - звук: mGBA синтезирует 32768 Гц стерео s16-пары в mAudioBuffer;
//     host читает mAudioBufferRead и ресемплит 32768→48000 (полифазный,
//     тот же GBA_RS_INC), пушит в I2S пакетно (i2s_push_block, r1135).
//   - BIOS: вшит в прошивку (open_gba_bios_rom, bios_data.S), mGBA-ядру
//     подаётся через core->loadBIOS — без файлов на SD (как r538/r565).
//
// Ресемплер и таблица полифазных коэффициентов перенесены из старого host
// (gpsp) — они проверены (r592) и не зависят от внутренностей ядра.

#include <stdint.h>
#include <string.h>

#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t  s16;
typedef int64_t  s64;
typedef uint64_t u64;

#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "i2s.h"
#include "h3_hs_timer.h"

// ---- ЕМУ-буфер кадра (240x160 RGB565, общий для эмуляторов) ----
#define EMU_FB ((uint16_t*)0x5F800000)
#define EMU_W  320
#define EMU_H  240

// GBA-размеры (mGBA interface.h): GBA_VIDEO_HORIZONTAL_PIXELS=240, VERTICAL=160.
#define GBA_W 240
#define GBA_H 160

// ---- ресемпл звука 32768 → 48000 Гц (полифазный, 64 фазы × 8 тапов) ----
// mGBA, как и gpSP, синтезирует GBA-звук на 32768 Гц стерео (s16-пары).
// I2S на 48 кГц; 32768→48000 = увеличение ×1.4648 (~804 пары/кадр).
#define GBA_RS_INC 44739u   // (32768<<16)/48000
#define GBA_RS_MAX_OUT 900  // норма ~804, cap выше — не режем нормаль
static u32 gba_rs_phase = 0;   // непрерывна между кадрами (поток не рвётся)

#define GBA_POLY_PHASES 64
#define GBA_POLY_TAPS 8
static const int16_t gba_poly_h[GBA_POLY_PHASES][GBA_POLY_TAPS] = {
  {     0,  -281,  2376, 14288, 14288,  2376,  -281,     0 },
  {     0,  -286,  2293, 14192, 14384,  2460,  -276,     0 },
  {     0,  -291,  2211, 14095, 14478,  2544,  -270,     0 },
  {     0,  -295,  2130, 13997, 14572,  2628,  -265,     0 },
  {     0,  -300,  2049, 13898, 14665,  2713,  -259,     0 },
  {     0,  -304,  1968, 13799, 14757,  2799,  -253,     0 },
  {     0,  -309,  1887, 13699, 14849,  2885,  -247,     0 },
  {     0,  -313,  1806, 13599, 14940,  2971,  -240,     0 },
  {     0,  -317,  1725, 13498, 15030,  3058,  -234,     0 },
  {     0,  -321,  1644, 13397, 15120,  3145,  -228,     0 },
  {     0,  -325,  1564, 13295, 15209,  3232,  -222,     0 },
  {     0,  -329,  1484, 13193, 15297,  3320,  -216,     0 },
  {     0,  -332,  1404, 13091, 15384,  3408,  -210,     0 },
  {     0,  -335,  1325, 12988, 15471,  3496,  -204,     0 },
  {     0,  -339,  1246, 12885, 15557,  3584,  -198,     0 },
  {     0,  -341,  1168, 12782, 15642,  3673,  -192,     0 },
  {     0,  -344,  1090, 12678, 15726,  3762,  -186,     0 },
  {     0,  -347,  1013, 12574, 15810,  3851,  -180,     0 },
  {     0,  -349,   936, 12469, 15893,  3940,  -174,     0 },
  {     0,  -351,   860, 12364, 15975,  4029,  -168,     0 },
  {     0,  -353,   784, 12259, 16056,  4119,  -162,     0 },
  {     0,  -355,   709, 12153, 16137,  4209,  -156,     0 },
  {     0,  -356,   635, 12047, 16217,  4299,  -150,     0 },
  {     0,  -358,   561, 11941, 16296,  4389,  -144,     0 },
  {     0,  -359,   488, 11835, 16374,  4479,  -139,     0 },
  {     0,  -360,   416, 11728, 16452,  4570,  -133,     0 },
  {     0,  -360,   344, 11621, 16529,  4661,  -127,     0 },
  {     0,  -361,   273, 11514, 16605,  4752,  -121,     0 },
  {     0,  -361,   203, 11406, 16681,  4843,  -116,     0 },
  {     0,  -361,   134, 11298, 16756,  4934,  -110,     0 },
  {     0,  -361,    65, 11190, 16830,  5026,  -105,     0 },
  {     0,  -361,    -3, 11082, 16903,  5118,   -99,     0 },
  {     0,  -360,   -71, 10973, 16976,  5210,   -94,     0 },
  {     0,  -360,  -138, 10864, 17048,  5302,   -89,     0 },
  {     0,  -359,  -204, 10755, 17119,  5394,   -84,     0 },
  {     0,  -358,  -269, 10646, 17190,  5487,   -79,     0 },
  {     0,  -357,  -334, 10537, 17260,  5580,   -74,     0 },
  {     0,  -356,  -398, 10427, 17329,  5672,   -69,     0 },
  {     0,  -355,  -461, 10318, 17398,  5765,   -64,     0 },
  {     0,  -353,  -523, 10208, 17466,  5858,   -60,     0 },
  {     0,  -352,  -584,  9999, 17533,  5951,   -55,     0 },
  {     0,  -350,  -645,  9889, 17600,  6044,   -50,     0 },
  {     0,  -348,  -704,  9780, 17666,  6137,   -46,     0 },
  {     0,  -346,  -762,  9670, 17732,  6230,   -41,     0 },
  {     0,  -344,  -819,  9561, 17797,  6323,   -37,     0 },
  {     0,  -342,  -875,  9451, 17862,  6417,   -33,     0 },
  {     0,  -339,  -930,  9341, 17926,  6510,   -29,     0 },
  {     0,  -337,  -984,  9232, 17989,  6604,   -25,     0 },
  {     0,  -334, -1037,  9122, 18052,  6697,   -21,     0 },
  {     0,  -332, -1089,  9013, 18115,  6791,   -18,     0 },
  {     0,  -329, -1139,  8904, 18177,  6884,   -14,     0 },
  {     0,  -326, -1189,  8795, 18238,  6978,   -11,     0 },
  {     0,  -323, -1237,  8687, 18300,  7071,    -8,     0 },
  {     0,  -320, -1284,  8578, 18360,  7165,    -5,     0 },
  {     0,  -317, -1330,  8470, 18421,  7259,    -2,     0 },
  {     0,  -314, -1375,  8362, 18481,  7352,     0,     0 },
  {     0,  -311, -1418,  8254, 18540,  7446,     2,     0 },
  {     0,  -308, -1460,  8147, 18600,  7539,     4,     0 },
  {     0,  -305, -1501,  8040, 18659,  7633,     6,     0 },
  {     0,  -302, -1541,  7933, 18717,  7726,     8,     0 },
  {     0,  -299, -1580,  7827, 18776,  7820,     9,     0 },
  {     0,  -296, -1618,  7721, 18834,  7913,    10,     0 },
  {     0,  -293, -1654,  7615, 18892,  8006,    11,     0 }
};

static u32 gba_resample(const int16_t* in, u32 nin_pairs, int16_t* out) {
    u32 ph = gba_rs_phase;
    u32 o = 0;
    while (o < GBA_RS_MAX_OUT) {
        u32 i = ph >> 16;
        if (i >= nin_pairs) break;
        u32 f = ph & 0xFFFFu;
        u32 p = (f * GBA_POLY_PHASES) >> 16;
        if (p >= GBA_POLY_PHASES) p = GBA_POLY_PHASES - 1;
        const int16_t* h = gba_poly_h[p];
        int64_t al = 0, ar = 0;
        for (int k = 0; k < GBA_POLY_TAPS; k++) {
            int idx = (int)i - (GBA_POLY_TAPS / 2 - 1) + k;
            if (idx < 0) idx = 0;
            if ((u32)idx >= nin_pairs) idx = (int)nin_pairs - 1;
            al += (int64_t)in[2 * idx]     * h[k];
            ar += (int64_t)in[2 * idx + 1] * h[k];
        }
        out[2 * o]     = (int16_t)(int32_t)(al >> 15);
        out[2 * o + 1] = (int16_t)(int32_t)(ar >> 15);
        o++;
        ph += GBA_RS_INC;
    }
    if (ph >= ((u64)nin_pairs << 16))
        gba_rs_phase = (u32)(ph - ((u64)nin_pairs << 16));
    else
        gba_rs_phase = ph;
    return o;
}

extern int printf(const char* fmt, ...);
extern u8 open_gba_bios_rom[1024 * 16];

// Строки ядра (mgba_core_serialize.c: mCoreSaveStateNamed пишет creator-метку).
const char* projectName = "pico-retro GBA (mGBA)";
const char* projectVersion = "dev";

static struct mCore* g_core = NULL;
static int g_loaded = 0;
static uint32_t g_keys = 0;
static u32 g_gba_pairs = 0;

// Тихий логгер для вендорного mGBA-ядра. Пока не зарегистрирован дефолтный
// логгер, mLog() (core/log.c:114) при `_defaultLogger == NULL` падает в ветку
// `printf("%s: ", ...)` и спамит UART ("GBA BIOS: SWI ...", "GBA DMA: Starting").
// Устанавливаем logger с функцией-пустышкой и `filter == NULL`: mLog вызывает
// `context->log(...)` без фильтрации → наша функция ничего не печатает,
// вендорный лог (в т.ч. потоковые DEBUG/INFO уровня GBA_BIOS/GBA_DMA) гаснет.
static void gba_mgba_silent_log(struct mLogger* logger, int category,
                                enum mLogLevel level, const char* format,
                                va_list args) {
    (void)logger; (void)category; (void)level; (void)format; (void)args;
}
static struct mLogger gba_mgba_logger = { gba_mgba_silent_log, NULL };

// ---- ввод: USB-клавиатура + Sega-геймпад -> битовая маска mGBA ----
// mGBA setKeys принимает бит N = GBA_KEY_N (input.h):
//   A=0 B=1 Select=2 Start=3 Right=4 Left=5 Up=6 Down=7 R=8 L=9
static void gba_poll_keys(void) {
    uint8_t keys[8];
    int n = usb_kbd_get_raw(keys, 8);
    uint32_t m = 0;

    uint16_t sp = pad_scan_combined();
    if (sp & 0x0001) m |= 1u << 6;   // Up
    if (sp & 0x0002) m |= 1u << 7;   // Down
    if (sp & 0x0004) m |= 1u << 5;   // Left
    if (sp & 0x0008) m |= 1u << 4;   // Right
    if (sp & 0x0010) m |= 1u << 0;   // Sega A -> A
    if (sp & 0x0020) m |= 1u << 1;   // Sega B -> B
    if (sp & 0x0040) m |= 1u << 9;   // Sega C -> L
    if (sp & 0x0100) m |= 1u << 8;   // Sega X -> R
    if (sp & 0x0080) m |= 1u << 3;   // Sega Start -> Start
    if (sp & 0x0800) m |= 1u << 2;   // Sega Mode -> Select

    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_UP, keys, n))    m |= 1u << 6;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_DOWN, keys, n))  m |= 1u << 7;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_LEFT, keys, n))  m |= 1u << 5;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_RIGHT, keys, n)) m |= 1u << 4;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_A, keys, n))     m |= 1u << 0;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_B, keys, n))     m |= 1u << 1;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_L, keys, n))     m |= 1u << 9;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_R, keys, n))     m |= 1u << 8;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_SELECT, keys, n)) m |= 1u << 2;
    if (remap_kbd_pressed(REMAP_PLAT_GBA, BTN_START, keys, n)) m |= 1u << 3;
    g_keys = m;
}

int gba_init_game(const uint8_t* rom, uint32_t size) {
    if (!rom || size == 0) { printf("GBA: no rom\n"); return 0; }
    printf("GBA: init size=%u\n", (unsigned)size);
    g_loaded = 0;
    gba_rs_phase = 0;
    // r1145: «как у эталона» (mGBA-фронтенд НЕ добавляет DC-фильтр) — полное
    // отключение DC-блокера CPU2 на пути GBA. Откат фильтра r1143 (shift=5),
    // по решению владельца: звук должен быть без ФВЧ, как выводит эталон mGBA.
    i2s_dc_bypass(1);

    // Вендорное ядро пишет mLOG(...) напрямую в printf, пока не задан дефолтный
    // логгер (см. core/log.c:114). Гасим его тихой функцией — UART не спамим.
    mLogSetDefaultLogger(&gba_mgba_logger);

    if (g_core) {
        g_core->deinit(g_core);
        g_core = NULL;
    }

    g_core = mCoreCreate(mPLATFORM_GBA);
    if (!g_core) {
        printf("GBA: mCoreCreate failed\n");
        return 0;
    }
    mCoreInitConfig(g_core, NULL);
    if (!g_core->init(g_core)) {
        printf("GBA: core init failed\n");
        g_core->deinit(g_core);
        g_core = NULL;
        return 0;
    }

    // Видео: mGBA software-рендер пишет RGB565 прямо в EMU_FB (stride=EMU_W).
    // Ядро собрано с COLOR_16_BIT+COLOR_5_6_5 → mColor=uint16_t RGB565.
    memset((void*)EMU_FB, 0, EMU_W * EMU_H * sizeof(uint16_t));
    g_core->setVideoBuffer(g_core, (mColor*)EMU_FB, EMU_W);

    // Звук: setAudioBufferSize (около ~549 пар/кадр на 32768 Гц).
    g_core->setAudioBufferSize(g_core, 1024);

    // ROM из памяти: VFileMemChunk не владеет данными (map = тот же буфер).
    struct VFile* romvf = VFileMemChunk(rom, size);
    if (!g_core->loadROM(g_core, romvf)) {
        printf("GBA: loadROM failed\n");
        romvf->close(romvf);
        g_core->deinit(g_core);
        g_core = NULL;
        return 0;
    }

    // BIOS ВШИТ В ПРОШИВКУ (16 КБ, open-source, bios_data.S). mGBA-ядру
    // подаём его напрямую из образа — никакой зависимости от SD (r538).
    struct VFile* biosvf = VFileFromMemory(open_gba_bios_rom, 16 * 1024);
    if (g_core->loadBIOS(g_core, biosvf, 0))
        printf("GBA: BIOS embedded (open-source, 16K)\n");
    else
        printf("GBA: WARN BIOS not loaded (mGBA default used)\n");

    g_core->reset(g_core);

    g_loaded = 1;
    printf("GBA: loaded %u bytes\n", (unsigned)size);
    return 1;
}

void gba_run_frame(void) {
    if (!g_loaded || !g_core) return;

    gba_poll_keys();
    g_core->setKeys(g_core, g_keys);

    g_core->runFrame(g_core);
    if (g_core->frameCounter(g_core) == 0) {
        // Защита: очень ранний кадр (не пройден VBlank) — не растим звук.
    }

    // Звук: mGBA синтезирует 32768 Гц стерео s16-пары в mAudioBuffer.
    static int16_t sndbuf[4096];
    static int16_t rsbuf[GBA_RS_MAX_OUT * 2];
    struct mAudioBuffer* ab = g_core->getAudioBuffer(g_core);
    size_t avail = ab ? mAudioBufferAvailable(ab) : 0;
    if (avail > 2048) avail = 2048;
    u32 frames = 0;
    if (ab && avail) {
        frames = (u32)mAudioBufferRead(ab, sndbuf, avail);
    }
    u32 np = gba_resample(sndbuf, frames, rsbuf);
    // r1135: пакетная подача ОДНИМ i2s_push_block вместо цикла push_sample.
    // push_sample делает dsb (системный барьер) на КАЖДУЮ пару; push_block —
    // 1 ожидание места + 1 dsb на блок (~800 пар/кадр → 800 dsb экономится).
    // Backpressure внутри: ждёт освобождения (до 75% кольца), переполнение
    // дропает — ровно то, что нужно, когда mGBA даёт больше, чем CPU2 успевает.
    if (np)
        i2s_push_block(rsbuf, np);
    g_gba_pairs = np;
}

u32 gba_last_pairs(void) { return g_gba_pairs; }

void gba_render_frame(void) {
    // Видео уже отрендерено software-рендером mGBA прямо в EMU_FB (setVideoBuffer).
    // Здесь нечего копировать — буфер кадра и есть EMU_FB (240x160 в левом
    // верхнем углу 320x240). Ничего не делаем.
}