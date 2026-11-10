// puae_host.c — host-слой Sega... нет: Commodore Amiga (PUAE / libretro-uae).
// Подключает PUAE (WinUAE 5.3.1) к bare-metal H3: ADF-дискеты с SD через
// fs_layer (a8_fs_register), видео RGB565 из retro_bmp -> EMU_FB 320x240,
// звук 44100->48000 (ресемплер как fuse_host), ввод — Sega-геймпад
// (D-Pad + 2 fire) + USB-клавиатура.
//
// Жизненный цикл: emu_run_amiga() -> puae_retro_* (переименовано из retro_*):
//   puae_retro_set_environment/video/audio/input...
//   puae_retro_init -> puae_retro_load_game(&info) -> цикл puae_retro_run()
//   -> puae_retro_unload_game + puae_retro_deinit.
//
// Kickstart: НЕ грузим с SD (правило владельца — BIOS вшит). Ядро имеет
// встроенный AROS-фолбэк (aros.rom.c), который активируется, когда ядро
// не находит kick34005.A500/kick40068.A1200 в system-директории. Host
// возвращает для GET_SYSTEM_DIRECTORY пустую строку — ядро идёт в AROS.
// (Позже можно вшить настоящий Kickstart через bios_data.S-паттерн.)

#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include "libretro.h"

#include "emu.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "remap.h"
#include "i2s.h"

// ---- враппер ядра переименован в puae_* (puae_rename.sh) ----
void puae_retro_set_environment(retro_environment_t);
void puae_retro_set_video_refresh(retro_video_refresh_t);
void puae_retro_set_audio_sample(retro_audio_sample_t);
void puae_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void puae_retro_set_input_poll(retro_input_poll_t);
void puae_retro_set_input_state(retro_input_state_t);
void puae_retro_init(void);
bool puae_retro_load_game(const struct retro_game_info*);
void puae_retro_run(void);
void puae_retro_unload_game(void);
void puae_retro_deinit(void);
void puae_retro_get_system_av_info(struct retro_system_av_info*);
void puae_retro_reset(void);

// ---- путь образа (как c64_host: имя файла с SD) ----
static char g_path[160] = "game.adf";

// ---- кнопки Amiga-джойстика ----
// Джойстик: D-Pad + кнопка → left fire (BTN_A), 2-я кнопка → right/blue (BTN_B).
// Mapping на Sega-паде: крестовина = D-Pad, A = Fire 1, B = Fire 2.
static uint16_t g_joy = 0;

// ---- видео: RGB565 из retro_bmp -> EMU_FB 320x240 ----
#define EMU_FB_ADDR 0x5F800000u
#define EMU_FB_W    320
#define EMU_FB_H    240

static void host_video(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || width == 0 || height == 0) return;
    uint16_t* dst = (uint16_t*)EMU_FB_ADDR;
    const uint16_t* src = (const uint16_t*)data;
    size_t sp = pitch >> 1;   // байты -> uint16

    if (width <= EMU_FB_W && height <= EMU_FB_H) {
        for (unsigned y = 0; y < height; y++)
            memcpy(dst + (size_t)y * EMU_FB_W, src + (size_t)y * sp, width * sizeof(uint16_t));
    } else {
        // SuperHires (720x574) -> 320x240 (nearest), как fuse_host
        for (unsigned y = 0; y < EMU_FB_H; y++) {
            unsigned sy = (y * height) / EMU_FB_H;
            if (sy >= height) sy = height - 1;
            const uint16_t* srow = src + (size_t)sy * sp;
            uint16_t* drow = dst + (size_t)y * EMU_FB_W;
            for (unsigned x = 0; x < EMU_FB_W; x++)
                drow[x] = srow[(x * width) / EMU_FB_W];
        }
    }
}

// ---- звук: стерео 44100 Гц -> 48000 (шаг 60211, ресемплер как fuse_host) ----
static void host_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }

