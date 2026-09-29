// cps1_host.c — FinalBurn Neo, CPS-1 (Capcom Play System 1) host for H3 bare-metal.
//
// Ядро: h3_bare/cores/cps1/ (vendored подмножество FBNeo, коммит d025cfc).
// В отличие от libretro-систем это «нативный» FBNeo: host сам выбирает
// драйвер (BurnDrvGetIndex по короткому имени игры), задаёт BurnExtLoadRom
// (чтение ROM-сета) и крутит цикл: BurnDrvFrame() → кадр RGB565 384×224
// (pBurnDraw) → прямой ресайз в HDMI FB 1024×600 (как bk_host).
// Звук ОТКЛЮЧЁН (nBurnSoundRate = 0, pBurnSoundOut не выделяется —
// драйверы CPS рендерят звук только при pBurnSoundOut).
//
// ROM-сет: /roms/cps1/<игра>/ или /roms/cps2/<игра>/ (папка с сырыми
// дампами чипов — приоритет)
//     или /roms/cps1|/roms/cps2/<игра>.zip (распаковка через zlib из fuse/zlib).
// Игра идентифицируется по имени выбранного в браузере элемента (папки
// или zip-файла); в драйвере поддерживаются: wof, kod, unsquad, varth,
// willow, 3wonders (см. burn/driverlist.h).
//
// Ввод: P1 — клавиатура (ремап-платформа REMAP_PLAT_CPS1, дефолт: стрелки +
// Z=Attack X=Jump C=Fire3, Enter/1=Start, S=Coin (fallback 5/Numpad5)
// + Sega-пад (крестовина, A=Attack B=Jump C=Fire3 Start=Start X=Coin).
// P2 — хардкод: WASD движение, J/K/L = Attack/Jump/Fire3, 2=Start, 6=Coin.
// Выход: ESC-удержание (emu_esc_hold, ~0.9 с).

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "burnint.h"
#include "driverlist.h"
#include "zlib.h"

extern "C" {
#include "emu.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "remap.h"
#include "fb_text.h"
#include "fat.h"
#include "h3_hs_timer.h"
}

// ---- FBNeo extern (burn.cpp / драйвер) ----
extern UINT32 nBurnDrvActive;
extern UINT8* pBurnDraw;
extern INT32 nBurnPitch;
extern INT32 nBurnBpp;
// BurnHighCol — конвертация R/G/B в наш пиксельный формат (RGB565).
// Вендорный filler возвращает ~0 (белый) — без host-функции экран белый.
extern UINT32 (__cdecl *BurnHighCol)(INT32 r, INT32 g, INT32 b, INT32 i);

static UINT32 __cdecl host_high_col(INT32 r, INT32 g, INT32 b, INT32 i)
{
    (void)i;
    return (uint32_t)(((r & 0xFF) >> 3) << 11) |
           (uint32_t)(((g & 0xFF) >> 2) << 5)  |
           (uint32_t)((b & 0xFF) >> 3);
}

// входные массивы драйвера CPS (cps_rw.cpp):
// CpsInp001 = P1 (0=Right 1=Left 2=Down 3=Up 4=Attack 5=Jump 6=Fire3)
// CpsInp000 = P2 (тот же порядок бит)
// CpsInp018 = монеты/старты (0=P1Coin 1=P2Coin 2=Service 4=P1Start 5=P2Start 6=Diag)
extern UINT8 CpsInp001[8];
extern UINT8 CpsInp000[8];
extern UINT8 CpsInp018[8];
// CPS-2 (d_cps2) использует другие регистры: Coin=020+4/+5, Start=020+0/+1,
// кики=011 (P1 +0..2, P2 +4..6), diag/service=021.
extern UINT8 CpsInp011[8];
extern UINT8 CpsInp020[8];
extern UINT8 CpsInp021[8];
extern UINT8 CpsInp010[8];

// ---- Видео ----
// CPS-1 рендерит 384×224 RGB565. Пишем напрямую в HDMI FB 1024×600
// (аспект 384/224 ≈ 1024/600) ближайшим соседом — без промежуточного
// EMU_FB (в него 384 колонки не влезают без потерь).
#define CPS1_W  384
#define CPS1_H  224
#define FB_ADDR ((uint32_t*)0x5F900000u)
#define FB_W    1024
#define FB_H    600
static uint16_t g_frame[CPS1_W * CPS1_H];

