#include <stdint.h>
#include <string.h>
#include "fb_text.h"
#include "systems.h"
#include "uart.h"
#include "usb_kbd.h"

#define PHYS_W 1024
#define PHYS_H 600

#define ROW_H     18
#define TITLE_Y   30
#define SEP_Y     62
#define LIST_TOP  74
#define FOOTER_Y  (PHYS_H - 30)
#define INDENT    40

#define MAX_ROWS (GROUP_COUNT + NUM_SYSTEMS)

typedef struct {
    int sys;          // индекс в systems[], или -1 для заголовка группы
    int group;        // группа (для заголовка)
} row_t;

static row_t rows[MAX_ROWS];
static int row_count;

static void build_rows(void) {
    row_count = 0;
    for (int g = 0; g < GROUP_COUNT; g++) {
        int count = 0;
        for (int i = 0; i < (int)NUM_SYSTEMS; i++)
            if (systems[i].group == g) count++;
        if (count == 0) continue;

        rows[row_count].sys = -1;
        rows[row_count].group = g;
        row_count++;

        for (int i = 0; i < (int)NUM_SYSTEMS; i++)
            if (systems[i].group == g) {
                rows[row_count].sys = i;
                rows[row_count].group = g;
                row_count++;
            }
    }
}

static void render_menu(int sel_row, int top) {
    fb_clear();
    fb_puts_s(60, TITLE_Y, "MultiTool Retro", 2, 0x00FF0000);
    fb_fill_rect(60, SEP_Y, 200, 2, 0x00FFFFFF);

    int max_rows = (FOOTER_Y - LIST_TOP - 40) / ROW_H;
    int y = LIST_TOP;

    for (int r = top; r < row_count && r < top + max_rows; r++) {
        if (rows[r].sys < 0) {
            fb_puts_s(60, y, group_names[rows[r].group], 1, 0x00666666);
        } else {
            int i = rows[r].sys;
            int sel = (r == sel_row);
            uint32_t clr;
            if (systems[i].status == STATUS_READY)
                clr = sel ? 0x00FFFF00 : 0x00FFFFFF;
            else
                clr = sel ? 0x00888800 : 0x00666666;

            if (sel)
                fb_fill_rect(50, y - 2, PHYS_W - 100, ROW_H, 0x00181818);

            char buf[72];
            int n = strlen(systems[i].name);
            if (n > 52) n = 52;
            memcpy(buf, systems[i].name, n);
            buf[n] = 0;

            if (systems[i].status == STATUS_READY)
                strcat(buf, " [OK]");
            else if (systems[i].status == STATUS_IN_PROGRESS)
                strcat(buf, " [...]");

            fb_puts_s(60 + INDENT, y, buf, 1, clr);
        }
        y += ROW_H;
    }

    fb_puts(60, FOOTER_Y, "  ^v : select    Enter : open    ESC : back", 0x00888888);
    fb_flush();
}

static int input_wait(void) {
    for (;;) {
        if (uart_rx_ready()) return uart_getc();
        int k = usb_kbd_poll();
        if (k) return k;
    }
}

int menu_run(void) {
    build_rows();

    int sel_row = 0;
    int top = 0;
    int max_rows = (FOOTER_Y - LIST_TOP - 40) / ROW_H;

    while (rows[sel_row].sys < 0) sel_row++;

    for (;;) {
        render_menu(sel_row, top);
        int k = input_wait();

        if (k == 82 || k == 'w') {
            int r = sel_row;
            do {
                if (r <= 0) break;
                r--;
            } while (rows[r].sys < 0);
            if (rows[r].sys >= 0) {
                sel_row = r;
                if (sel_row < top) top = sel_row;
            }
        } else if (k == 81 || k == 's') {
            int r = sel_row;
            do {
                if (r >= row_count - 1) break;
                r++;
            } while (rows[r].sys < 0);
            if (rows[r].sys >= 0) {
                sel_row = r;
                if (sel_row >= top + max_rows) top = sel_row - max_rows + 1;
            }
        } else if (k == 40 || k == '\n' || k == '\r') {
            if (rows[sel_row].sys >= 0) return rows[sel_row].sys;
        } else if (k == 41 || k == 27 || k == 'q') {
            return -1;
        }
    }
}