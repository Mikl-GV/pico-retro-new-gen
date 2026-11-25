/* cw_host.c — host-слой Castaway для H3 (r1566)
 *
 * Ядро Castaway (cpu/, st/) не знает о железе. Здесь:
 *   • диски .st через FAT (cw_disk_*), вместо fopen/fread;
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
#include "../emu.h"      /* r1602: emu_disk_* — hot-swap диска A: по F12 */
#include "../fb_text.h"  /* r1609: fb_clear/fb_flush на выходе */

/* r1578: DE2 читает кадр без кэша — после CPU-записи обязателен clean. */
extern void usb_cache_clean(uint32_t addr, uint32_t size);
/* r1600: регистр состояния 68k (op68k.h) — маска IPL и режим. */
extern uint16 GetSRW(void);
/* r1603: снимок IKBD-мыши и счётчики ввода (st/ikbd.c, cw_input.c). */
extern int      cw_ikbd_mode, cw_ikbd_pause, cw_ikbd_x, cw_ikbd_stx, cw_ikbd_th;
extern int      cw_ikbd_joymode, cw_ikbd_joy;
extern unsigned cw_ikbd_bytes, cw_ikbd_last, cw_ikbd_keys, cw_ikbd_mpack;
extern unsigned cw_in_motion;
extern int      cw_in_odx, cw_in_ody;
extern unsigned cw_in_pad, cw_in_raw;
#include "sound/cw_sound.h"
#include "cw_input.h"
#include "../usb_kbd.h"      /* r1602: F12 читаем прямой опрос отчёта */

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

/* r1600: редкие события 68k в единый лог сессии.
 * Бюджет: первые 8 строк, затем каждая 256-я (аварии) / 8192-я (IRQ) —
 * шторм прерываний не должен вытеснить из лога первую аварию. */
