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
// ROM-сет: /roms/cps1/<игра>/, /roms/cps2/<игра>/ или /roms/neogeo/<игра>/
// (папка с сырыми дампами чипов — приоритет)
//     или /roms/<cps1|cps2|neogeo>/<игра>.zip (распаковка через zlib из fuse/zlib).
// BIOS NEOGEO: /roms/neogeo/neogeo/ (папка) или /roms/neogeo/neogeo.zip.
// Игра идентифицируется по имени выбранного в браузере элемента (папки
// или zip-файла). В driverlist.h — 1491 драйвер (427 CPS-1 + 377 CPS-2 +
// 687 NEOGEO).
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
#include "btn_pad.h"
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

// NEOGEO/MVS: СВОИ регистры ввода (CpsInp* NEO не читает!):
//   NeoJoy1[0..3]=P1 Up/Down/Left/Right, [4..7]=P1 A/B/C/D; NeoJoy2 — P2.
//   NeoButton1[0/1]=P1 Start/Select, [2/3]=P2 Start/Select.
//   NeoButton2[0/1]=P1/P2 Coin, [2]=Service. NeoDiag[0]=Test.
extern UINT8 NeoJoy1[8];
extern UINT8 NeoJoy2[8];
extern UINT8 NeoButton1[32];
extern UINT8 NeoButton2[8];
extern UINT8 NeoDiag[2];

// ---- Видео ----
// CPS-1 рендерит 384×224 RGB565. Пишем напрямую в HDMI FB 1024×600
// (аспект 384/224 ≈ 1024/600) ближайшим соседом — без промежуточного
// EMU_FB (в него 384 колонки не влезают без потерь).
#define CPS1_W  384
#define CPS1_H  224
// Тоаплан рисует в буфер 320×240 (ToaClearScreen/ToaGetBitmap); вертикальные
// игры (240×320) при не-ротации могут выйти за 240 строк — буфер берём с
// запасом 320 строк, чтобы ядро не писал мимо.
#define TOA_W   320
#define TOA_H   240
#define FB_ADDR ((uint32_t*)0x5F900000u)
#define FB_W    1024
#define FB_H    600
static uint16_t g_frame[CPS1_W * 320];
static int g_frame_h = CPS1_H;   // высота активного кадра: 224 (CPS/NEO) или 320 (Toaplan вертик.)
static int g_frame_w = 0;        // ширина активного кадра: 0 = брать из nBurnPitch (питч 320)
static int g_rot = 0;            // 1 = вертикалку разворачиваем на 90° при выводе

// ---- ROM-источники (выбранная игра) ----
// zip-буферы: основной 0x50000000 (92 МБ, крупнейшие сеты NEOGEO ~84 МБ),
// BIOS/родителя 0x5C000000 (48 МБ). Между ними запас; выше до HDMI FB
// (~0x5F900000) ещё свободно.
#define ZIP_BUF1 ((uint8_t*)0x50000000u)
#define ZIP_BUF2 ((uint8_t*)0x5C000000u)
#define ZIP_MAX  (92u * 1024u * 1024u)   // крупнейшие сеты ~84 МБ (NEOGEO)
#define ZIP_MAX2 (48u * 1024u * 1024u)   // буфер родителя/BIOS ниже EMU_FB
static char g_dir[FAT_NAME_LEN + 16];   // "/roms/cps1/<game>"
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

// DBG-TEMP (r0.344-373) снят в r0.374 — диагностика Slap Fight велась только
// по UART; ROM-сет alcon по CRC совпадал. Вопрос отрисовки тайлмапа Slap Fight
// остаётся открытым отдельно.

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