// ---- ROM-источники (выбранная игра) ----
// zip-буферы: основной — 0x50000000 (64 МБ), родителя — 0x54000000 (64 МБ).
// Между ними запас, выше до EMU_FB/HDMI (~0x5F800000) ещё ~100 МБ свободно;
// реальные сеты CPS-2 (ddsom — самый большой) ~33 МБ сырых.
#define ZIP_BUF1 ((uint8_t*)0x50000000u)
#define ZIP_BUF2 ((uint8_t*)0x5C000000u)
#define ZIP_MAX  (92u * 1024u * 1024u)   // крупнейшие сеты ~84 МБ (NEOGEO)
#define ZIP_MAX2 (48u * 1024u * 1024u)   // буфер родителя ниже EMU_FB
static char g_dir[FAT_NAME_LEN + 16];   // "/roms/cps1/<game>"
static char g_zip[FAT_NAME_LEN + 16];   // "/roms/cps1/<game>.zip"
static const uint8_t* g_zip_data = NULL;
static uint32_t g_zip_size = 0;
static char g_parent_dir[FAT_NAME_LEN + 16];   // "/roms/cps1/<parent>" (клоны)
static char g_parent_zip[FAT_NAME_LEN + 16];
static char g_bios_dir[FAT_NAME_LEN + 16];    // BIOS (NEOGEO): root + "/neogeo"
static char g_bios_zip[FAT_NAME_LEN + 16];
static const uint8_t* g_bios_zip_data = NULL;
static uint32_t g_bios_zip_size = 0;
static const uint8_t* g_zip_parent_data = NULL;
static uint32_t g_zip_parent_size = 0;

// ---- Ввод: маски Sega-пада ----
#define SP_UP    0x0001
#define SP_DOWN  0x0002
#define SP_LEFT  0x0004
#define SP_RIGHT 0x0008
#define SP_A     0x0010
#define SP_B     0x0020
#define SP_C     0x0040
#define SP_START 0x0080
#define SP_X     0x0100
#define SP_Y     0x0200
#define SP_Z     0x0400

// ---- маленькие LE-хелперы для zip ----
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// Извлечение файла из zip-буфера (STORE или DEFLATE/raw). 0=ok, 1=нет/ошибка.
static int zip_extract(const uint8_t* z, uint32_t zsize, const char* target,
                       uint8_t* out, uint32_t maxout, uint32_t* got)
{
    if (!z || zsize < 22) return 1;
    // EOCD: ищем сигнатуру с конца (комментарий до 65535 байт)
    uint32_t lo = (zsize > 22 + 65535) ? (zsize - 22 - 65535) : 0;
    int32_t eocd = -1;
    for (uint32_t p = zsize - 22; ; p--) {
        if (z[p] == 0x50 && z[p + 1] == 0x4b && z[p + 2] == 0x05 && z[p + 3] == 0x06) { eocd = (int32_t)p; break; }
        if (p == lo) break;
    }
    if (eocd < 0) return 1;
    uint32_t count   = le16(z + eocd + 10);
    uint32_t cd_off  = le32(z + eocd + 16);
    uint32_t cd_size = le32(z + eocd + 12);
    if (cd_off > zsize || cd_size > zsize - cd_off) return 1;
    uint32_t tlen = (uint32_t)strlen(target);

    // сканируем Central Directory
    for (uint32_t p = cd_off; p + 46 <= cd_off + cd_size && count-- > 0; ) {
        if (z[p] != 0x50 || z[p + 1] != 0x4b || z[p + 2] != 0x01 || z[p + 3] != 0x02) return 1;
        uint16_t method   = le16(z + p + 10);
        uint32_t csize    = le32(z + p + 20);
        uint32_t usize    = le32(z + p + 24);
        uint16_t nlen     = le16(z + p + 28);
        uint16_t elen     = le16(z + p + 30);
        uint16_t clen     = le16(z + p + 32);
        uint32_t lho      = le32(z + p + 42);
        const uint8_t* nm = z + p + 46;
        p += 46 + nlen + elen + clen;

        if (nlen < tlen) continue;
        int match = (nlen == tlen && memcmp(nm, target, tlen) == 0);
        if (!match && nlen > tlen && nm[nlen - tlen - 1] == '/' &&
            memcmp(nm + nlen - tlen, target, tlen) == 0)
            match = 1;   // MAME: файлы лежат в подпапке внутри zip
        if (!match) continue;

        // local header: проверить сигнатуру и смещение данных
        if (lho + 30 > zsize || z[lho] != 0x50 || z[lho + 1] != 0x4b ||
            z[lho + 2] != 0x03 || z[lho + 3] != 0x04) return 1;
        uint32_t data = lho + 30 + le16(z + lho + 26) + le16(z + lho + 28);
        if (data + csize > zsize) return 1;
        if (usize > maxout) return 1;

        if (method == 0) {
            memcpy(out, z + data, usize);
            if (got) *got = usize;
            return 0;
        }
        if (method == 8) {
            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            if (inflateInit2(&zs, -15) != Z_OK) return 1;
            zs.next_in  = (Bytef*)(z + data);
            zs.avail_in = csize;
            zs.next_out = out;
            zs.avail_out = maxout;
            int r = inflate(&zs, Z_FINISH);
            inflateEnd(&zs);
            if (r != Z_STREAM_END) return 1;
            if (got) *got = (uint32_t)zs.total_out;
            return 0;
        }
        return 1;   // незнакомый метод сжатия
    }
    return 1;
}

