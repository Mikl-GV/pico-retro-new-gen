// gp_cheats.c — чит-инжин Genesis Plus GX (GG/SMS/MD)
// Расшифровка Game Genie / Action Replay кодов и патч ROM/RAM.

// Форматы кодов (из эталонного GPGX gx/gui/cheats.c):
//   GG 8-bit:  DDA-AAA-XXX  (SMS/GG) — data[7:0], addr[15:0], ref[7:0] (XOR/rotate)
//   AR 8-bit:  xxAAAA:DD    (SMS/GG) — RAM only
//   GG 16-bit: ABCD-EFGH    (MD)     — data[15:0], addr[23:0]
//   AR 16-bit: AAAAAA:DDDD  (MD)     — addr[23:0] + data[15:0]

#include <stdint.h>
#include <string.h>
#include "cheatdb.h"

extern int printf(const char* fmt, ...);

// ---- globals ядра (реальные символы: memz80.c / genesis.c) ----
extern unsigned char *z80_readmap[64];
extern uint8_t work_ram[0x10000];

// ROM, на который ставим патчи — передаёт host (system_gpgx_h3.c), чтобы
// не тянуть сюда структурy external_t/ext из ядра
static uint8_t *gp_rom = NULL;
static uint32_t gp_rom_size = 0;

void gp_cheats_set_rom(uint8_t* rom, uint32_t size) {
    gp_rom = rom;
    gp_rom_size = size;
}

// ---- распатченные записи (текущая сессия) ----
#define GP_MAX_PATCHES 64
typedef struct {
    uint32_t addr;
    uint16_t data;
    uint16_t old;
    int  enabled;
    int  is_rom;       // 1 = ROM-патч (8/16-bit GG или 16-bit AR в ROM), 0 = RAM
    uint8_t *prev;     // указатель на запатченную байтовую ячейку (для 8-bit GG)
} gp_patch_t;

static gp_patch_t g_patches[GP_MAX_PATCHES];
static int g_patch_count = 0;
static int g_is_md = 0;

static const char gg_alphabet[] = "ABCDEFGHJKLMNPRSTVWXYZ0123456789";
static const char ar_alphabet[] = "0123456789ABCDEF";

// ---- декодеры ----
// DDA-AAA-XXX (SMS/GG 8-bit)
static int decode_gg8(const char* code, uint32_t* addr, uint16_t* data, uint8_t* compare) {
    if (!code || strlen(code) < 11) return 0;
    if (code[3] != '-' || code[7] != '-') return 0;
    uint16_t a = 0, d = 0, r = 0;
    int j = 0;
    for (int i = 0; i < 11; i++) {
        if (i == 3 || i == 7) continue;
        const char* p = strchr(ar_alphabet, code[i]);
        if (!p) return 0;
        uint8_t n = (uint8_t)(p - ar_alphabet) & 0xF;
        if (j < 2)       d = (d << 4) | n;
        else if (j < 5)  a = (a << 4) | n;
        else if (j < 6)  a = (a << 4) | (n ^ 0xF);
        else             r = (r << 4) | n;
        j++;
    }
    r = ((r >> 2) | ((r & 3) << 6)) ^ 0xBA;
    *addr = a;
    *data = (uint16_t)d;
    *compare = (uint8_t)r;
    return 1;
}

// xxAAAA:DD (SMS/GG AR)
static int decode_ar8(const char* code, uint32_t* addr, uint16_t* data) {
    if (!code || strlen(code) < 9) return 0;
    if (code[6] != ':') return 0;
    uint32_t a = 0;
    for (int i = 0; i < 6; i++) {
        const char* p = strchr(ar_alphabet, code[i]);
        if (!p) return 0;
        a = (a << 4) | (uint8_t)(p - ar_alphabet);
    }
    uint16_t d = 0;
    for (int i = 7; i < 9; i++) {
        const char* p = strchr(ar_alphabet, code[i]);
        if (!p) return 0;
        d = (d << 4) | (uint8_t)(p - ar_alphabet);
    }
    *addr = a;
    *data = d;
    return 1;
}

