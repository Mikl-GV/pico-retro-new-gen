#include <stdint.h>
#include <string.h>
#include "fb_text.h"
#include "systems.h"
#include "fat.h"
#include "uart.h"
#include "usb_kbd.h"

#define PHYS_W 1024
#define PHYS_H 600

#define ROW_H    18
#define TITLE_Y  30
#define SEP_Y    62
#define LIST_TOP 74
#define FOOTER_Y (PHYS_H - 30)
#define INDENT   40

#define MAX_MENU_ITEMS 64

typedef struct {
    const char *id;
    const char *name;
    const char *dir;
    int group;
    int status;
    int present;  // 1 = real dir found on card
} menu_item_t;

static menu_item_t g_items[MAX_MENU_ITEMS];
static int g_item_count = 0;

// регистронезависимое сравнение
static int namencmp(const char* a, const char* b, int n) {
    while (n > 0 && *a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 1;
        a++; b++; n--;
    }
    if (n == 0) return 0;
    if (*a == *b) return 0;
    return 1;
}

// найти систему в реестре по имени папки (регистронезависимо)
static int find_system(const char* dir_name) {
    for (int i = 0; i < (int)NUM_SYSTEMS; i++) {
        const char* d = system_rom_dir(i);
        if (namencmp(dir_name, d, 128) == 0) return i;
        if (systems[i].alt_dir && namencmp(dir_name, systems[i].alt_dir, 128) == 0) return i;
    }
    return -1;
}

static void build_menu(void) {
    g_item_count = 0;
    fat_entry_t dirs[FAT_MAX_ENTRIES];
    int n = fat_list("/roms", dirs, FAT_MAX_ENTRIES);
    if (n <= 0) return;

    for (int i = 0; i < n && i < FAT_MAX_ENTRIES; i++) {
        // Только папки (size == 0)
        if (dirs[i].size != 0) continue;
        // Пропускаем . и ..
        const char* dn = dirs[i].name;
        if (dn[0] == '.' && (dn[1] == 0 || (dn[1] == '.' && dn[2] == 0)))
            continue;

        int sys_idx = find_system(dn);
        if (sys_idx >= 0) {
            // дубль: эта система уже добавлена (напр. nes + nes_roms) — пропускаем
            int dup = 0;
            for (int k = 0; k < g_item_count; k++) {
                if (strcmp(g_items[k].id, systems[sys_idx].id) == 0) { dup = 1; break; }
            }
            if (dup) continue;

            g_items[g_item_count].id = systems[sys_idx].id;
            g_items[g_item_count].name = systems[sys_idx].name;
            // dir = реально найденная папка на карте (может быть alt_dir)
            g_items[g_item_count].dir = dn;
            g_items[g_item_count].group = systems[sys_idx].group;
            g_items[g_item_count].status = systems[sys_idx].status;
            g_items[g_item_count].present = 1;
        } else {
            g_items[g_item_count].id = dn;
            g_items[g_item_count].name = dn;
            g_items[g_item_count].dir = dn;
            g_items[g_item_count].group = GROUP_OTHER;
            g_items[g_item_count].status = STATUS_PLANNED;
            g_items[g_item_count].present = 1;
        }
        g_item_count++;
    }
}

static int group_sort_key(int g) {
    switch (g) {
        case GROUP_PORTABLE: return 0;
        case GROUP_CONSOLE:  return 1;
        case GROUP_ARCADE:   return 2;
        case GROUP_COMPUTER: return 3;
        default:             return 4;
    }
}

// сортировка по группам (Portable → Console → Computer → Other), внутри по имени
static void sort_items(void) {
    for (int i = 0; i < g_item_count - 1; i++)
        for (int j = i + 1; j < g_item_count; j++) {
            int ki = group_sort_key(g_items[i].group);
            int kj = group_sort_key(g_items[j].group);
            if (ki > kj || (ki == kj && strcmp(g_items[i].name, g_items[j].name) > 0)) {
                menu_item_t t = g_items[i]; g_items[i] = g_items[j]; g_items[j] = t;
            }
        }
}

#define MAX_ROWS (GROUP_COUNT + MAX_MENU_ITEMS + 2)

typedef struct { int item; int group; } row_t;
static row_t rows[MAX_ROWS];
static int row_count;

static void build_rows(void) {
    row_count = 0;
    int cur_group = -1;
    for (int i = 0; i < g_item_count; i++) {
        if (g_items[i].group != cur_group) {
            cur_group = g_items[i].group;
            rows[row_count].item = -1;
            rows[row_count].group = cur_group;
            row_count++;
        }
        rows[row_count].item = i;
        rows[row_count].group = cur_group;
        row_count++;
    }
    // разделитель и пункт Settings
    rows[row_count].item = -1;
    rows[row_count].group = -1;
    row_count++;
    rows[row_count].item = -2;   // settings
    rows[row_count].group = -1;
    row_count++;
}