// чтение ROM-файла из папки (dir — "/roms/cps1/<имя>"). 0=ok, 1=нет/ошибка
static int load_from_dir(const char* dir, const char* romname, UINT8* dest, INT32 want, INT32* pnWrote)
{
    if (!dir || !dir[0]) return 1;
    fat_entry_t f;
    if (!fat_find(dir, romname, &f) || f.size <= 0) return 1;
    if ((uint32_t)f.size < (uint32_t)want) return 1;
    if (fat_read_file(&f, 0, dest, (uint32_t)want) < 0) return 1;
    // сливаем dirty-линии D-cache (write-back) в DRAM — как rom_browser
    uint32_t a = ((uint32_t)dest) & ~0x1Fu;
    uint32_t end = a + (uint32_t)want + 32;
    for (; a < end; a += 32)
        __asm volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(a));
    __asm volatile("dsb" ::: "memory");
    if (pnWrote) *pnWrote = want;
    return 0;
}

// поиск записи в zip по имени (CEN), без распаковки → размер. 0=ok, 1=нет
static int zip_lookup(const uint8_t* z, uint32_t zsize, const char* target, uint32_t* usize)
{
    if (!z || zsize < 22) return 1;
    uint32_t lo = (zsize > 22 + 65535) ? (zsize - 22 - 65535) : 0;
    int32_t eocd = -1;
    for (uint32_t p = zsize - 22; ; p--) {
        if (z[p] == 0x50 && z[p + 1] == 0x4b && z[p + 2] == 0x05 && z[p + 3] == 0x06) { eocd = (int32_t)p; break; }
        if (p == lo) break;
    }
    if (eocd < 0) return 1;
    uint32_t count   = le16(z + eocd + 10);
    uint32_t cd_off  = le32(z + eocd + 16);
    uint32_t cd_size = le32(z + eocd + 12);
    if (cd_off > zsize || cd_size > zsize - cd_off) return 1;
    uint32_t tlen = (uint32_t)strlen(target);
    for (uint32_t p = cd_off; p + 46 <= cd_off + cd_size && count-- > 0; ) {
        if (z[p] != 0x50 || z[p + 1] != 0x4b || z[p + 2] != 0x01 || z[p + 3] != 0x02) return 1;
        uint32_t uv = le32(z + p + 24);
        uint16_t nlen = le16(z + p + 28);
        uint16_t elen = le16(z + p + 30);
        uint16_t clen = le16(z + p + 32);
        const uint8_t* nm = z + p + 46;
        p += 46 + nlen + elen + clen;
        if (nlen < tlen) continue;
        int match = (nlen == tlen && memcmp(nm, target, tlen) == 0);
        if (!match && nlen > tlen && nm[nlen - tlen - 1] == '/' &&
            memcmp(nm + nlen - tlen, target, tlen) == 0)
            match = 1;
        if (match) { if (usize) *usize = uv; return 0; }
    }
    return 1;
}

// ---- прогресс загрузки CPS на HDMI (не смотреть на чёрный экран) ----
static int s_prog_total = 0;
static int s_prog_done  = 0;
static char g_load_name[32] = "";