// Сверка CRC дампа с драйвером (размеры могут совпасть у другой ревизии).
// crc32 — из zlib, который и так линкуется.
static void check_rom_crc(const char* name, const UINT8* buf, INT32 len, UINT32 want)
{
    if (!want || len <= 0) return;
    UINT32 crc = (UINT32)crc32(0, (const Bytef*)buf, (uInt)len);
    if (crc != want)
        printf("ROM CRC %s need %08x have %08x\n", name, (unsigned)want, (unsigned)crc);
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
            goto loaded;
        }
        if (g_bios_zip_data) {
            uint32_t got = 0;
            if (zip_extract(g_bios_zip_data, g_bios_zip_size, name, Dest, (uint32_t)ri.nLen, &got) == 0 && got > 0) {
                if (pnWrote) *pnWrote = (INT32)got;
                if (s_prog_total) { s_prog_done++; load_progress(""); }
                goto loaded;
            }
        }
    }
    if (load_from_dir(g_dir, name, Dest, ri.nLen, pnWrote) == 0) {
        if (s_prog_total) { s_prog_done++; load_progress(""); }
        goto loaded;
    }
    if (load_from_dir(g_parent_dir, name, Dest, ri.nLen, pnWrote) == 0) {
        if (s_prog_total) { s_prog_done++; load_progress(""); }
        goto loaded;
    }
    if (g_zip_data) {
        uint32_t got = 0;
        if (zip_extract(g_zip_data, g_zip_size, name, Dest, (uint32_t)ri.nLen, &got) == 0 && got > 0) {
            if (pnWrote) *pnWrote = (INT32)got;
            if (s_prog_total) { s_prog_done++; load_progress(""); }
            goto loaded;
        }
    }
    if (g_zip_parent_data) {
        uint32_t got = 0;
        if (zip_extract(g_zip_parent_data, g_zip_parent_size, name, Dest, (uint32_t)ri.nLen, &got) == 0 && got > 0) {
            if (pnWrote) *pnWrote = (INT32)got;
            if (s_prog_total) { s_prog_done++; load_progress(""); }
            goto loaded;
        }
    }

    printf("CPS: missing rom %s (index %d)\n", name, (int)i);
    if (pnWrote) *pnWrote = 0;
    return 1;

loaded:
    check_rom_crc(name, Dest, ri.nLen, (UINT32)ri.nCrc);
    return 0;
}

