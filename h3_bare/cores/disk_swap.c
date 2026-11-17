/*
    r1363: единый hot-swap дискет по F12 для компьютерных эмуляторов.

    Хост регистрирует emu_disk_ops_t (число приводов, допустимые расширения,
    callback вставки) и вызывает emu_disk_poll() раз в кадр ПЕРЕД чтением
    клавиатуры. F12 открывает список образов текущего каталога; выбранный
    файл читается в общий буфер и передаётся callback'у, который подменяет
    диск в ядре без сброса машины. F12 намеренно не отдаётся ни одному
    эмулятору, пока hot-swap активен; после закрытия оверлея клавиша
    дожидается отпускания, чтобы не «доехать» в игру.

    Буфер один на все системы: 2 МБ в NOLOAD-секции .swap_buf. Для машин с
    несколькими приводами он режется на слоты по числу приводов (2 привода →
    по 1 МБ), иначе второй привод затирал образ первого: fdd1 и Frodo держат
    ленивый поток на этом буфере и читают его во время эмуляции.
*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "emu.h"
#include "emulog.h"
#include "fat.h"
#include "fb_text.h"
#include "usb_kbd.h"
#include "h3.h"

#define SWAP_MAX_BYTES (2u * 1024u * 1024u)
#define SWAP_ROW_H     20
#define SWAP_ROWS      20

/* r1363: буфер — в собственной NOLOAD-секции 0x4A420000 (linker.ld), иначе
   2 МБ в .bss перекрывают ZIP_BUF_BIG аркад (0x44C00000). */
static uint8_t  g_swap_buf[SWAP_MAX_BYTES] __attribute__((section(".swap_buf")));
static char     g_dir[FAT_NAME_LEN + 32];
static const emu_disk_ops_t *g_ops;
static uint8_t  g_prev_f12;

void emu_disk_set_dir(const char *dir)
{
    if (!dir) { g_dir[0] = 0; return; }
    size_t n = strlen(dir);
    if (n >= sizeof(g_dir)) n = sizeof(g_dir) - 1;
    memcpy(g_dir, dir, n);
    g_dir[n] = 0;
}

void emu_disk_register(const emu_disk_ops_t *ops)
{
    g_ops = ops;
    g_prev_f12 = 0;
}

/* Расширение файла против списка "dsk;do;po;..." (без точек). */
static int ext_ok(const char *name)
{
    if (!g_ops || !g_ops->exts || !g_ops->exts[0]) return 1;
    const char *dot = strrchr(name, '.');
    if (!dot || !dot[1]) return 0;
    const char *p = g_ops->exts;
    while (*p) {
        const char *e = p;
        while (*p && *p != ';') p++;
        size_t n = (size_t)(p - e);
        const char *d = dot + 1;
        size_t m = strlen(d);
        if (n == m) {
            size_t i;
            int same = 1;
            for (i = 0; i < n; i++) {
                char ca = e[i], cb = d[i];
                if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
                if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
                if (ca != cb) { same = 0; break; }
            }
            if (same) return 1;
        }
        if (*p == ';') p++;
    }
    return 0;
}

static void sort_list(fat_entry_t *list, int n)
{
    int i, j;

    for (i = 0; i < n - 1; i++) {
        for (j = i + 1; j < n; j++) {
            if (strcmp(list[i].name, list[j].name) > 0) {
                fat_entry_t t = list[i];
                list[i] = list[j];
                list[j] = t;
            }
        }
    }
}

static int wait_key(void)
{
    for (;;) {
        int k = usb_input_poll();
        if (k) return k;
        udelay(16000);
    }
}

static void msg_wait(const char *line)
{
    fb_clear();
    fb_puts_s(60, 240, line, 2, 0x00FFAA00);
    fb_flush();
    udelay(600000);
}

static int pick_drive(void)
{
    int sel = 0;

    if (!g_ops || g_ops->drives <= 1) return 0;
    for (;;) {
        int i;

        fb_clear();
        fb_puts_s(60, 40, "DISK SWAP (F12)", 2, 0x00FFAA00);
        fb_puts_s(60, 80, "Select drive", 1, 0x00FFFFFF);
        for (i = 0; i < g_ops->drives; i++) {
            int y = 120 + i * 30;
            char b[32];
            snprintf(b, sizeof(b), "Drive %d", i + 1);
            if (i == sel) fb_fill_rect(50, y - 4, 300, 26, 0x00181818);
            fb_puts_s(80, y, b, 2, i == sel ? 0x00FFFF00 : 0x00FFFFFF);
        }
        fb_puts(60, 560, "  Up/Down : select    Enter : next    ESC : cancel",
                0x00888888);
        fb_flush();

        int k = wait_key();
        if (k == 41) return -1;
        if (k == 82) { if (sel > 0) sel--; }
        else if (k == 81) { if (sel < g_ops->drives - 1) sel++; }
        else if (k == 40) return sel;
    }
}