// ABCD-EFGH (MD GG 16-bit)
static int decode_gg16(const char* code, uint32_t* addr, uint16_t* data) {
    if (!code || strlen(code) < 9) return 0;
    if (code[4] != '-') return 0;
    uint32_t a = 0;
    uint16_t d = 0;
    int j = 0;
    for (int i = 0; i < 9; i++) {
        if (i == 4) continue;
        const char* p = strchr(gg_alphabet, code[i]);
        if (!p) return 0;
        int n = (int)(p - gg_alphabet);
        switch (j) {
        case 0: d |= n << 3; break;
        case 1: d |= n >> 2; a |= (n & 3) << 14; break;
        case 2: a |= n << 9; break;
        case 3: a |= (n & 0xF) << 20 | (n >> 4) << 8; break;
        case 4: d |= (n & 1) << 12; a |= (n >> 1) << 16; break;
        case 5: d |= (n & 1) << 15 | (n >> 1) << 8; break;
        case 6: d |= (n >> 3) << 13; a |= (n & 7) << 5; break;
        case 7: a |= n; break;
        }
        j++;
    }
    *addr = a;
    *data = d;
    return 1;
}

// AAAAAA:DDDD (MD AR 16-bit)
static int decode_ar16(const char* code, uint32_t* addr, uint16_t* data) {
    if (!code || strlen(code) < 11) return 0;
    if (code[6] != ':') return 0;
    uint32_t a = 0;
    for (int i = 0; i < 6; i++) {
        const char* p = strchr(ar_alphabet, code[i]);
        if (!p) return 0;
        a = (a << 4) | (uint8_t)(p - ar_alphabet);
    }
    uint16_t d = 0;
    for (int i = 7; i < 11; i++) {
        const char* p = strchr(ar_alphabet, code[i]);
        if (!p) return 0;
        d = (d << 4) | (uint8_t)(p - ar_alphabet);
    }
    *addr = a;
    *data = d;
    return 1;
}

// Публичный декодер: 1=GG8(ROM) 2=AR8(RAM) 3=GG16(MD ROM) 4=AR16(MD RAM) 0=не распознан
int gp_cheat_decode(const char* code, uint32_t* addr, uint16_t* data, uint8_t* compare) {
    *compare = 0;
    if (!code || !code[0]) return 0;
    if (decode_gg8(code, addr, data, compare)) return 1;
    if (decode_ar8(code, addr, data)) return 2;
    if (decode_gg16(code, addr, data)) return 3;
    if (decode_ar16(code, addr, data)) return 4;
    return 0;
}

// ---- сборка списка патчей из отмеченных читов ----
void gp_cheats_compile(int is_md) {
    g_is_md = is_md;
    g_patch_count = 0;
    int cnt = cheats_count();
    for (int i = 0; i < cnt && g_patch_count < GP_MAX_PATCHES; i++) {
        const cheat_t* ch = cheats_get(i);
        if (!ch || !ch->enabled) continue;
        uint32_t addr;
        uint16_t data;
        uint8_t cmp;
        int type = gp_cheat_decode(ch->code, &addr, &data, &cmp);
        if (!type) continue;   // не наш формат — не спамим в UART

        gp_patch_t* p = &g_patches[g_patch_count];
        memset(p, 0, sizeof(*p));
        p->addr = addr;
        p->data = data;
        p->enabled = 1;

        switch (type) {
        case 1: // GG 8-bit: ROM (патч через z80_readmap банк)
            p->is_rom = 1;
            // old = compare — значение, которое должно быть в ROM на этом адресе
            p->old = cmp;
            break;
        case 2: // AR 8-bit: RAM
            p->is_rom = 0;
            break;
        case 3: // GG 16-bit: MD ROM (прямая запись слова)
            p->is_rom = 1;
            break;
        case 4: // AR 16-bit: MD RAM
            p->is_rom = 0;
            break;
        }
        g_patch_count++;
    }
}

// ---- применение ROM-патча (банкованного) ----
// Вызывается при загрузке и при каждой смене банка (ROMCheatUpdate)
static void apply_rom_cheat(gp_patch_t* p) {
    if (g_is_md) {
        // MD: ROM в gp_rom, побайтовая 16-bit запись (без unaligned STRH).
        if (p->addr < gp_rom_size) {
            uint32_t a = p->addr & ~1u;
            p->old = (uint16_t)((gp_rom[a] << 8) | gp_rom[a + 1]);
            gp_rom[a]     = (uint8_t)(p->data >> 8);
            gp_rom[a + 1] = (uint8_t)p->data;
        }
        return;
    }

    // SMS/GG: банкованный z80_readmap, адрес в пространстве Z80 (0x0000-0x7FFF)
    // GG-код даёт адрес как есть; если он >= 0x8000 — не ROM-область Z80
    if (p->addr < 0x8000) {
        uint8_t* ptr = &z80_readmap[p->addr >> 10][p->addr & 0x3FF];
        // проверяем compare (old) против текущего содержимого банка
        if (p->old == *ptr || !p->old) {
            *ptr = (uint8_t)p->data;
            p->prev = ptr;
        }
    }
}