// ---- рендер кадра pBurnDraw (RGB565, активная ширина nBurnPitch/2) в HDMI FB ----
static void host_render_frame(void)
{
    const uint16_t* src = (const uint16_t*)pBurnDraw;
    if (!src) return;
    int cols = g_frame_w > 0 && g_frame_w <= CPS1_W ? g_frame_w : (nBurnPitch >> 1);
    if (cols <= 0 || cols > CPS1_W) cols = CPS1_W;
    int rows = g_frame_h;
    if (rows <= 0 || rows > 320) rows = CPS1_H;
    int sstride = nBurnPitch >> 1;
    if (sstride <= 0 || sstride > CPS1_W) sstride = CPS1_W;
    uint32_t* dst = FB_ADDR;

    // Кадр из драйвера всегда «альбомный» W×H: NEO 304×224, CPS 384×224,
    // Toaplan-гор 304×240. Для вертикальных Toaplan драйвер рисует сцену
    // «лежащей» (строки кадра = вертикаль сцены, колонки = горизонталь),
    // а g_rot ниже транспонирует её под портретную панель.
    int W = cols, H = rows;
    // Панель ПОРТРЕТНАЯ (600×1024 после физ. поворота). Вертикальную игру
    // выводим как на портретной панели: длинная ось (rows) идёт по вертикали,
    // короткая (cols) по горизонтали, равномерный масштаб, центр панели.
    if (g_rot) {
        const int FWp = FB_H;   // панельная ширина 600
        const int FHp = FB_W;   // панельная высота 1024
        int pw = (int)(((int64_t)cols * FHp) / rows);   // если бы вписывали по вертикали
        int ph = FHp;
        if (pw > FWp) { ph = (int)(((int64_t)rows * FWp) / cols); pw = FWp; }
        if (pw < 1) pw = 1;
        if (ph < 1) ph = 1;
        int px0 = (FWp - pw) / 2;      // по панельной горизонтали (600)
        int py0 = (FHp - ph) / 2;      // по панельной вертикали (1024)

        // закрашиваем весь буфер чёрным; поля по бокам остаются от заливки
        for (int y = 0; y < FB_H; y++) {
            uint32_t* drow = dst + (size_t)y * FB_W;
            for (int x = 0; x < FB_W; x++) drow[x] = 0;
        }

        // транспонированная запись: панель (px,py) -> буфер (x=py, y=px).
        // Шаги 16.16 считаются ОДИН раз на кадр (по правилу r0.360: без
        // делений/умножений int64 в пиксельном цикле). Порядок осей и область
        // чтения не менялись — только убраны ~480k div64 на кадр у вертикалок.
        uint32_t step_gy = ((uint32_t)cols << 16) / (uint32_t)pw;
        uint32_t step_gx = ((uint32_t)rows << 16) / (uint32_t)ph;
        uint32_t acc_gy = step_gy >> 1;
        for (int px = px0; px < px0 + pw; px++) {
            uint32_t gy = acc_gy >> 16;
            if (gy >= (uint32_t)cols) gy = cols - 1;
            uint32_t acc_gx = step_gx >> 1;
            uint32_t* drow = dst + (size_t)px * FB_W;
            for (int py = py0; py < py0 + ph; py++) {
                uint32_t gx = acc_gx >> 16;
                if (gx >= (uint32_t)rows) gx = rows - 1;
                uint16_t p = src[(size_t)gx * sstride + gy];
                uint32_t r = ((p >> 11) & 0x1F) << 3;
                uint32_t g = ((p >> 5) & 0x3F) << 2;
                uint32_t b = (p & 0x1F) << 3;
                drow[py] = (r << 16) | (g << 8) | b;
                acc_gx += step_gx;
            }
            acc_gy += step_gy;
        }
        return;
    }

    // Обычный (не-повёрнутый) кадр: размеры, центрирование, поля, таблица X
    int vw, vh;
    if ((int64_t)W * FB_H > (int64_t)H * FB_W) { vw = FB_W; vh = (int)(((int64_t)H * FB_W) / W); }
    else { vh = FB_H; vw = (int)(((int64_t)W * FB_H) / H); }
    if (vw < 1) vw = 1;
    if (vh < 1) vh = 1;
    int x0 = (FB_W - vw) / 2;
    int y0 = (FB_H - vh) / 2;
    for (int dy = 0; dy < FB_H; dy++) {
        uint32_t* drow = dst + (size_t)dy * FB_W;
        if (dy < y0 || dy >= y0 + vh) { for (int dx = 0; dx < FB_W; dx++) drow[dx] = 0; }
        else { for (int dx = 0; dx < x0; dx++) drow[dx] = 0;
               for (int dx = x0 + vw; dx < FB_W; dx++) drow[dx] = 0; }
    }
    static uint16_t sx[1024];
    uint32_t step_x = ((uint32_t)cols << 16) / (uint32_t)vw;
    uint32_t acc_x = step_x >> 1;
    for (int dx = 0; dx < vw; dx++) {
        uint32_t t = acc_x >> 16;
        if (t >= (uint32_t)cols) t = cols - 1;
        sx[dx] = (uint16_t)t;
        acc_x += step_x;
    }

    uint32_t step_y = ((uint32_t)rows << 16) / (uint32_t)vh;
    uint32_t y_acc = step_y >> 1;
    for (int dy = 0; dy < vh; dy++) {
        uint32_t sy = y_acc >> 16;
        if (sy >= (uint32_t)rows) sy = rows - 1;
        const uint16_t* srow = src + (size_t)sy * sstride;
        uint32_t* drow = dst + (size_t)(y0 + dy) * FB_W + x0;
        for (int dx = 0; dx < vw; dx++) {
            uint16_t p = srow[sx[dx]];
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5) & 0x3F) << 2;
            uint32_t b = (p & 0x1F) << 3;
            drow[dx] = (r << 16) | (g << 8) | b;
        }
        y_acc += step_y;
    }
}