static void load_progress(const char* phase)
{
    // рисуем в HDMI FB напрямую (EMU_FB в это время не нужен)
    char buf[64];
    if (s_prog_total > 0) {
        int pct = (s_prog_done * 100) / s_prog_total;
        if (pct > 100) pct = 100;
        snprintf(buf, sizeof(buf), "Loading %s: %d/%d (%d%%)",
                 g_load_name, s_prog_done, s_prog_total, pct);
    } else {
        snprintf(buf, sizeof(buf), "Loading %s %s", g_load_name, phase ? phase : "");
    }
    fb_clear();
    fb_text_center(buf, 280, 2, 0x00FFFFFF);
    if (s_prog_total > 0) {
        int w = (s_prog_done * 800) / s_prog_total;
        if (w > 0) fb_fill_rect(112, 316, w, 22, 0x00FFAA00);
        fb_fill_rect(112, 338, 800, 3, 0x00333333);   // шкала-подложка
    }
    fb_flush();
}

// ---- проверка полноты ROM-сета (имена и размеры вшиты в драйвер) ----
// Возвращает 1, если есть missing или неверные размеры. Печатает список.
static int check_romset(const char* game)
{
    int total = 0, missing = 0, bad = 0;
    for (int i = 0; i < 256; i++) {
        struct BurnRomInfo ri;
        ri.nType = 0; ri.nLen = 0;
        BurnDrvGetRomInfo(&ri, (UINT32)i);
        if (ri.nType == 0 && ri.nLen == 0) continue;   // пустые слоты (break ломал NEO: BIOS на 128+)
        if (ri.nLen == 0) continue;
        if (ri.nType & BRF_OPT) continue;   // PLD-микросхемы драйвер не грузит
        total++;
        char* name = NULL;
        BurnDrvGetRomName(&name, (UINT32)i, 0);
        if (!name || !name[0]) continue;

        uint32_t sz = 0;
        int found = 0;
        fat_entry_t f;
        if (!found && (ri.nType & BRF_BIOS)) {
            if (fat_find(g_bios_dir, name, &f) && f.size > 0) { found = 1; sz = (uint32_t)f.size; }
            else if (g_bios_zip_data && zip_lookup(g_bios_zip_data, g_bios_zip_size, name, &sz) == 0) found = 1;
        }
        if (!found && g_dir[0])  { if (fat_find(g_dir, name, &f) && f.size > 0)  { found = 1; sz = (uint32_t)f.size; } }
        if (!found && g_parent_dir[0]) { if (fat_find(g_parent_dir, name, &f) && f.size > 0) { found = 1; sz = (uint32_t)f.size; } }
        if (!found && g_zip_data)        { if (zip_lookup(g_zip_data, g_zip_size, name, &sz) == 0) found = 1; }
        if (!found && g_zip_parent_data) { if (zip_lookup(g_zip_parent_data, g_zip_parent_size, name, &sz) == 0) found = 1; }

        if (!found) {
            printf("MISSING %s\n", name);
            missing++;
        } else if (sz != (uint32_t)ri.nLen) {
            printf("BAD SIZE %s need 0x%x have 0x%x\n", name, (unsigned)ri.nLen, (unsigned)sz);
            bad++;
        }
    }
    if (total == 0) return 1;
    s_prog_total = total;
    printf("RS %s: %d ok / %d total (%d missing, %d bad size)\n", game, total - missing - bad, total, missing, bad);
    return (missing || bad) ? 1 : 0;
}

