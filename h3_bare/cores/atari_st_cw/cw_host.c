/* cw_host.c — host-слой Castaway для H3 (r1566)
 *
 * Ядро Castaway (cpu/, st/) не знает о железе. Здесь:
 *   • диски .st/.msa через FAT (cw_disk_*), вместо fopen/fread;
 *   • кадр 320x200 (4 bitplane) -> XRGB8888 -> DE2 GSU (800x600);
 *   • цикл кадра: CPURun -> IOTimer (200 Гц) -> render/VBlank (50 Гц).
 *
 * Ввод (USB -> IKBD) и звук (YM2149) подключаются отдельными модулями:
 * этот файл их не содержит и заглушек не оставляет.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "68000.h"
#include "st.h"
#include "cw_host.h"

#include "fat.h"
#include "h3_de2.h"
#include "h3_hs_timer.h"
#include "../emulog.h"

/* r1578: DE2 читает кадр без кэша — после CPU-записи обязателен clean. */
extern void usb_cache_clean(uint32_t addr, uint32_t size);
#include "sound/cw_sound.h"
#include "cw_input.h"

#ifdef CW_DEBUG
#include "uart.h"
#define CWLOG(s) uart_puts(s)
#else
#define CWLOG(s) do { } while (0)
#endif

/* Выход из эмулятора: ESC (USB) с удержанием, общий для всех ядер. */
extern int  emu_esc_hold(void);
extern void emu_esc_hold_reset(void);

/* init.c ссылается на этот символ (режим «instructions per second»).
 * 0 = без ограничения: темп задаёт наш цикл кадров. */
unsigned ips = 0;

/* ======================= диски через FAT ======================= */

#define CW_DISKS 2

static fat_entry_t   g_de[CW_DISKS];
static fat_stream_t  g_ds[CW_DISKS];
static int           g_dok[CW_DISKS];

/* r1587: счётчики для лога — сколько секторов ядро реально запросило. */
static uint32_t      g_cw_reads;
static uint32_t      g_cw_last_off;

static int cw_split(const char *path, char *dir, unsigned dirlen,
                    char *name, unsigned namelen)
{
    const char *slash = strrchr(path, '/');
    if (!slash)
        return -1;
    unsigned dl = (unsigned)(slash - path);
    if (dl == 0) {
        if (dirlen < 2) return -1;
        dir[0] = '/'; dir[1] = 0;
    } else {
        if (dl + 1 > dirlen) return -1;
        memcpy(dir, path, dl);
        dir[dl] = 0;
    }
    unsigned nl = (unsigned)strlen(slash + 1);
    if (nl + 1 > namelen) return -1;
    memcpy(name, slash + 1, nl + 1);
    return 0;
}

int cw_disk_open(int idx, const char *path)
{
    char dir[256], name[128];

    if (idx < 0 || idx >= CW_DISKS || !path || !path[0])
        return -1;
    g_dok[idx] = 0;
    if (cw_split(path, dir, sizeof(dir), name, sizeof(name)) != 0) {
        emulog_field("ST", "diskerr", "split '%s' failed", path);
        return -1;
    }
    if (!fat_find(dir, name, &g_de[idx])) {
        emulog_field("ST", "diskerr", "find '%s' in '%s' failed", name, dir);
        return -1;
    }
    fat_stream_init(&g_ds[idx], &g_de[idx]);
    g_dok[idx] = 1;
    return 0;
}

int cw_disk_size(int idx)
{
    if (idx < 0 || idx >= CW_DISKS || !g_dok[idx])
        return -1;
    return (int)g_de[idx].size;
}

int cw_disk_read(int idx, uint32_t off, void *buf, uint32_t len)
{
    if (idx < 0 || idx >= CW_DISKS || !g_dok[idx])
        return -1;
    g_cw_reads++;
    g_cw_last_off = off;
    return fat_stream_read(&g_ds[idx], off, (uint8_t *)buf, len);
}

int cw_disk_write(int idx, uint32_t off, const void *buf, uint32_t len)
{
    /* Образы монтируются только для чтения: запись на SD не поддерживаем. */
    (void)idx; (void)off; (void)buf; (void)len;
    return -1;
}

/* ======================= кадр ======================= */