// ---- ввод ----
// Логические кнопки P1/P2 — единый источник и для CPS-регистров (CpsInp*),
// и для NEOGEO-массивов (NeoJoy*/NeoButton*), и для Toaplan (по указателям
// из BurnDrvGetInputInfo: у toaplan-драйверов входные массивы static в файлах).
static int g_neo_input = 0;   // активен NEOGEO-драйвер (см. run_cps)
static int g_toa      = 0;    // активен Toaplan-драйвер
static int g_cave     = 0;    // активен Cave-драйвер (ранняя 68K-эра, r0.377)
static int g_sega     = 0;    // активен Sega System 16 (r0.383)

struct HostPad {
    unsigned up, down, left, right, a, b, c, d, start, select, kick1, kick2, kick3;
};

// Кэш указателей на входы Toaplan-драйвера (имена стандартные в FBNeo).
struct ToaPad {
    UINT8 *coin[2], *start[2];
    UINT8 *up[2], *down[2], *left[2], *right[2];
    UINT8 *b1[2], *b2[2], *b3[2];
};
static struct ToaPad g_toa_pad;
static void toa_input_cache(void) {
    memset(&g_toa_pad, 0, sizeof(g_toa_pad));
    for (UINT32 i = 0; i < 64; i++) {
        struct BurnInputInfo ii;
        memset(&ii, 0, sizeof(ii));
        BurnDrvGetInputInfo(&ii, i);
        if (!ii.szName || !ii.szName[0]) break;      // конец списка
        if ((ii.nType & 0x01) == 0) continue;        // BIT_DIGITAL
        UINT8* p = ii.pVal;
        if (!p) continue;
        UINT8 pl = (ii.szName[1] == '2') ? 1 : 0;    // "P1 "/"P2 "
        const char* n = ii.szName + 3;
        if      (!strcmp(n, "Up"))      g_toa_pad.up[pl] = p;
        else if (!strcmp(n, "Down"))    g_toa_pad.down[pl] = p;
        else if (!strcmp(n, "Left"))    g_toa_pad.left[pl] = p;
        else if (!strcmp(n, "Right"))   g_toa_pad.right[pl] = p;
        else if (!strcmp(n, "Button 1")) g_toa_pad.b1[pl] = p;
        else if (!strcmp(n, "Button 2")) g_toa_pad.b2[pl] = p;
        else if (!strcmp(n, "Button 3")) g_toa_pad.b3[pl] = p;
        else if (!strcmp(n, "Coin"))    g_toa_pad.coin[pl] = p;
        else if (!strcmp(n, "Start"))   g_toa_pad.start[pl] = p;
    }
}

static void toa_write_input(const HostPad* p1, const HostPad* p2)
{
    struct ToaPad* t = &g_toa_pad;
    const HostPad* p[2] = { p1, p2 };
    for (int pl = 0; pl < 2; pl++) {
        if (t->up[pl])    *t->up[pl]    = p[pl]->up    ? 1 : 0;
        if (t->down[pl])  *t->down[pl]  = p[pl]->down  ? 1 : 0;
        if (t->left[pl])  *t->left[pl]  = p[pl]->left  ? 1 : 0;
        if (t->right[pl]) *t->right[pl] = p[pl]->right ? 1 : 0;
        if (t->b1[pl])    *t->b1[pl]    = p[pl]->a     ? 1 : 0;
        if (t->b2[pl])    *t->b2[pl]    = p[pl]->b     ? 1 : 0;
        if (t->b3[pl])    *t->b3[pl]    = p[pl]->c     ? 1 : 0;
        if (t->coin[pl])  *t->coin[pl]  = p[pl]->select ? 1 : 0;
        if (t->start[pl]) *t->start[pl] = p[pl]->start ? 1 : 0;
    }
}