static size_t host_audio_sample_batch(const int16_t* d, size_t f)
{
    if (!d || f == 0) return f;
    static uint32_t rs_phase = 0;
    static int16_t out[2048 * 2];
    const size_t in_n = f;
    size_t o = 0;
    while (o < 2048) {
        uint32_t i = rs_phase >> 16;
        if (i + 1 >= (uint32_t)in_n) break;
        uint32_t fr = rs_phase & 0xFFFFu;
        int32_t l0 = d[2 * i],     l1 = d[2 * (i + 1)];
        int32_t r0 = d[2 * i + 1], r1 = d[2 * (i + 1) + 1];
        out[2 * o]     = (int16_t)(l0 + (int32_t)(((int64_t)(l1 - l0) * (int32_t)fr) >> 16));
        out[2 * o + 1] = (int16_t)(r0 + (int32_t)(((int64_t)(r1 - r0) * (int32_t)fr) >> 16));
        o++;
        rs_phase += 60211u;
    }
    if (rs_phase >= ((uint64_t)in_n << 16))
        rs_phase = (uint32_t)(rs_phase - ((uint64_t)in_n << 16));
    else
        rs_phase &= 0xFFFFu;
    for (size_t i = 0; i < o; i++)
        i2s_push_sample(out[2 * i], out[2 * i + 1]);
    return f;
}

// ---- ввод ----
// PUAE опрашивает input_state: RETRO_DEVICE_JOYPAD (D-Pad + B/A) и
// RETRO_DEVICE_KEYBOARD (RETROK_*). Джойстик — из Sega-пада, клавиатура — USB.
static uint8_t g_kbd[RETROK_LAST];
static uint16_t g_kbd_prev[RETROK_LAST];

// HID-сканкод -> RETROK (тот же маппинг, что в c64_host)
static uint16_t hid_to_retrok(uint8_t sc)
{
    switch (sc) {
    case 0x04: return RETROK_a;   case 0x05: return RETROK_b;   case 0x06: return RETROK_c;
    case 0x07: return RETROK_d;   case 0x08: return RETROK_e;   case 0x09: return RETROK_f;
    case 0x0A: return RETROK_g;   case 0x0B: return RETROK_h;   case 0x0C: return RETROK_i;
    case 0x0D: return RETROK_j;   case 0x0E: return RETROK_k;   case 0x0F: return RETROK_l;
    case 0x10: return RETROK_m;   case 0x11: return RETROK_n;   case 0x12: return RETROK_o;
    case 0x13: return RETROK_p;   case 0x14: return RETROK_q;   case 0x15: return RETROK_r;
    case 0x16: return RETROK_s;   case 0x17: return RETROK_t;   case 0x18: return RETROK_u;
    case 0x19: return RETROK_v;   case 0x1A: return RETROK_w;   case 0x1B: return RETROK_x;
    case 0x1C: return RETROK_y;   case 0x1D: return RETROK_z;
    case 0x1E: return RETROK_1;   case 0x1F: return RETROK_2;   case 0x20: return RETROK_3;
    case 0x21: return RETROK_4;   case 0x22: return RETROK_5;   case 0x23: return RETROK_6;
    case 0x24: return RETROK_7;   case 0x25: return RETROK_8;   case 0x26: return RETROK_9;
    case 0x27: return RETROK_0;
    case 0x28: return RETROK_RETURN;     case 0x29: return RETROK_ESCAPE;
    case 0x2A: return RETROK_BACKSPACE;  case 0x2B: return RETROK_TAB;
    case 0x2C: return RETROK_SPACE;
    case 0x2D: return RETROK_MINUS;      case 0x2E: return RETROK_EQUALS;
    case 0x2F: return RETROK_LEFTBRACKET;case 0x30: return RETROK_RIGHTBRACKET;
    case 0x31: return RETROK_BACKSLASH;  case 0x33: return RETROK_SEMICOLON;
    case 0x34: return RETROK_QUOTE;      case 0x35: return RETROK_BACKQUOTE;
    case 0x36: return RETROK_COMMA;      case 0x37: return RETROK_PERIOD;
    case 0x38: return RETROK_SLASH;      case 0x39: return RETROK_CAPSLOCK;
    case 0x3A: return RETROK_F1;   case 0x3B: return RETROK_F2;   case 0x3C: return RETROK_F3;
    case 0x3D: return RETROK_F4;   case 0x3E: return RETROK_F5;   case 0x3F: return RETROK_F6;
    case 0x40: return RETROK_F7;   case 0x41: return RETROK_F8;   case 0x42: return RETROK_F9;
    case 0x43: return RETROK_F10;  case 0x44: return RETROK_F11;  case 0x45: return RETROK_F12;
    case 0x49: return RETROK_INSERT;     case 0x4A: return RETROK_HOME;
    case 0x4B: return RETROK_PAGEUP;     case 0x4C: return RETROK_DELETE;
    case 0x4D: return RETROK_END;        case 0x4E: return RETROK_PAGEDOWN;
    case 0x4F: return RETROK_RIGHT;      case 0x50: return RETROK_LEFT;
    case 0x51: return RETROK_DOWN;       case 0x52: return RETROK_UP;
    case 0x53: return RETROK_KP_PERIOD;
    case 0x54: return RETROK_KP_DIVIDE;  case 0x55: return RETROK_KP_MULTIPLY;
    case 0x56: return RETROK_KP_MINUS;   case 0x57: return RETROK_KP_PLUS;
    case 0x58: return RETROK_KP_ENTER;
    case 0x59: return RETROK_KP1;  case 0x5A: return RETROK_KP2;  case 0x5B: return RETROK_KP3;
    case 0x5C: return RETROK_KP4;  case 0x5D: return RETROK_KP5;  case 0x5E: return RETROK_KP6;
    case 0x5F: return RETROK_KP7;  case 0x60: return RETROK_KP8;  case 0x61: return RETROK_KP9;
    case 0x62: return RETROK_KP0;
    default: return 0;
    }
}