// ---- BurnExtLoadRom: чтение i-го ROM драйвера ----
// Источники по порядку: папка игры, папка родителя (для клонов), zip игры,
// zip родителя.
static INT32 host_ext_load_rom(UINT8* Dest, INT32* pnWrote, INT32 i)
{
    struct BurnRomInfo ri;
    ri.nType = 0; ri.nLen = 0;
    BurnDrvGetRomInfo(&ri, i);
    if (ri.nType == 0 || ri.nLen == 0) { if (pnWrote) *pnWrote = 0; return 0; }

    char* name = NULL;
    BurnDrvGetRomName(&name, i, 0);
    if (!name || !name[0]) return 1;

    if (ri.nType & BRF_BIOS) {
        // BIOS (NEOGEO): файлы лежат в /roms/<root>/neogeo/ или neogeo.zip
        if (load_from_dir(g_bios_dir, name, Dest, ri.nLen, pnWrote) == 0) {
            if (s_prog_total) { s_prog_done++; load_progress(""); }
            return 0;
        }
        if (g_bios_zip_data) {
            uint32_t got = 0;
            if (zip_extract(g_bios_zip_data, g_bios_zip_size, name, Dest, (uint32_t)ri.nLen, &got) == 0 && got > 0) {
                if (pnWrote) *pnWrote = (INT32)got;
                if (s_prog_total) { s_prog_done++; load_progress(""); }
                return 0;
            }
        }
    }
    if (load_from_dir(g_dir, name, Dest, ri.nLen, pnWrote) == 0) {
        if (s_prog_total) { s_prog_done++; load_progress(""); }
        return 0;
    }
    if (load_from_dir(g_parent_dir, name, Dest, ri.nLen, pnWrote) == 0) {
        if (s_prog_total) { s_prog_done++; load_progress(""); }
        return 0;
    }
    if (g_zip_data) {
        uint32_t got = 0;
        if (zip_extract(g_zip_data, g_zip_size, name, Dest, (uint32_t)ri.nLen, &got) == 0 && got > 0) {
            if (pnWrote) *pnWrote = (INT32)got;
            if (s_prog_total) { s_prog_done++; load_progress(""); }
            return 0;
        }
    }
    if (g_zip_parent_data) {
        uint32_t got = 0;
        if (zip_extract(g_zip_parent_data, g_zip_parent_size, name, Dest, (uint32_t)ri.nLen, &got) == 0 && got > 0) {
            if (pnWrote) *pnWrote = (INT32)got;
            if (s_prog_total) { s_prog_done++; load_progress(""); }
            return 0;
        }
    }

    printf("CPS: missing rom %s (index %d)\n", name, (int)i);
    if (pnWrote) *pnWrote = 0;
    return 1;
}

// ---- рендер кадра pBurnDraw (RGB565 384×224) в HDMI FB 1024×600 ----
static void host_render_frame(void)
{
    const uint16_t* src = (const uint16_t*)pBurnDraw;
    if (!src) return;
    uint32_t* dst = FB_ADDR;
    uint32_t step_x = ((uint32_t)CPS1_W << 16) / (uint32_t)FB_W;
    uint32_t acc_x = step_x >> 1;
    static uint16_t sx[FB_W];
    for (int dx = 0; dx < FB_W; dx++) {
        sx[dx] = (uint16_t)(acc_x >> 16);
        acc_x += step_x;
        if (acc_x >= ((uint32_t)CPS1_W << 16)) acc_x -= (uint32_t)CPS1_W << 16;
    }
    uint32_t step_y = ((uint32_t)CPS1_H << 16) / (uint32_t)FB_H;
    uint32_t y_acc = step_y >> 1;
    int sy = 0;
    for (int dy = 0; dy < FB_H; dy++) {
        const uint16_t* srow = src + (size_t)sy * (nBurnPitch >> 1);
        uint32_t* drow = dst + (size_t)dy * FB_W;
        for (int dx = 0; dx < FB_W; dx++) {
            uint16_t p = srow[sx[dx]];
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5) & 0x3F) << 2;
            uint32_t b = (p & 0x1F) << 3;
            drow[dx] = (r << 16) | (g << 8) | b;
        }
        y_acc += step_y;
        int nsy = (int)(y_acc >> 16);
        if (nsy > sy) { if (nsy >= CPS1_H) nsy = CPS1_H - 1; sy = nsy; }
    }
}