static int sel_row = 0;
static int scroll_top = 0;

static void render_menu(void) {
    fb_draw_stars();
    fb_puts_s(60, TITLE_Y, "MultiTool Retro", 2, 0x00FF0000);
    fb_fill_rect(60, SEP_Y, 200, 2, 0x00FFFFFF);

    int max_visible = (FOOTER_Y - LIST_TOP - 40) / ROW_H;
    int y = LIST_TOP;

    for (int r = scroll_top; r < row_count && r < scroll_top + max_visible; r++) {
        if (rows[r].item == -2) {
            // Settings
            int sel = (r == sel_row);
            uint32_t clr = sel ? 0x00FFFF00 : 0x00AAAAAA;
            if (sel) fb_fill_rect(50, y - 2, PHYS_W - 100, ROW_H, 0x00181818);
            fb_puts_s(60 + INDENT, y, "== Settings ==", 1, clr);
            y += ROW_H;
        } else if (rows[r].item < 0) {
            // заголовок группы
            int g = rows[r].group;
            if (g < 0) {
                // разделитель
                y += ROW_H / 2;
            } else {
                fb_puts_s(60, y, group_names[g], 1, 0x00666666);
                y += ROW_H;
            }
        } else {
            int i = rows[r].item;
            int sel = (r == sel_row);
            uint32_t clr;
            if (g_items[i].status == STATUS_READY)
                clr = sel ? 0x00FFFF00 : 0x00FFFFFF;
            else
                clr = sel ? 0x00888800 : 0x00666666;
            if (sel)
                fb_fill_rect(50, y - 2, PHYS_W - 100, ROW_H, 0x00181818);
            char buf[64];
            int n = strlen(g_items[i].name);
            if (n > 52) n = 52;
            memcpy(buf, g_items[i].name, n);
            buf[n] = 0;
            if (g_items[i].status == STATUS_READY)
                strcat(buf, " [OK]");
            else if (g_items[i].status == STATUS_IN_PROGRESS)
                strcat(buf, " [WIP]");
            fb_puts_s(60 + INDENT, y, buf, 1, clr);
            y += ROW_H;
        }
    }

    fb_puts(60, FOOTER_Y, "  ^v : select    Enter : open    ESC : back", 0x00888888);
    fb_flush();
}

static int input_wait(void) {
    for (;;) {
        if (uart_rx_ready()) return uart_getc();
        int k = usb_input_poll();
        if (k) return k;
    }
}

int menu_run(void) {
    build_menu();
    sort_items();
    build_rows();

    if (row_count == 0) {
        fb_clear();
        fb_text_center("No ROM folders found on SD", 200, 2, 0x00FF4444);
        fb_text_center("Place ROMs in /roms/<system>/ folders", 250, 1, 0x00FFFFFF);
        fb_flush();
        return -1;
    }

    sel_row = 0;
    scroll_top = 0;
    // пропустить заголовок
    if (rows[sel_row].item < 0) sel_row++;

    for (;;) {
        render_menu();
        int k = input_wait();
        int max_visible = (FOOTER_Y - LIST_TOP - 40) / ROW_H;

        if (k == 82 || k == 'w') {
            int r = sel_row;
            do {
                if (r <= 0) break;
                r--;
            } while (r > 0 && rows[r].item < 0);
            if (rows[r].item >= 0 || rows[r].item == -2) {
                sel_row = r;
                if (sel_row < scroll_top) scroll_top = sel_row;
            }
        } else if (k == 81 || k == 's') {
            int r = sel_row;
            do {
                if (r >= row_count - 1) break;
                r++;
            } while (r < row_count - 1 && rows[r].item < 0);
            if (rows[r].item >= 0 || rows[r].item == -2) {
                sel_row = r;
                if (sel_row >= scroll_top + max_visible)
                    scroll_top = sel_row - max_visible + 1;
            }
        } else if (k == 40 || k == '\n' || k == '\r') {
            if (rows[sel_row].item == -2)
                return -2;    // Settings
            if (rows[sel_row].item >= 0) {
                int idx = rows[sel_row].item;
                return idx;
            }
        } else if (k == 41 || k == 27 || k == 'q') {
            return -1;
        }
    }
}

// вспомогательные для main.c
const char* menu_get_id(int idx) { return g_items[idx].id; }
const char* menu_get_dir(int idx) { return g_items[idx].dir; }
const char* menu_get_name(int idx) { return g_items[idx].name; }