static void host_input_poll(void)
{
    memset(g_kbd, 0, sizeof(g_kbd));
    uint8_t keys[8];
    int n = usb_kbd_get_raw(keys, 8);
    for (int i = 0; i < n; i++) {
        uint16_t rk = hid_to_retrok(keys[i]);
        if (rk) g_kbd[rk] = 1;
    }
    uint8_t mods = usb_kbd_get_mods();
    if (mods & 0x02) g_kbd[RETROK_LSHIFT] = 1;
    if (mods & 0x20) g_kbd[RETROK_RSHIFT] = 1;
    if (mods & 0x01) g_kbd[RETROK_LCTRL] = 1;
    if (mods & 0x10) g_kbd[RETROK_RCTRL] = 1;
    if (mods & 0x04) g_kbd[RETROK_LALT] = 1;
    if (mods & 0x40) g_kbd[RETROK_RALT] = 1;

    uint16_t sp = pad_scan_combined();
    uint16_t j = 0;
    if (sp & 0x0001) j |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
    if (sp & 0x0002) j |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (sp & 0x0004) j |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (sp & 0x0008) j |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (sp & 0x0010) j |= (1u << RETRO_DEVICE_ID_JOYPAD_B);   // Fire 1 (A)
    if (sp & 0x0020) j |= (1u << RETRO_DEVICE_ID_JOYPAD_A);   // Fire 2 (B)
    if (sp & 0x0080) j |= (1u << RETRO_DEVICE_ID_JOYPAD_START); // Start
    g_joy = j;
}

static int16_t host_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port; (void)index;
    if (device == RETRO_DEVICE_JOYPAD)
        return (g_joy & (1u << id)) ? 1 : 0;
    if (device == RETRO_DEVICE_KEYBOARD)
        return (id < RETROK_LAST && g_kbd[id]) ? 1 : 0;
    return 0;
}

// ---- environment: опции ядра + ключевые настройки ----
// Встроенный AROS-фолбэк (Kickstart не нужен) — правило владельца: BIOS вшит,
// с SD не грузится. Директории возвращаем "/" (fs_layer) — туда ядро пытается
// сходить за kick34005.A500, не находит → AROS.
static void puae_log(enum retro_log_level lvl, const char* fmt, ...)
{
    if (lvl < RETRO_LOG_WARN) return;
    extern int uart0_printf(const char* fmt, ...);
    uart0_printf("  puae# ");
    if (fmt) {
        va_list ap;
        char buf[512];
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        buf[sizeof(buf) - 1] = 0;
        uart0_printf("%s", buf);
    }
}