void cw_dbg_evt(const char *what, unsigned a, unsigned b, unsigned c)
{
    static uint32_t n;
    if (n < 8 || (n & 0xFF) == 0)
        emulog_field("ST", "evt", "%s addr=0x%06X pc=0x%06X sr=%04X n=%u",
                     what, a, b, c, (unsigned)n);
    n++;
}

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
        /* r1606: B: по умолчанию не подключён ('diskb') — это норма,
         * в лог пишем только настоящую ошибку диска A:. */
        if (idx == 0)
            emulog_field("ST", "diskerr", "split '%s' failed", path);
        return -1;
    }
    if (!fat_find(dir, name, &g_de[idx])) {
        if (idx == 0)
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

/* r1602: F12 -> hot-swap диска A: общим disk_swap.c (r1363). Сброса машины
 * нет: подменяем образ и перечитываем BPB/геометрию через FDCInit(). */
static int cw_disk_insert(int drive, const char *path, const uint8_t *data,
                          uint32_t size, const char *name)
{
    (void)data; (void)size; (void)name;

    if (drive != 0 || !path || !path[0])
        return -1;

    strncpy(disk[0].name, path, sizeof(disk[0].name) - 1);
    disk[0].name[sizeof(disk[0].name) - 1] = 0;
    FDCInit();                      /* открыть образ по имени + BPB */
    if (cw_disk_size(0) < 0)
        return -1;
    emulog_field("ST", "disk", "A: %d B, %d trk x %d sec x %d side (swap)",
                 cw_disk_size(0), disk[0].tracks, disk[0].sectors, disk[0].sides);
    return 0;
}

static const emu_disk_ops_t g_cw_disk_ops = {
    /* r1609: только .st — .msa требует распаковки (file.c не в сборке),
     * .stx (Pasti) трековый и не поддерживается вовсе. */
    1, "st", 0, cw_disk_insert
};

/* ======================= кадр ======================= */

#define CW_MAX_W 640     /* r1593: mid-res 640x200 (COL2) — самая широкая */
#define CW_H 200
#define CW_OUT_H 600

/* r1574: кадр — в .noinit_fbn (0x54000000), как у всех ядер: DE2 читает
 * буфер без кэша, .bss-копия давала пропуски строк и скачки по горизонтали.
 * r1594: строки упакованы плотно по активной ширине (320 low / 640 mid),
 * иначе DE2 с PITCH=INSIZE читает половину строки. */
static uint32_t g_fb[CW_MAX_W * CW_H]
    __attribute__((aligned(64), section(".rgb_hi")));

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
    static int render_mode;       /* фактический режим рендера (0/1/2) */
    static int pend_mode = -1, pend_cnt;

    /* r1596: одиночные переключения регистра (монитор-детект TOS) не должны
     * рвать кадр: смену режима применяем, только если держится 2 кадра. */
    int m = (vid_shiftmode == 1) ? 1 : (vid_shiftmode == 2) ? 2 : 0;
    if (m == render_mode) {
        pend_mode = -1;
    } else if (m == pend_mode) {
        if (++pend_cnt >= 2) {
            render_mode = m;
            pend_mode = -1;
        }
    } else {
        pend_mode = m;
        pend_cnt = 0;
    }

    cw_palette_update();
    switch (render_mode) {
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
    /* r1595: событие смены режима/ширины. r1603: базу не сравниваем —
     * игры переключают её каждый кадр (двойная буферизация), лог тонул. */
    {
        static int last_mode = -1;
        static unsigned last_w;
        if (render_mode != last_mode || src_w != last_w) {
            last_mode = render_mode;
            last_w = src_w;
            emulog_field("ST", "mode", "%s %ux%u base=0x%05X -> %dx%d @%d",
                         render_mode == 1 ? "COL2" :
                         render_mode == 2 ? "MONO" : "COL4",
                         src_w, CW_H, base, dw, CW_OUT_H, dx);
        }
    }
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

    /* r1609: общая подготовка входа (bump/newlib heap, EMU_FB) — без неё
     * повторный запуск malloc'ил ещё 4 МБ поверх прошлой сессии. */
    emu_prepare();
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

    /* r1602: список F12-свапа — каталог текущего образа. */
    {
        char d[256], f[128];

        if (disk_path && cw_split(disk_path, d, sizeof(d), f, sizeof(f)) == 0)
            emu_disk_set_dir(d);
        emu_disk_register(&g_cw_disk_ops);
    }

    /* r1579: умолчание st.c — MONO, а CommandlineInit (-4) не вызывается:
     * без этой строки рендер уходил в mono (ч/б, полосы, двоение). */
    display_mode = COL4;
    Init(0, NULL);              /* MemInit/FDCInit/IOInit/nf_init/HWReset */
    cw_ikbd_reset();            /* r1609: IKBD/мышь/джойстик — с чистого листа */
    g_cw_reads = 0;             /* r1609: счётчики сессии */
    g_cw_last_off = 0;
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
#ifdef CW_DEBUG
    uint32_t t_stat = t_io;
#endif
    uint32_t frames = 0;
#ifdef CW_DEBUG
    unsigned dbg_once = 1;
    unsigned stop_log = 0;
#endif

    while (!emu_esc_hold()) {
#ifdef CW_DEBUG
        /* r1606: срез раз в секунду — только в отладочной сборке. В релизе
         * остаются значимые события: evt/irq/mode/disk/diskio. */
        uint32_t now0 = h3_hs_timer_lo_us();
        if ((uint32_t)(now0 - t_stat) >= 1000000u) {
            t_stat += 1000000u;
            emulog_field("ST", "stat",
                         "pc=0x%05X ip=%u sr=%04X st=%d reads=%u f=%u base=0x%05X"
                         " ipb=%02X ierb=%02X v45=%X v46=%X v47=%X v4D=%X",
                         (unsigned)pc, (unsigned)intpri,
                         (unsigned)GetSRW(), (int)cpu_state,
                         (unsigned)g_cw_reads, (unsigned)frames,
                         cw_vid_base(),
                         (unsigned)mfp_iprb, (unsigned)mfp_ierb,
                         (unsigned)GetMemL(0x114), (unsigned)GetMemL(0x118),
                         (unsigned)GetMemL(0x11C), (unsigned)GetMemL(0x134));
            /* r1603: состояние ввода — что доходит до IKBD и что отдаём. */
            emulog_field("ST", "inp",
                         "mode=%d pause=%d x=%d stx=%d th=%d keys=%u bytes=%u"
                         " mp=%u last=%02X mov=%u odx=%d ody=%d pad=%u raw=%u"
                         " jm=%d js=%02X",
                         cw_ikbd_mode, cw_ikbd_pause, cw_ikbd_x, cw_ikbd_stx,
                         cw_ikbd_th, cw_ikbd_keys, cw_ikbd_bytes,
                         cw_ikbd_mpack, cw_ikbd_last,
                         cw_in_motion, cw_in_odx, cw_in_ody,
                         cw_in_pad, cw_in_raw,
                         cw_ikbd_joymode, cw_ikbd_joy);
        }
#endif
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
            /* r1602: F12 — свап диска A:. Оверлей disk_swap рисует в буфер
             * меню, поэтому на фронт F12 слой DE2 возвращается меню; после
             * оверлея g_cw_gsu_ready=0 заставит cw_render вернуть слой ST. */
            {
                static uint8_t prev_f12;
                uint8_t keys[8];
                int nk = usb_kbd_get_raw(keys, sizeof(keys));
                uint8_t f12 = 0;

                for (int i = 0; i < nk; i++)
                    if (keys[i] == 0x45)        /* HID F12 */
                        f12 = 1;
                if (f12 && !prev_f12) {
                    extern void h3_de2_ui_restore(void);
                    h3_de2_ui_restore();
                }
                emu_disk_poll();                /* сам откроет оверлей по F12 */
                if (f12 && !prev_f12)
                    g_cw_gsu_ready = 0;
                prev_f12 = f12;
            }
            VBlank();
            /* r1573: рендер ПОСЛЕ VBlank — TOS закончил кадр, база стабильна. */
            cw_render();
            cw_sound_render();          /* r1566: кадр звука -> I2S */
            frames++;                   /* r1601: счётчик и в релизе (fps в exit) */
#ifdef CW_DEBUG
            /* r1570: раз в 64 кадра — где CPU и куда лезет вне карты памяти. */
            if ((frames & 0x3F) == 0) {
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
    emu_disk_register(0);       /* r1602: снять F12-свап с этого ядра */
    /* r1575: вернуть DE2 слой на буфер меню (иначе экран держит кадр ST)
     * и бордюр обратно чёрный, как у Agat/32X. */
    {
        extern void h3_de2_ui_restore(void);
        extern void h3_de2_set_bkcolor(uint32_t argb);

        h3_de2_ui_restore();
        h3_de2_set_bkcolor(0xFF000000u);
    }
    /* r1609: остановить звук (кольцо/CPU2) и отдать меню чистый кадр. */
    emu_audio_stop();
    fb_clear();
    fb_flush();
    emulog_field("ST", "diskio", "%u reads, last off 0x%X",
                 (unsigned)g_cw_reads, (unsigned)g_cw_last_off);
    emulog_frames("ST", frames, h3_hs_timer_lo_us() - t_start);
    emulog_end("ST");
}