// ---- снятие ROM-патча (восстановление) ----
static void unapply_rom_cheat(gp_patch_t* p) {
    if (g_is_md) {
        if (p->addr < gp_rom_size) {
            uint32_t a = p->addr & ~1u;
            gp_rom[a]     = (uint8_t)(p->old >> 8);
            gp_rom[a + 1] = (uint8_t)p->old;
        }
        return;
    }
    if (p->prev) {
        *p->prev = (uint8_t)p->old;
        p->prev = NULL;
    }
}

// ---- clean D-cache для запатченных ROM-областей ----
// После записи патчей в DRAM (ROM_BUF) dirty-линии write-back D-cache
// могут не дойти до DRAM, и 68k/VDP DMA прочитают старые данные. Для MD
// чистим по адресу патча; для SMS/GG — по указателю p->prev (текущий банк).
static void cheats_cache_clean(void) {
    for (int i = 0; i < g_patch_count; i++) {
        gp_patch_t* p = &g_patches[i];
        if (!p->enabled || !p->is_rom) continue;
        uint32_t a;
        if (g_is_md) {
            a = p->addr & ~0x1Fu;
        } else {
            if (!p->prev) continue;
            a = (uint32_t)p->prev & ~0x1Fu;
        }
        uint32_t e = a + 64;
        for (; a < e; a += 32)
            __asm volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(a));
    }
    __asm volatile("dsb" ::: "memory");
}

// ---- применение RAM-патчей (раз в кадр) ----
static void apply_ram_cheats(void) {
    for (int i = 0; i < g_patch_count; i++) {
        gp_patch_t* p = &g_patches[i];
        if (!p->enabled || p->is_rom) continue;
        // AR 8-bit (SMS/GG): адрес в Work RAM 0xC000-0xFFFF -> 0xFF0000|(a&0x1FFF)
        // AR 16-bit (MD): адрес 24-бит, work_ram 0xFF0000 (64KB).
        // ВНИМАНИЕ: адреса MD < 0xFF0000 — это ROM/векторы, в RAM их писать нельзя.
        if (g_is_md) {
            if (p->addr >= 0xFF0000 && p->addr < 0xFF0000 + 0x10000) {
                uint32_t a = p->addr & 0xFFFF;
                work_ram[a] = (uint8_t)p->data;
            }
        } else {
            if (p->addr >= 0xC000) {
                uint32_t a = p->addr & 0x1FFF;
                work_ram[a] = (uint8_t)p->data;
            }
        }
    }
}

// ---- публичный API ----
// применить все отмеченные читы (вызывается после init игры)
void gp_cheats_apply(void) {
    for (int i = 0; i < g_patch_count; i++) {
        gp_patch_t* p = &g_patches[i];
        if (!p->enabled) continue;
        if (p->is_rom) apply_rom_cheat(p);
    }
    // ROM-патчи пишут в DRAM (cart.rom / ROM_BUF), а D-cache write-back
    // может не сбросить изменения → 68k DMA читает старые данные → игра
    // зависает. Принудительный clean D-cache для запятнанных адресов.
    cheats_cache_clean();

    apply_ram_cheats();
}

// снять все читы (при выходе из игры)
void gp_cheats_clear(void) {
    for (int i = 0; i < g_patch_count; i++) {
        gp_patch_t* p = &g_patches[i];
        if (!p->enabled) continue;
        if (p->is_rom) unapply_rom_cheat(p);
    }
    g_patch_count = 0;
}

// ---- хуки ядра (CHEATS_UPDATE при смене банка / RAM раз в кадр) ----
void ROMCheatUpdate(void) {
    // переустановить ROM-патчи после смены банка
    if (!g_is_md) {
        for (int i = 0; i < g_patch_count; i++) {
            gp_patch_t* p = &g_patches[i];
            if (p->enabled && p->is_rom) {
                unapply_rom_cheat(p);
                apply_rom_cheat(p);
            }
        }
        // после смены банка страницы переставлены: clean D-cache по новым
        // адресам p->prev, иначе Z80/68k читают старые (кэшированные) данные
        cheats_cache_clean();
    }
}

void RAMCheatUpdate(void) {
    apply_ram_cheats();
}