extern int uart0_printf(const char* fmt, ...);

static bool host_environment(unsigned cmd, void* data)
{
    // r1207-ДИАГ: трассировка первых 80 env-команд. puae_retro_load_game
    // общается с фронтендом только через этот колбэк — по последнему номеру
    // видно, на каком шаге загрузка встала (стенд r1206: hang без abort
    // после выделения 1.5 МБ).
    {
        static unsigned s_env_n;
        if (s_env_n < 80) { uart0_printf("PUAE env %u\n", (unsigned)cmd); s_env_n++; }
    }
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return (*(unsigned*)data) == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback* cb = (struct retro_log_callback*)data;
        if (cb) cb->log = puae_log;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        return false;   // пусто — ядро идёт в AROS-фолбэк (вшитый)
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable* v = (struct retro_variable*)data;
        if (v && v->key) {
            if (strcmp(v->key, "puae_model") == 0)    { v->value = "auto"; return true; }
            if (strcmp(v->key, "puae_kickstart") == 0) { v->value = "aros"; return true; }
            if (strcmp(v->key, "puae_fastmem_size") == 0) { v->value = "0"; return true; }
            if (strcmp(v->key, "puae_chipmem_size") == 0) { v->value = "1"; return true; }
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        return false;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
        bool* b = (bool*)data; if (b) *b = true; return true;
    }
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: {
        bool* b = (bool*)data; if (b) *b = true; return true;
    }
    default:
        return false;
    }
}

// ---- заглушки dirent (retro_dirent.c исключён: newlib не имеет <dirent.h>).
// PUAE зовёт retro_opendir/retro_readdir для сканирования system-директории
// (список Kickstart) и TEMP-папки ZIP. Ядро PUAЕ переименовано в puae_retro_*
// (puae_rename.sh) — стабы называем так же, иначе голые retro_* конфликтуют
// с такими же символами других ядер (msx_compat.c и др.; linkcheck: дубли).
// В bare-metal системной директории нет (Kickstart вшит/AROS) — «список пуст».
struct RDIR;
struct RDIR* puae_retro_opendir(const char* path) { (void)path; return 0; }
struct RDIR* puae_retro_opendir_include_hidden(const char* path, bool include_hidden) { (void)path; (void)include_hidden; return 0; }
int puae_retro_readdir(struct RDIR* rdir) { (void)rdir; return 0; }
const char* puae_retro_dirent_get_name(struct RDIR* rdir) { (void)rdir; return 0; }
void puae_retro_dirent_get_name_full(struct RDIR* rdir, char* buf, size_t len) { (void)rdir; (void)buf; (void)len; }
bool puae_retro_dirent_is_dir(struct RDIR* rdir, const char* n) { (void)rdir; (void)n; return false; }
void puae_retro_closedir(struct RDIR* rdir) { (void)rdir; }

// ---- POSIX-стабы для newlib: ftello64/fseeko64 объявлены в stdio.h, но не
// реализованы в libc; chmod/truncate отсутствуют (fsdb-код hardfile их зовёт,
// в bare-metal файлы идут через VFS/fs_layer — реальные вызовы не достигают).
#include <stdio.h>
_off64_t ftello64(FILE* f) { return (_off64_t)ftello(f); }
_off64_t fseeko64(FILE* f, _off64_t off, int whence) { return (_off64_t)fseeko(f, (_off_t)off, whence); }
int chmod(const char* path, unsigned mode) { (void)path; (void)mode; return 0; }
int truncate(const char* path, long length) { (void)path; (void)length; return 0; }