static int swap_run(void)
{
    fat_entry_t *all = fat_scratch();
    fat_entry_t *list = all;
    int n = fat_list(g_dir, all, FAT_MAX_ENTRIES);
    int w = 0, i;
    int cursor = 0, scroll = 0;
    int drive;

    if (n <= 0) {
        msg_wait("Folder is empty");
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (all[i].attr & 0x10) continue;
        if (all[i].size == 0) continue;
        if (!ext_ok(all[i].name)) continue;
        list[w++] = all[i];
    }
    n = w;
    sort_list(list, n);
    if (n <= 0) {
        msg_wait("No disk images in folder");
        return 0;
    }

    drive = pick_drive();
    if (drive < 0) return 0;

    for (;;) {
        int rows = n < SWAP_ROWS ? n : SWAP_ROWS;
        int y = 110;

        fb_clear();
        fb_puts_s(60, 30, "DISK SWAP (F12)", 2, 0x00FFAA00);
        {
            char hdr[160];
            snprintf(hdr, sizeof(hdr), "Drive %d   %s", drive + 1, g_dir);
            fb_puts_s(60, 65, hdr, 1, 0x00AAAAAA);
        }
        fb_fill_rect(60, 88, 900, 2, 0x00FFFFFF);

        for (i = 0; i < rows; i++) {
            int idx = scroll + i;
            int sel = (idx == cursor);
            char buf[70];
            int len = (int)strlen(list[idx].name);

            if (len > 62) len = 62;
            memcpy(buf, list[idx].name, (size_t)len);
            buf[len] = 0;
            if (sel) fb_fill_rect(50, y - 3, 930, SWAP_ROW_H, 0x00181818);
            fb_puts_s(80, y, buf, 1, sel ? 0x00FFFF00 : 0x00FFFFFF);
            y += SWAP_ROW_H;
        }

        {
            char pg[48];
            snprintf(pg, sizeof(pg), "  %d/%d    Enter : insert    ESC : cancel",
                     cursor + 1, n);
            fb_puts(60, 560, pg, 0x00888888);
        }
        fb_flush();

        int k = wait_key();
        if (k == 41) return 0;
        if (k == 82) {
            if (n > 1) cursor = (cursor == 0) ? n - 1 : cursor - 1;
        } else if (k == 81) {
            if (n > 1) cursor = (cursor >= n - 1) ? 0 : cursor + 1;
        } else if (k == 40) {
            char name[FAT_NAME_LEN];
            char path[sizeof(g_dir) + FAT_NAME_LEN + 2];
            fat_entry_t f;
            int sl = (int)strlen(list[cursor].name);
            int pl;

            if (sl >= FAT_NAME_LEN) sl = FAT_NAME_LEN - 1;
            memcpy(name, list[cursor].name, (size_t)sl);
            name[sl] = 0;

            pl = (int)strlen(g_dir);
            if (pl && g_dir[pl - 1] == '/') pl--;
            if (pl + 1 + sl >= (int)sizeof(path)) {
                msg_wait("Path too long");
                return 0;
            }
            memcpy(path, g_dir, (size_t)pl);
            path[pl] = '/';
            memcpy(path + pl + 1, name, (size_t)sl);
            path[pl + 1 + sl] = 0;

            if (!fat_find(g_dir, name, &f)) {
                msg_wait("File not found");
                return 0;
            }
            if (g_ops->in_memory) {
                /* r1364: буфер режется по числу приводов — у Apple II и C64
                   два привода, и общий буфер затирал образ первого привода. */
                uint32_t cap = SWAP_MAX_BYTES /
                               (uint32_t)(g_ops->drives > 0 ? g_ops->drives : 1);
                uint8_t *dst = g_swap_buf + (size_t)drive * cap;

                if (f.size == 0 || f.size > cap) {
                    msg_wait("Image too large for drive slot");
                    return 0;
                }
                if (fat_read_file(&f, 0, dst, f.size) != (int)f.size) {
                    msg_wait("Read failed");
                    return 0;
                }
                if (g_ops->insert(drive, path, dst, f.size, name) == 0) {
                    emulog_field("DISK", "swap", "insert drv%d '%s' %u B",
                                 drive, name, (unsigned)f.size);
                    msg_wait(name);
                    return 1;
                }
                msg_wait("Insert failed");
                return 0;
            }
            if (g_ops->insert(drive, path, 0, 0, name) == 0) {
                emulog_field("DISK", "swap", "insert drv%d '%s' (mem)",
                             drive, name);
                msg_wait(name);
                return 1;
            }
            msg_wait("Insert failed");
            return 0;
        }
        if (cursor < scroll) scroll = cursor;
        if (cursor >= scroll + rows) scroll = cursor - rows + 1;
    }
}

/*
    Вызывается хостом раз в кадр ДО чтения клавиатуры. Если F12 нажата и
    hot-swap зарегистрирован — открывает оверлей. После оверлея ждёт
    отпускания клавиш, чтобы F12/стрелки не попали в машину.

    НЕ МЕНЯТЬ: порядок «сначала emu_disk_poll() ПЕРЕД чтением клавиатуры
    ядра» обязателен. Если перенести poll после ввода эмулятора — F12 уйдёт
    в гостевую машину как обычная клавиша и hot-swap перестанет работать
    (проверено r1363). emu_throttle_reset() после оверлея тоже обязателен —
    без него кадры ускоряются вперёд (r1364).
*/
int emu_disk_poll(void)
{
    uint8_t keys[8];
    int n, i;
    int f12 = 0;

    if (!g_ops || !g_ops->insert || !g_dir[0]) return 0;

    n = usb_kbd_get_raw(keys, 8);
    for (i = 0; i < n; i++) if (keys[i] == 0x45) f12 = 1;

    if (f12 && !g_prev_f12) {
        int r;
        g_prev_f12 = 1;
        r = swap_run();
        usb_kbd_wait_release();
        /* r1364: пауза оверлея не должна копиться в throttle. Без сброса
           emu_ts0 остаётся в прошлом, elapsed >> period → кадры идут без
           ожидания, пока база не догонит реальное время (fast-forward). */
        emu_throttle_reset();
        return r;
    }
    g_prev_f12 = (uint8_t)f12;
    return 0;
}