static void host_update_input(void)
{
    memset(CpsInp001, 0, sizeof(CpsInp001));
    memset(CpsInp000, 0, sizeof(CpsInp000));
    memset(CpsInp018, 0, sizeof(CpsInp018));
    memset(CpsInp011, 0, sizeof(CpsInp011));
    memset(CpsInp020, 0, sizeof(CpsInp020));
    memset(CpsInp021, 0, sizeof(CpsInp021));
    memset(CpsInp010, 0, sizeof(CpsInp010));
    if (g_neo_input) {
        memset(NeoJoy1, 0, 8); memset(NeoJoy2, 0, 8);
        memset(NeoButton1, 0, 32); memset(NeoButton2, 0, 8);
        NeoDiag[0] = 0;
    }

    // Клавиатура: usb_kbd_get_raw возвращает текущее УДЕРЖИВАЕМОЕ состояние.
    uint8_t keys[8];
    int n = usb_kbd_get_raw(keys, 8);

    HostPad p1 = {}, p2 = {};

    // P1 — ремап-платформа CPS-1
    p1.up     = remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_UP,     keys, n);
    p1.down   = remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_DOWN,   keys, n);
    p1.left   = remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_LEFT,   keys, n);
    p1.right  = remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_RIGHT,  keys, n);
    p1.a      = remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_A,      keys, n);
    p1.b      = remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_B,      keys, n);
    p1.c      = remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_C,      keys, n);
    p1.select = remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_SELECT, keys, n);   // Coin
    p1.start  = remap_kbd_pressed(REMAP_PLAT_CPS1, BTN_START,  keys, n);

    // Хардкод-клавиши (классика MAME) + P2 на WASD/JKL
    for (int i = 0; i < n; i++) {
        switch (keys[i]) {
            case 30:          p1.start  = 1; break;   // 1 = Start
            case 34: case 91: p1.select = 1; break;   // 5 / Numpad5 = Coin
            case 20:          p1.kick1  = 1; break;   // Q = Kick weak (CPS-2)
            case 8:           p1.kick2  = 1; break;   // E = Kick med
            case 21:          p1.kick3  = 1; break;   // R = Kick strong
            case 25:          p1.d      = 1; break;   // V = NEO Button D
            case 26: p2.up    = 1; break;             // W
            case 22: p2.down  = 1; break;             // S
            case 4:  p2.left  = 1; break;             // A
            case 7:  p2.right = 1; break;             // D
            case 13: p2.a     = 1; break;             // J
            case 14: p2.b     = 1; break;             // K
            case 15: p2.c     = 1; break;             // L
            case 31: p2.start = 1; break;             // 2 = Start
            case 35: p2.select = 1; break;            // 6 = Coin
        }
    }

    // Sega-пад: НЕ МЕНЯТЬ прямое чтение — только pad_scan_combined() раз в кадр,
    // без фильтров/дебаунса на хосте. scan сам НЕ ощущается надёжным и уже
    // внутренне фильтрует фазы (тайминги эмпирические, sega_pad.c — отдельная
    // зона). Попытки фильтровать на хосте ломали ввод: usb_pad (3 скана)
    // глотал короткие X/Y/Z, «пересечение двух сканов» (r0.301) убивало пад
    // вовсе из-за кадрового интервала 16 мс.
    uint16_t sp = pad_scan_combined();
    if (sp & SP_UP)    p1.up     = 1;
    if (sp & SP_DOWN)  p1.down   = 1;
    if (sp & SP_LEFT)  p1.left   = 1;
    if (sp & SP_RIGHT) p1.right  = 1;
    if (sp & SP_A)     p1.a      = 1;
    if (sp & SP_B)     p1.b      = 1;
    if (sp & SP_C)     p1.c      = 1;
    if (sp & SP_START) p1.start  = 1;
    if (sp & SP_X)     p1.select = 1;   // Coin
    if (sp & SP_Y)     p1.kick1  = 1;
    if (sp & SP_Z)     p1.kick2  = 1;

    // --- CPS-1/2 регистры ---
    if (p1.up)     CpsInp001[3] = 1;
    if (p1.down)   CpsInp001[2] = 1;
    if (p1.left)   CpsInp001[1] = 1;
    if (p1.right)  CpsInp001[0] = 1;
    if (p1.a)      CpsInp001[4] = 1;
    if (p1.b)      CpsInp001[5] = 1;
    if (p1.c)      CpsInp001[6] = 1;
    if (p1.select) { CpsInp018[0] = 1; CpsInp020[4] = 1; }   // Coin
    if (p1.start)  { CpsInp018[4] = 1; CpsInp020[0] = 1; }   // Start
    if (p1.kick1)  CpsInp011[0] = 1;
    if (p1.kick2)  CpsInp011[1] = 1;
    if (p1.kick3)  CpsInp011[2] = 1;
    if (p2.up)     CpsInp000[3] = 1;
    if (p2.down)   CpsInp000[2] = 1;
    if (p2.left)   CpsInp000[1] = 1;
    if (p2.right)  CpsInp000[0] = 1;
    if (p2.a)      CpsInp000[4] = 1;
    if (p2.b)      CpsInp000[5] = 1;
    if (p2.c)      CpsInp000[6] = 1;
    if (p2.start)  { CpsInp018[5] = 1; CpsInp020[1] = 1; }
    if (p2.select) { CpsInp018[1] = 1; CpsInp020[5] = 1; }

    // --- NEOGEO/MVS ---
    if (g_neo_input) {
        if (p1.up)     NeoJoy1[0] = 1;
        if (p1.down)   NeoJoy1[1] = 1;
        if (p1.left)   NeoJoy1[2] = 1;
        if (p1.right)  NeoJoy1[3] = 1;
        if (p1.a)      NeoJoy1[4] = 1;
        if (p1.b)      NeoJoy1[5] = 1;
        if (p1.c)      NeoJoy1[6] = 1;
        if (p1.d)      NeoJoy1[7] = 1;
        if (p1.start)  NeoButton1[0] = 1;
        if (p1.kick1)  NeoButton2[2] = 1;   // Service
        if (p1.select) NeoButton2[0] = 1;   // P1 Coin
        if (p2.up)     NeoJoy2[0] = 1;
        if (p2.down)   NeoJoy2[1] = 1;
        if (p2.left)   NeoJoy2[2] = 1;
        if (p2.right)  NeoJoy2[3] = 1;
        if (p2.a)      NeoJoy2[4] = 1;
        if (p2.b)      NeoJoy2[5] = 1;
        if (p2.c)      NeoJoy2[6] = 1;
        if (p2.start)  NeoButton1[2] = 1;
        if (p2.select) NeoButton2[1] = 1;   // P2 Coin
    }

    // --- Toaplan/Cave/Sega: пишем по указателям входа драйвера ---
    if (g_toa || g_cave || g_sega)
        toa_write_input(&p1, &p2);
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

    // путь ROM-источников (root = "/roms/cps1", "/roms/cps2" или "/roms/neogeo")
    g_dir[0] = 0; g_zip_data = NULL; g_zip_size = 0;
    g_parent_dir[0] = 0; g_parent_zip[0] = 0; g_zip_parent_data = NULL; g_zip_parent_size = 0;
    snprintf(g_dir, sizeof(g_dir), "%s/%s", root, game);
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
    // NEOGEO: ядро само адресует строки как nNeoScreenWidth (304/320, не
    // 384), поэтому nBurnPitch обязан совпасть с реальной шириной драйвера —
    // иначе строки съезжают по диагонали. CPS-драйверы как раз 384.
    // ВАЖНО: проверять по ТОЧНОМУ публичному коду, а не по `& HARDWARE_SNK_NEOGEO`:
    // HARDWARE_SNK_NEOGEO (0x05010000) пересекается по битам с
    // HARDWARE_CAPCOM_CPS2 (0x07010000) и CPS1 (0x01010000) — из-за этого
    // CPS-игры определялись как NEOGEO и брали чужой pitch/ширину
    // (dimahoo: 384-кадр читался как 304 → верх внизу, полосы).
    INT32 hw = BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK;
    g_neo_input = (hw == HARDWARE_SNK_NEOGEO || hw == HARDWARE_SNK_MVS ||
                   hw == HARDWARE_SNK_NEOCD  || hw == HARDWARE_SNK_DEDICATED_PCB) ? 1 : 0;
    g_toa = ((BurnDrvGetHardwareCode() & 0xFF000000) == HARDWARE_PREFIX_TOAPLAN) ? 1 : 0;
    g_cave = ((BurnDrvGetHardwareCode() & 0xFF000000) == HARDWARE_PREFIX_CAVE) ? 1 : 0;
    g_sega = ((BurnDrvGetHardwareCode() & 0xFF000000) == HARDWARE_PREFIX_SEGA) ? 1 : 0;
    if (g_neo_input) {
        // NEOGEO: ядро адресует строки как nNeoScreenWidth (304/320).
        extern INT32 nNeoScreenWidth;
        nBurnPitch = nNeoScreenWidth * 2;
        g_frame_h = CPS1_H;
        g_frame_w = nBurnPitch >> 1;
        g_rot = 0;
} else if (g_toa) {
        // Toaplan: выводим ТОЧНО то, что рисует ядро — как на родном железе.
        // Кадр всегда «альбомный» nw×nh: GP9001 и generic-платы рисуют буфер
        // 320×240/280×240 без программной ротации (bToaRotateScreen=false);
        // вертикальные игры ядро кладёт «боком» — так же, как в оригинальный
        // аркадный монитор ДО физического поворота оператором. Наша панель
        // стоит портретно ФИЗИЧЕСКИ, поэтому программное транспонирование НЕ
        // нужно: обычный contain-рендер кадра как есть + поворот панели даёт
        // ровную вертикальную/горизонтальную картинку (масштаб — по меньшей
        // стороне панели 600, центр, чёрные поля), ничего не режется.
        INT32 fw = 0, fh = 0, vw = 0, vh = 0;
        BurnDrvGetFullSize(&fw, &fh);
        BurnDrvGetVisibleSize(&vw, &vh);
        int vert = (BurnDrvGetFlags() & BDF_ORIENTATION_VERTICAL) != 0;
        int nw = (fw > 0 && fw <= CPS1_W) ? fw : TOA_W;
        int nh = (fh > 0 && fh <= 400)    ? fh : TOA_H;
        nBurnPitch = nw * 2;                   // stride копии BurnTransferCopy
        g_frame_w = nw;                        // полный кадр ядра: без кропа (не 304),
        g_frame_h = nh;                        // без свопов/транспонирования
        g_rot = 0;
        printf("TOA: game %s full=%dx%d visible=%dx%d vert=%d -> %dx%d pitch=%d rot=%d\n",
               game, (int)fw, (int)fh, (int)vw, (int)vh, vert,
               g_frame_w, g_frame_h, (int)nBurnPitch, g_rot);
    } else if (g_cave) {
        // Cave (ранняя 68K-эра): как Toaplan — кадр ядра как есть (без кропа/
        // транспонирования), вертикали ставит физический поворот панели.
        INT32 fw = 0, fh = 0;
        BurnDrvGetFullSize(&fw, &fh);
        int nw = (fw > 0 && fw <= CPS1_W) ? fw : TOA_W;
        int nh = (fh > 0 && fh <= 400)    ? fh : TOA_H;
        nBurnPitch = nw * 2;
        g_frame_w = nw;
        g_frame_h = nh;
        g_rot = 0;
        printf("CAV: game %s full=%dx%d -> %dx%d pitch=%d rot=%d\n",
               game, (int)fw, (int)fh, g_frame_w, g_frame_h, (int)nBurnPitch, g_rot);
    } else if (g_sega) {
        // Sega System 16 (r0.383): 68K+Z80, кадр как есть (rot=0); панель
        // физически портретная — вертикалки (скроллеры) встают сами.
        INT32 fw = 0, fh = 0;
        BurnDrvGetFullSize(&fw, &fh);
        int nw = (fw > 0 && fw <= CPS1_W) ? fw : TOA_W;
        int nh = (fh > 0 && fh <= 400)    ? fh : TOA_H;
        nBurnPitch = nw * 2;
        g_frame_w = nw;
        g_frame_h = nh;
        g_rot = 0;
        printf("S16: game %s full=%dx%d -> %dx%d pitch=%d rot=%d\n",
               game, (int)fw, (int)fh, g_frame_w, g_frame_h, (int)nBurnPitch, g_rot);
    } else {
        nBurnPitch = CPS1_W * 2;
        g_frame_h = CPS1_H;
        g_frame_w = CPS1_W;
        g_rot = 0;
    }
    // Кадровый буфер хоста (static BSS) переживает выход из эмулятора —
    // без очистки при повторном входе виден мусор предыдущей игры.
    memset(g_frame, 0, sizeof(g_frame));
    if (g_toa || g_cave || g_sega)
        toa_input_cache();   // входные массивы драйвера — static в его файле
    printf("CPS: %s frame %dx%d pitch %d\n", game,
           (int)(nBurnPitch >> 1), g_frame_h, (int)nBurnPitch);

    printf("CPS: %s started (%s%s)\n", game,
           g_zip_data ? "zip" : "folder",
           g_zip_parent_data ? "+parent" : "");
    // r0.385: манифест звуковых ядер сборки (звук НЕ задействован — только
    // инвентаризация чипов для дальнейшего послойного подключения).
    printf("SND %s cores: ym2612 ym2413 ym2151 ym2203 ym3812 ymz280b msm5205 msm6295 rf5c68 segapcm dac upd7759 (ay8910=ports, ym2610=stub, sound OFF)\n",
           game);
    // Отладочная строка для проверки ядер: платформа (корень) + короткое имя
    // рома + название системы драйвера из FBNeo.
    printf("EMU: SYS=%s ROM=%s HW=%s\n", root + 1, game,
           BurnDrvGetTextA(DRV_SYSTEM));
    if (g_neo_input) {
        printf("NEO keys: P1 arrows+Z/X/C(A/B/C) V=D, 1/Enter=Start, S/5=Coin; P2 WASD+J/K/L, 2=Start, 6=Coin; pad A/B/C, X=Coin; ESC=exit\n");
    } else if (g_toa) {
        printf("TOA keys: P1 arrows+Z/X/C, 1=Start, 5=Coin; P2 WASD+J/K/L, 2=Start, 6=Coin; ESC x3=exit\n");
    } else if (g_cave) {
        printf("CAV keys: P1 arrows+Z/X/C, 1=Start, 5=Coin; P2 WASD+J/K/L, 2=Start, 6=Coin; ESC x3=exit\n");
    } else if (g_sega) {
        printf("S16 keys: P1 arrows+Z/X/C, 1=Start, 5=Coin; P2 WASD+J/K/L, 2=Start, 6=Coin; ESC x3=exit\n");
    } else {
        printf("CPS1 keys: P1 arrows+Z/X/C, 1/Enter=Start, S=Coin; P2 WASD+J/K/L, 2=Start, 6=Coin; pad=A/B/C, X=Coin; ESC=exit\n");
    }

    emu_set_border_color(0x00000000);
    emu_throttle_reset();
    emu_esc_hold_reset();

for (;;) {
        host_update_input();
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

void emu_run_toaplan(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    run_cps("/roms/toaplan", rom, size, rom_name);
}

// Cave (68K, ранняя эра) — отдельный корень /roms/cave (r0.383).
void emu_run_cave(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    run_cps("/roms/cave", rom, size, rom_name);
}

// Sega System 16 — корень /roms/segasys (r0.383, V2 семейств).
void emu_run_segasys(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    run_cps("/roms/segasys", rom, size, rom_name);
}

void emu_run_fbneo(const uint8_t* rom, uint32_t size, const char* rom_name)
{
    // Общий корень FBNeo: для любой игры, чей драйвер есть в driverlist
    // (CPS-1/2, NEOGEO, Toaplan ныне; другие наборы довиваются позже).
    run_cps("/roms/fbneo", rom, size, rom_name);
}