// ---- собственный аллокатор PUAE (puae_malloc/puae_calloc/puae_realloc/puae_free)
// на sbrk-зоне (0x4B700000+), куда пул gameboy_stubs НЕ ходит. Причина:
// глобальные malloc/calloc в проекте — это gb-пул (.gb_heap, 0x4A700000),
// bump-указатель которого на стенде дошёл до 0x4A800000 → Data Abort
// (пересечение зон). puae_rename.sh переименовывает все ссылки malloc/
// calloc/realloc/free в объектах ядра на функции здесь.
// Простой bump + LIFO-free: сессия Amiga живёт от emu_run_amiga до выхода,
// память сбрасывается при повторном входе (sbrk-бросок в emu_prepare).
extern void* _sbrk(int incr);          // libc_min.c (bump, SBRK_LIMIT 0x4EF00000)
static char* g_puae_brk = 0;
static char* g_puae_brk_low = 0;       // LIFO-свободный хвост (realloc/free хвоста)
static size_t g_puae_last = 0;         // размер последнего блока

static void puae_heap_reset(void)
{
    g_puae_brk = 0;
    g_puae_brk_low = 0;
    g_puae_last = 0;
}

void* puae_malloc(size_t sz)
{
    if (sz == 0) sz = 1;
    if (!g_puae_brk) {
        g_puae_brk = (char*)_sbrk(0);
        if (g_puae_brk == (char*)-1) return 0;
        g_puae_brk_low = g_puae_brk;
    }
    char* p = g_puae_brk;
    uintptr_t a = ((uintptr_t)p + 7u) & ~(uintptr_t)7u;
    if (_sbrk((int)(a - (uintptr_t)p + (uintptr_t)sz + 8)) == (void*)-1) return 0;
    ((size_t*)a)[0] = sz;              // заголовок [size]
    g_puae_brk = (char*)(a + sz + 8);
    g_puae_last = sz;
    // r1199-ДИАГ: крупные выделения + защита от ухода brk из sbrk-зоны
    // (0x4BF00000..0x4EF00000). Если brk вне диапазона — это следствие
    // порчи указателя (LIFO-free чужого указателя и т.п.), а не нехватки
    // памяти: печатаем и отказываем, чтобы не улететь memset'ом в мусор.
    if ((uintptr_t)g_puae_brk < 0x4BF00000u || (uintptr_t)g_puae_brk > 0x4EF00000u) {
        printf("PUAE: brk OUT %08X (sz=%u)\n",
               (unsigned)(uintptr_t)g_puae_brk, (unsigned)sz);
        g_puae_brk = (char*)(uintptr_t)(-1);
        return 0;
    }
    if (sz >= 65536u) {
        static unsigned s_big_n;
        if (s_big_n < 24) {
            printf("PUAE: alloc %u -> %08X (brk=%08X)\n", (unsigned)sz,
                   (unsigned)(a + 8), (unsigned)(uintptr_t)g_puae_brk);
            s_big_n++;
        }
    }
    return (void*)(a + 8);
}

void puae_free(void* p)
{
    if (!p) return;
    uint8_t* b = (uint8_t*)p - 8;
    size_t sz = ((size_t*)b)[0];
    if ((char*)b + sz + 8 >= g_puae_brk - (g_puae_last + 8) && (char*)b + 8 >= g_puae_brk - (g_puae_last + 8)) {
        // LIFO: освобождаем хвост — откатываем brk
        // r1199-ДИАГ: первые 16 откатов — в лог. Если brk откатывается на
        // указатель ВНЕ sbrk-зоны, следующий malloc вернёт мусор (стенд:
        // memset по 0x467AB5F0 = символ puae_spixels в .bss).
        static unsigned s_free_n;
        if (s_free_n < 16) {
            printf("PUAE: free rewind %08X sz=%u (brk %08X)\n",
                   (unsigned)(uintptr_t)b, (unsigned)sz,
                   (unsigned)(uintptr_t)g_puae_brk);
            s_free_n++;
        }
        g_puae_brk = (char*)b;
        g_puae_last = 0;
        if (g_puae_brk < g_puae_brk_low) g_puae_brk = g_puae_brk_low;
    }
}

void* puae_calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    void* p = puae_malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* puae_realloc(void* p, size_t sz)
{
    if (!p) return puae_malloc(sz);
    uint8_t* b = (uint8_t*)p - 8;
    size_t old = ((size_t*)b)[0];
    if (old >= sz) return p;           // shrink — оставляем (bump)
    void* np = puae_malloc(sz);
    if (!np) return 0;
    memcpy(np, p, old);
    puae_free(p);
    return np;
}