// ---- ввод ----
static void host_update_input(void)
{
    memset(CpsInp001, 0, sizeof(CpsInp001));
    memset(CpsInp000, 0, sizeof(CpsInp000));
    memset(CpsInp018, 0, sizeof(CpsInp018));
    memset(CpsInp011, 0, sizeof(CpsInp011));
    memset(CpsInp020, 0, sizeof(CpsInp020));
    memset(CpsInp021, 0, sizeof(CpsInp021));
    memset(CpsInp010, 0, sizeof(CpsInp010));

    // Клавиатура: штатный usb_kbd_get_raw (внутри себя держит окно 100 мс:
    // повтор пришёл ≤100 мс — клавиша держится, тишина дольше — отпущена;
    // новые изменения отчёта — новое нажатие).
    uint8_t keys[8];
    int n = usb_kbd_get_raw(keys, 8);

    // P1 — ремап-платформа CPS-1 (регистры CPS-1: Coin=018, Start=018;
    // CPS-2 ждёт их в 020, поэтому пишем в оба)
    if (remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_UP,     keys, n)) CpsInp001[3] = 1;
    if (remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_DOWN,   keys, n)) CpsInp001[2] = 1;
    if (remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_LEFT,   keys, n)) CpsInp001[1] = 1;
    if (remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_RIGHT,  keys, n)) CpsInp001[0] = 1;
    if (remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_A,      keys, n)) CpsInp001[4] = 1;   // Attack
    if (remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_B,      keys, n)) CpsInp001[5] = 1;   // Jump
    if (remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_C,      keys, n)) CpsInp001[6] = 1;   // Fire3
    if (remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_SELECT, keys, n)) { CpsInp018[0] = 1; CpsInp020[4] = 1; }   // Coin
    if (remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_START,  keys, n)) { CpsInp018[4] = 1; CpsInp020[0] = 1; }   // Start
    for (int i = 0; i < n; i++) {
        if (keys[i] == 30)       { CpsInp018[4] = 1; CpsInp020[0] = 1; }   // 1 = Start (классика MAME)
        else if (keys[i] == 34 || keys[i] == 91) { CpsInp018[0] = 1; CpsInp020[4] = 1; }  // 5 / Numpad5 = Coin
        else if (keys[i] == 20) CpsInp011[0] = 1;   // Q = Kick weak (CPS-2)
        else if (keys[i] == 8)  CpsInp011[1] = 1;   // E = Kick med
        else if (keys[i] == 21) CpsInp011[2] = 1;   // R = Kick strong
        // P2 — хардкод: WASD, J/K/L, 2=Start, 6=Coin
        else if (keys[i] == 26) CpsInp000[3] = 1;  // W up
        else if (keys[i] == 22) CpsInp000[2] = 1;  // S down
        else if (keys[i] == 4)  CpsInp000[1] = 1;  // A left
        else if (keys[i] == 7)  CpsInp000[0] = 1;  // D right
        else if (keys[i] == 13) CpsInp000[4] = 1;  // J Attack
        else if (keys[i] == 14) CpsInp000[5] = 1;  // K Jump
        else if (keys[i] == 15) CpsInp000[6] = 1;  // L Fire3
        else if (keys[i] == 31) { CpsInp018[5] = 1; CpsInp020[1] = 1; }  // 2 Start
        else if (keys[i] == 35) { CpsInp018[1] = 1; CpsInp020[5] = 1; }  // 6 Coin
    }

    // Sega-пад: НЕ МЕНЯТЬ прямое чтение — только sega_pad_scan() раз в кадр,
    // без фильтров/дебаунса на хосте. scan сам НЕ ощущается надёжным и уже
    // внутренне фильтрует фазы (тайминги эмпирические, sega_pad.c — отдельная
    // зона). Попытки фильтровать на хосте ломали ввод: usb_pad (3 скана)
    // глотал короткие X/Y/Z, «пересечение двух сканов» (r0.301) убивало пад
    // вовсе из-за кадрового интервала 16 мс.
    uint16_t sp = sega_pad_scan();
    if (sp & SP_UP)     CpsInp001[3] = 1;
    if (sp & SP_DOWN)   CpsInp001[2] = 1;
    if (sp & SP_LEFT)   CpsInp001[1] = 1;
    if (sp & SP_RIGHT)  CpsInp001[0] = 1;
    if (sp & SP_A)      CpsInp001[4] = 1;   // Attack
    if (sp & SP_B)      CpsInp001[5] = 1;   // Jump
    if (sp & SP_C)      CpsInp001[6] = 1;   // Fire3
    if (sp & SP_START)  { CpsInp018[4] = 1; CpsInp020[0] = 1; }   // Start
    if (sp & SP_X)      { CpsInp018[0] = 1; CpsInp020[4] = 1; }   // Coin
    if (sp & SP_Y)      CpsInp011[0] = 1;   // Kick weak (CPS-2)
    if (sp & SP_Z)      CpsInp011[1] = 1;   // Kick med
}