#define CW_MAX_W 640     /* r1593: mid-res 640x200 (COL2) — самая широкая */
#define CW_H 200
#define CW_OUT_H 600

/* r1574: кадр — в .noinit_fbn (0x54000000), как у всех ядер: DE2 читает
 * буфер без кэша, .bss-копия давала пропуски строк и скачки по горизонтали.
 * r1594: строки упакованы плотно по активной ширине (320 low / 640 mid),
 * иначе DE2 с PITCH=INSIZE читает половину строки. */
static uint32_t g_fb[CW_MAX_W * CW_H]
    __attribute__((aligned(64), section(".noinit_fbn")));

static uint32_t cw_palette[16];

static void cw_palette_update(void)
{
    for (int i = 0; i < 16; i++) {
        uint16_t c = vid_col[i];            /* ST: 0000 0RRR 0GGG 0BBB */
        uint32_t r = (c >> 8) & 7u;
        uint32_t g = (c >> 4) & 7u;
        uint32_t b = c & 7u;
        r = (r * 255u + 3u) / 7u;
        g = (g * 255u + 3u) / 7u;
        b = (b * 255u + 3u) / 7u;
        cw_palette[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

/* ST low res: 320x200, 4 bitplane. Строка — 160 байт: 20 блоков по 8 байт
 * (4 слова, plane0..plane3), в каждом блоке 16 пикселей, старший бит — левый. */
static void cw_render_col4(const uint8_t *v)
{
    for (int y = 0; y < CW_H; y++) {
        const uint8_t *src = v + (unsigned)y * 160u;
        uint32_t *dst = &g_fb[(unsigned)y * 320u];   /* r1594: плотно 320 */
        for (int blk = 0; blk < 20; blk++) {
            const uint8_t *p = src + (unsigned)blk * 8u;
            uint16_t p0 = (uint16_t)((p[0] << 8) | p[1]);
            uint16_t p1 = (uint16_t)((p[2] << 8) | p[3]);
            uint16_t p2 = (uint16_t)((p[4] << 8) | p[5]);
            uint16_t p3 = (uint16_t)((p[6] << 8) | p[7]);
            for (int i = 0; i < 16; i++) {
                unsigned bit = 15u - (unsigned)i;
                unsigned ci = ((p0 >> bit) & 1u) |
                              (((p1 >> bit) & 1u) << 1) |
                              (((p2 >> bit) & 1u) << 2) |
                              (((p3 >> bit) & 1u) << 3);
                dst[(unsigned)blk * 16u + (unsigned)i] = cw_palette[ci];
            }
        }
    }
}

/* ST mid res: 640x200, 2 bitplane. Строка — 160 байт: 40 блоков по 4 байта
 * (plane0..plane1), в каждом блоке 16 пикселей, старший бит — левый. */
static void cw_render_col2(const uint8_t *v)
{
    for (int y = 0; y < CW_H; y++) {
        const uint8_t *src = v + (unsigned)y * 160u;
        uint32_t *dst = &g_fb[(unsigned)y * 640u];   /* r1594: плотно 640 */
        for (int blk = 0; blk < 40; blk++) {
            const uint8_t *p = src + (unsigned)blk * 4u;
            uint16_t p0 = (uint16_t)((p[0] << 8) | p[1]);
            uint16_t p1 = (uint16_t)((p[2] << 8) | p[3]);
            for (int i = 0; i < 16; i++) {
                unsigned bit = 15u - (unsigned)i;
                unsigned ci = ((p0 >> bit) & 1u) | (((p1 >> bit) & 1u) << 1);
                dst[(unsigned)blk * 16u + (unsigned)i] = cw_palette[ci];
            }
        }
    }
}

/* ST mono: 640x400, 1 plane; на выходе берём каждую вторую строку. */
static void cw_render_mono(const uint8_t *v)
{
    uint32_t fg = cw_palette[0], bg = cw_palette[1];
    for (int y = 0; y < CW_H; y++) {
        const uint8_t *src = v + (unsigned)y * 2u * 80u;
        uint32_t *dst = &g_fb[(unsigned)y * 640u];   /* r1594: плотно 640 */
        for (int x = 0; x < CW_MAX_W; x++) {
            unsigned byte = (unsigned)x >> 3;
            unsigned bit = 7u - ((unsigned)x & 7u);
            dst[x] = ((src[byte] >> bit) & 1u) ? fg : bg;
        }
    }
}

/* r1573: база экрана в 68k-адресах (24-bit: baseh<<16 | basem<<8). */
static uint32_t cw_vid_base(void)
{
    return ((uint32_t)vid_baseh << 16) | ((uint32_t)vid_basem << 8);
}

/* r1580: слой DE2 настраиваем один раз на режим: повторный scaler_rect
 * каждый кадр перезапускал GSU — картинка дрожала вправо/влево.
 * r1593: режим ST (low/mid/mono) может смениться — тогда перекалибровка. */
static int g_cw_gsu_ready;
static int g_cw_gsu_w;

static void cw_render(void)
{
    uint32_t base = cw_vid_base();
    unsigned src_w;

    cw_palette_update();
    switch (vid_shiftmode) {
    case 1:                          /* mid: 640x200, 2 bitplane */
        cw_render_col2((const uint8_t *)membase + base);
        src_w = 640;
        break;
    case 2:                          /* mono: 640x400, 1 bitplane */
        cw_render_mono((const uint8_t *)membase + base);
        src_w = 640;
        break;
    default:                         /* low: 320x200, 4 bitplane */
        cw_render_col4((const uint8_t *)membase + base);
        src_w = 320;
        break;
    }

    /* r1578/r1593: пропорции 4:3 по пикселю и центр по 1024, как у Agat;
     * слой настраиваем один раз на режим. Кадр после записи чистим. */
    int dw = ((int)src_w * CW_OUT_H) / CW_H;
    if (dw > 1024)
        dw = 1024;
    int dx = (1024 - dw) / 2;
    if (g_cw_gsu_w != (int)src_w)
        g_cw_gsu_ready = 0;
    usb_cache_clean((uint32_t)(uintptr_t)g_fb,
                    (uint32_t)((size_t)src_w * CW_H * 4u));
    if (!g_cw_gsu_ready) {
        if (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)g_fb, src_w, CW_H, 32,
                                  (uint32_t)dx, 0, (uint32_t)dw, CW_OUT_H) == 0) {
            g_cw_gsu_ready = 1;
            g_cw_gsu_w = (int)src_w;
        }
    }
}

/* ======================= цикл ======================= */

#define CW_FRAME_US   20000u    /* 50 Гц, PAL */
#define CW_IOTIMER_US  5000u    /* 200 Гц: MFP/IKBD таймер ядра */

/* nf.c (NatFeats) отдаёт эту строку по NF_VERSION; в оригинале жила в main.c SDL. */
char version[] = "MultiTool Castaway";

/* r1567: точка входа диспетчера меню (emu.h). Образ диска — rom_name;
 * вшитый EmuTOS берёт st/mem.c, поэтому rom/size не нужны. */
void emu_run_atari_st_cw(const uint8_t *rom, uint32_t size, const char *rom_name)
{
    (void)rom;
    (void)size;
    CWLOG("cw: enter\n");
    if (rom_name && rom_name[0]) {
        CWLOG("cw: disk ");
        CWLOG(rom_name);
        CWLOG("\n");
    }
    cw_run(rom_name, size);
}

void cw_run(const char *disk_path, uint32_t img_size)
{
    uint32_t t_start = h3_hs_timer_lo_us();

    emulog_begin("ST");
    emulog_field("ST", "rom", "%s, %u B",
                 disk_path ? disk_path : "(none)", (unsigned)img_size);
    /* r1593: Pasti .stx (трековый формат) не поддерживается — назад в меню. */
    if (disk_path && strstr(disk_path, ".stx")) {
        emulog_field("ST", "disk", "unsupported .stx (Pasti)");
        emulog_end("ST");
        return;
    }

    if (disk_path && disk_path[0]) {
        strncpy(disk[0].name, disk_path, sizeof(disk[0].name) - 1);
        disk[0].name[sizeof(disk[0].name) - 1] = 0;
    }

    /* r1579: умолчание st.c — MONO, а CommandlineInit (-4) не вызывается:
     * без этой строки рендер уходил в mono (ч/б, полосы, двоение). */
    display_mode = COL4;
    Init(0, NULL);              /* MemInit/FDCInit/IOInit/nf_init/HWReset */
    CWLOG("cw: init ok\n");
    {
        int dsz = cw_disk_size(0);
        if (dsz >= 0)
            emulog_field("ST", "disk", "A: %d B, %d trk x %d sec x %d side",
                         dsz, disk[0].tracks, disk[0].sectors, disk[0].sides);
        else
            emulog_field("ST", "disk", "A: none");
    }
    emulog_field("ST", "machine", "68000 PAL 50 Hz, DRAM %u MB",
                 (unsigned)(memsize >> 20));
    emulog_field("ST", "video", "COL4 320x200 -> 960x600 (DE2 GSU)");
    emulog_field("ST", "audio", "YM2149 44100 Hz, 882 pairs/frame");
    emulog_field("ST", "init", "OK");
#ifdef CW_DEBUG
    uart_puts("cw: pc="); uart_print_hex((uint32_t)pc);
    uart_puts(" sp="); uart_print_hex((uint32_t)ssp);
    uart_puts(" mem="); uart_print_hex((uint32_t)memsize);
    uart_puts(" memb="); uart_print_hex((uint32_t)(uintptr_t)membase);
    uart_puts("\n");
    uart_puts("cw: ram0="); uart_print_hex(*(volatile uint32_t *)(membase + 0));
    uart_puts(" ram4="); uart_print_hex(*(volatile uint32_t *)(membase + 4));
    uart_puts("\n");
#endif
    cw_sound_reset();
    CWLOG("cw: snd ok\n");
    cw_input_init();
    g_cw_gsu_ready = 0;          /* r1580: слой меню сброшен — настроить заново */
    emu_esc_hold_reset();
    CWLOG("cw: loop\n");

    uint32_t t_io = h3_hs_timer_lo_us();
    uint32_t t_vbl = t_io;
    uint32_t frames = 0;
#ifdef CW_DEBUG
    unsigned dbg_once = 1;
    unsigned stop_log = 0;
#endif

    while (!emu_esc_hold()) {
        unsigned n = CPURun(2000);
#ifdef CW_DEBUG
        if (dbg_once) {                  /* первый выход из CPURun */
            dbg_once = 0;
            uart_puts("cw: n="); uart_print_uint(n);
            uart_puts(" pc="); uart_print_hex((uint32_t)pc);
            uart_puts("\n");
        }
#endif
        if (!n) {                        /* STOP — ожидание прерывания, не выход */
#ifdef CW_DEBUG
            if (stop_log++ == 0)
                CWLOG("cw: cpu stop (ждём прерывание)\n");
#endif
        }

        uint32_t now = h3_hs_timer_lo_us();
        if ((uint32_t)(now - t_io) >= CW_IOTIMER_US) {
            t_io += CW_IOTIMER_US;
            IOTimer();
            cw_input_poll();        /* 200 Гц: USB -> IKBD press/release */
        }
        if ((uint32_t)(now - t_vbl) >= CW_FRAME_US) {
            t_vbl += CW_FRAME_US;
            VBlank();
            /* r1573: рендер ПОСЛЕ VBlank — TOS закончил кадр, база стабильна. */
            cw_render();
            cw_sound_render();          /* r1566: кадр звука -> I2S */
#ifdef CW_DEBUG
            /* r1570: раз в 64 кадра — где CPU и куда лезет вне карты памяти. */
            if ((++frames & 0x3F) == 0) {
                uart_puts("cw: t pc="); uart_print_hex((uint32_t)pc);
                uart_puts(" u="); uart_print_uint((unsigned)cw_unmapped_cnt);
                uart_puts(" a="); uart_print_hex(cw_unmapped_addr);
                uart_puts(" b="); uart_print_hex(cw_vid_base());
                uart_puts("\n");
            }
#endif
        }
        IkbdWriteBuffer();
        SetInterruptPriority();
    }
    CWLOG("cw: exit\n");
    /* r1575: вернуть DE2 слой на буфер меню (иначе экран держит кадр ST)
     * и бордюр обратно чёрный, как у Agat/32X. */
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);

        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    emulog_field("ST", "diskio", "%u reads, last off 0x%X",
                 (unsigned)g_cw_reads, (unsigned)g_cw_last_off);
    emulog_frames("ST", frames, h3_hs_timer_lo_us() - t_start);
    emulog_end("ST");
}