// PUAE-версии strdup/strndup: глобальный newlib-strdup аллоцировал бы через
// gb-пул; здесь — через puae_malloc (та же sbrk-зона).
char* puae_strdup(const char* s)
{
    if (!s) return 0;
    size_t n = strlen(s) + 1;
    char* p = (char*)puae_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char* puae_strndup(const char* s, size_t n)
{
    if (!s) return 0;
    size_t l = 0;
    while (l < n && s[l]) l++;
    char* p = (char*)puae_malloc(l + 1);
    if (p) { memcpy(p, s, l); p[l] = 0; }
    return p;
}

// ---- точка входа emu.c (как c64/fuse) ----
void emu_run_amiga(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    puae_heap_reset();
    emu_prepare();
    // r1199-ДИАГ: снимок L1 перед запуском PUAE (после загрузки ROM с USB):
    // сравнить с L1["boot"] из main.c — покажет, менялась ли таблица и когда.
    extern void mmu_l1_dump(const char* tag);
    mmu_l1_dump("amiga");
    // r1198-ДИАГ: проверка карты sbrk-зоны ДО первого выделения PUAE
    // (сжато: печатаем только сводку; при провале — первую плохую секцию).
    {
        volatile uint32_t* p;
        unsigned bad = 0, bad_mb = 0, n = 0;
        for (p = (volatile uint32_t*)0x4BF00000u;
             p < (volatile uint32_t*)0x4EF00000u;
             p += 0x100000u / sizeof(*p), n++) {
            uint32_t save = *p;
            *p = 0xA5A5A5A5u;
            if (*p != 0xA5A5A5A5u) { bad++; if (!bad_mb) bad_mb = n; }
            *p = save;
        }
        printf("sbrk-map: %u/%u OK%s\n", n - bad, n, bad ? " (BAD!)" : "");
        if (bad) printf("sbrk-map: first bad MB=%u\n", bad_mb);
    }
    snd_manifest("amiga", "paula");

    if (rom && rom_name && rom_name[0]) {
        size_t n = strlen(rom_name);
        if (n >= sizeof(g_path)) n = sizeof(g_path) - 1;
        memcpy(g_path, rom_name, n);
        g_path[n] = 0;
    } else if (rom) {
        strcpy(g_path, "game.adf");
    } else {
        g_path[0] = 0;
    }

    // Расширение → нижний регистр (как c64 r1003): PUAE определяет тип
    // образа по расширению файла (.adf), на флешке может быть .ADF.
    {
        char* dot = 0;
        for (char* p = g_path; *p; p++)
            if (*p == '.') dot = p;
        if (dot) {
            for (char* p = dot + 1; *p; p++)
                if (*p >= 'A' && *p <= 'Z') *p += 32;
        }
    }

    // Регистрируем ADF в едином файловом слое (fs_layer/a8_fs.o).
    extern void a8_fs_register(const char* path, const void* data, uint32_t size);
    a8_fs_register(g_path, rom, size);

    puae_retro_set_environment(host_environment);
    puae_retro_set_video_refresh(host_video);
    puae_retro_set_audio_sample(host_audio_sample);
    puae_retro_set_audio_sample_batch(host_audio_sample_batch);
    puae_retro_set_input_poll(host_input_poll);
    puae_retro_set_input_state(host_input_state);

    printf("Amiga: init...\n");
    puae_retro_init();
    printf("Amiga: init ok\n");

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = g_path;
    info.data = (void*)rom;
    info.size = size;

    printf("Amiga: load...\n");
    if (!puae_retro_load_game(&info)) {
        printf("Amiga: load failed\n");
        puae_retro_deinit();
        return;
    }
    printf("Amiga: started (%s, %u bytes)\n", g_path[0] ? g_path : "AROS/BIOS", (unsigned)size);

    emu_set_border_color(0x00080814);   // тёмно-синий
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        puae_retro_run();
        emu_throttle();
        emu_scale(320, 240);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    puae_retro_unload_game();
    puae_retro_deinit();
    emu_audio_stop();
    fb_clear();
    fb_flush();
}