// снять суффикс .zip/.ZIP
static void strip_zip_ext(char* s)
{
    size_t l = strlen(s);
    if (l >= 4 && (strcasecmp(s + l - 4, ".zip") == 0)) s[l - 4] = 0;
}

// ---- точка входа из rom_browser (emu.h) ----
// rom/size НЕ используются: элемент браузера = папка игры или zip-файл,
// host сам читает ROM-сет по имени (rom_name).
// BurnLibInit/BurnGameListInit НЕ вызываются: они копируют короткие имена
// драйверов в malloc-блоки и переписывают pDriver[i]->szShortName на них;
// эти копии затирались после пары запусков игр (имена становились пустыми,
// BurnDrvGetIndex переставал находить драйверы). Вместо этого оставляем
// pDriver[i]->szShortName константными строками rodata и выставляем
// nBurnDrvCount = CPS_DRV_COUNT один раз.
static void run_cps(const char* root, const uint8_t* rom, uint32_t size, const char* rom_name)
{
    (void)rom; (void)size;
    emu_prepare();

    if (!rom_name || !rom_name[0]) {
        printf("CPS: no game name\n");
        return;
    }

    char game[FAT_NAME_LEN];
    int gl = 0;
    for (const char* s = rom_name; *s && gl < FAT_NAME_LEN - 1; s++) game[gl++] = *s;
    game[gl] = 0;
    strip_zip_ext(game);
    // FAT отдаёт имена регистром как на диске (KOD/3WONDERS), а BurnDrvGetIndex
    // сравнивает регистрозависимо — приводим к нижнему.
    for (char* s = game; *s; s++) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + 32);

    // путь ROM-источников (root = "/roms/cps1" или "/roms/cps2")
    g_dir[0] = 0; g_zip[0] = 0; g_zip_data = NULL; g_zip_size = 0;
    g_parent_dir[0] = 0; g_parent_zip[0] = 0; g_zip_parent_data = NULL; g_zip_parent_size = 0;
    snprintf(g_dir, sizeof(g_dir), "%s/%s", root, game);
    snprintf(g_zip, sizeof(g_zip), "%s/%s.zip", root, game);
    snprintf(g_bios_dir, sizeof(g_bios_dir), "%s/neogeo", root);
    snprintf(g_bios_zip, sizeof(g_bios_zip), "%s/neogeo.zip", root);

    static int g_lib_inited = 0;
    if (!g_lib_inited) {
        nBurnDrvCount = CPS_DRV_COUNT;
        g_lib_inited = 1;
    }

    int idx = BurnDrvGetIndex(game);
    if (idx < 0) {
        printf("CPS: no driver for '%s' (%d CPS drivers)\n", game, (int)nBurnDrvCount);
        fb_clear();
        fb_text_center("CPS-1: unknown game", 200, 2, 0x00FF4444);
        fb_text_center(game, 240, 2, 0x00FFFFFF);
        fb_flush();
        return;
    }
    nBurnDrvActive = (UINT32)idx;

    // прогресс-бар загрузки
    strncpy(g_load_name, game, sizeof(g_load_name) - 1);
    g_load_name[sizeof(g_load_name) - 1] = 0;
    s_prog_done = 0;
    s_prog_total = 0;

    // Родитель (для клонов): недостающие ROM ищем в папке/zip родителя
    {
        char* parent = BurnDrvGetTextA(DRV_PARENT);
        if (parent && parent[0] && strcmp(parent, game) != 0) {
            snprintf(g_parent_dir, sizeof(g_parent_dir), "%s/%s", root, parent);
            snprintf(g_parent_zip, sizeof(g_parent_zip), "%s/%s.zip", root, parent);
        }
    }

    // zip игры — целиком в ROM_BUF (браузер его больше не использует)
    char zipname[FAT_NAME_LEN];
    snprintf(zipname, sizeof(zipname), "%s.zip", game);
    fat_entry_t zf;
    if (fat_find(root, zipname, &zf) && zf.size > 0 && (uint32_t)zf.size <= ZIP_MAX) {
        load_progress("(zip)");   // чтение большого архива может идти секунды
        if (fat_read_file(&zf, 0, ZIP_BUF1, (uint32_t)zf.size) >= 0) {
            g_zip_data = ZIP_BUF1;
            g_zip_size = (uint32_t)zf.size;
        }
    }
    // zip родителя — во второй буфер (только если есть и отличается)
    if (g_parent_zip[0]) {
        snprintf(zipname, sizeof(zipname), "%s.zip", BurnDrvGetTextA(DRV_PARENT));
        fat_entry_t pf;
        if (fat_find(root, zipname, &pf) && pf.size > 0 && (uint32_t)pf.size <= ZIP_MAX2) {
            if (fat_read_file(&pf, 0, ZIP_BUF2, (uint32_t)pf.size) >= 0) {
                g_zip_parent_data = ZIP_BUF2;
                g_zip_parent_size = (uint32_t)pf.size;
            }
        }
    }

    // BIOS-zip (NEOGEO): neogeo.zip лежит в корне системы; грузим во второй
    // буфер, если он не занят родительским zip (у NEOGEO нет родительских клонов).
    if (!g_zip_parent_data && g_bios_zip[0]) {
        fat_entry_t bf;
        if (fat_find(root, "neogeo.zip", &bf) && bf.size > 0 && (uint32_t)bf.size <= ZIP_MAX2) {
            load_progress("(bios)");
            if (fat_read_file(&bf, 0, ZIP_BUF2, (uint32_t)bf.size) >= 0) {
                g_bios_zip_data = ZIP_BUF2;
                g_bios_zip_size = (uint32_t)bf.size;
            }
        }
    }

    if (check_romset(game)) {
        printf("CPS: %s incomplete, not starting\n", game);
        fb_clear();
        fb_text_center("CPS: ROM set incomplete", 210, 2, 0x00FF4444);
        fb_text_center(game, 250, 2, 0x00FFFFFF);
        fb_flush();
        return;
    }

    load_progress("");   // старт загрузки ROM (прогресс пойдёт по слотам)

    // кадровый буфер FBNeo
    pBurnDraw = (UINT8*)g_frame;
    nBurnPitch = CPS1_W * 2;
    nBurnBpp = 2;

    BurnExtLoadRom = host_ext_load_rom;
    BurnHighCol = host_high_col;   // иначе вендорный filler даёт белый экран

    if (BurnDrvInit() != 0) {
        printf("CPS: %s init failed (check ROM set)\n", game);
        // что пошло не так: список требуемых драйвером ROM (имена FBNeo)
        for (int r = 0; r < 64; r++) {
            struct BurnRomInfo rri;
            rri.nType = 0; rri.nLen = 0;
            BurnDrvGetRomInfo(&rri, (UINT32)r);
            if (rri.nType == 0 && rri.nLen == 0) break;
            if (rri.nLen == 0) continue;
            char* rn = NULL;
            BurnDrvGetRomName(&rn, (UINT32)r, 0);
            printf("CPS1 ROM %d: %s 0x%x\n", r, rn ? rn : "?", (unsigned)rri.nLen);
        }
        fb_clear();
        fb_text_center("CPS-1: load failed", 200, 2, 0x00FF4444);
        fb_text_center(game, 240, 2, 0x00FFFFFF);
        fb_flush();
        return;
    }
    printf("CPS: %s started (%s%s)\n", game,
           g_zip_data ? "zip" : "folder",
           g_zip_parent_data ? "+parent" : "");
    printf("CPS1 keys: P1 arrows+Z/X/C, 1/Enter=Start, S=Coin; P2 WASD+J/K/L, 2=Start, 6=Coin; pad=A/B/C, X=Coin; ESC=exit\n");

    emu_set_border_color(0x00000000);
    emu_throttle_reset();
    emu_esc_hold_reset();

    for (;;) {
        host_update_input();
        // DBG-TEMP r0.312: маркер кадра — если печатается, цикл жив
        { static unsigned fr = 0; if ((fr % 60) == 0) printf("FR %u\n", fr); fr++; }
        BurnDrvFrame();
        host_render_frame();
        fb_flush();
        emu_throttle();
        if (emu_esc_hold()) break;
    }

    BurnDrvExit();
    fb_clear(); fb_flush();
}

// ---- обёртки для rom_browser (emu.h) ----
void emu_run_cps1(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    run_cps("/roms/cps1", rom, size, rom_name);
}

void emu_run_cps2(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    run_cps("/roms/cps2", rom, size, rom_name);
}

void emu_run_neogeo(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    run_cps("/roms/neogeo", rom, size, rom_name);
}