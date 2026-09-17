// cheats.c — менеджер читов (парсер .cht базы libretro + управление)
#include <stdint.h>
#include <string.h>
#include "cheatdb.h"
#include "fat.h"
#include "uart.h"

extern int printf(const char* fmt, ...);

static cheat_t g_cheats[CHEATS_MAX];
static int g_count = 0;
static int g_loaded = 0;   // 1 = загружены из файла

void cheats_reset(void) {
    g_count = 0;
    g_loaded = 0;
}

int cheats_count(void) { return g_count; }

const cheat_t* cheats_get(int idx) {
    return (idx >= 0 && idx < g_count) ? &g_cheats[idx] : 0;
}

const char* cheats_desc(int idx) {
    return (idx >= 0 && idx < g_count) ? g_cheats[idx].desc : 0;
}

const char* cheats_code(int idx) {
    return (idx >= 0 && idx < g_count) ? g_cheats[idx].code : 0;
}

int cheats_enabled(int idx) {
    return (idx >= 0 && idx < g_count) ? g_cheats[idx].enabled : 0;
}

void cheats_toggle(int idx) {
    if (idx >= 0 && idx < g_count)
        g_cheats[idx].enabled = !g_cheats[idx].enabled;
}

void cheats_set_enabled(int idx, int on) {
    if (idx >= 0 && idx < g_count)
        g_cheats[idx].enabled = on ? 1 : 0;
}

// регистронезависимое сравнение имени рома с именем .cht файла (без расширения)
static int name_match(const char* rom_name, const char* cht_name) {
    while (*rom_name && *cht_name) {
        char a = *rom_name, b = *cht_name;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        rom_name++; cht_name++;
    }
    // rom_name кончился — cht_name может кончиться или за ним идёт
    // '.' (расширение) или '(' (суффикс региона)
    if (!*rom_name) {
        return (!*cht_name || *cht_name == '.' || *cht_name == '(');
    }
    return 0;
}

// Значение в строке "key = "value"" — снимаем кавычки и обрезаем
static void extract_value(const char* eq, char* out, int maxlen) {
    const char* cv = eq + 1;
    while (*cv == ' ' || *cv == '\t') cv++;
    if (*cv == '"') cv++;
    int vl = 0;
    while (*cv && *cv != '"' && *cv != '\n' && *cv != '\r' && vl < maxlen - 1)
        out[vl++] = *cv++;
    out[vl] = 0;
}

// true если строка начинается с "cheat<N>_<field>"
static int cheat_field(const char* line, int* num, const char** field) {
    if (strncmp(line, "cheat", 5) != 0) return 0;
    const char* p = line + 5;
    int n = 0;
    while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
    if (*p != '_') return 0;
    *num = n;
    *field = p + 1;
    return 1;
}

int cheats_load(const char* system_folder, const char* rom_name) {
    cheats_reset();
    if (!rom_name || !rom_name[0]) return 0;

    // /cheats/<system_folder>/
    char dir[FAT_NAME_LEN + 40];
    int dp = 0;
    const char* pfx = "/cheats/";
    while (*pfx && dp < (int)sizeof(dir) - 1) dir[dp++] = *pfx++;
    for (const char* s = system_folder; *s && dp < (int)sizeof(dir) - 1; s++) dir[dp++] = *s;
    dir[dp] = 0;

    fat_entry_t* list = fat_scratch();
    int n = fat_list(dir, list, FAT_MAX_ENTRIES);
    if (n <= 0) { printf("cheats: dir not found %s\n", dir); return 0; }

    // Ищем .cht по имени (регистронезависимо)
    char found_path[FAT_NAME_LEN + 80];
    fat_entry_t found_entry;
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (list[i].size == 0) continue;
        const char* fn = list[i].name;
        int fl = strlen(fn);
        if (fl < 5) continue;
        if (fn[fl-4] != '.' ||
            (fn[fl-3] != 'c' && fn[fl-3] != 'C') ||
            (fn[fl-2] != 'h' && fn[fl-2] != 'H') ||
            (fn[fl-1] != 't' && fn[fl-1] != 'T')) continue;
        char cht_name[FAT_NAME_LEN];
        int cl = fl - 4;
        if (cl >= FAT_NAME_LEN) cl = FAT_NAME_LEN - 1;
        memcpy(cht_name, fn, cl);
        cht_name[cl] = 0;
        if (name_match(rom_name, cht_name)) {
            found_entry = list[i];
            int plen = dp;
            found_path[plen++] = '/';
            int flen = fl;
            if (flen >= (int)sizeof(found_path) - plen - 1)
                flen = (int)sizeof(found_path) - plen - 1;
            memcpy(found_path + plen, fn, flen);
            found_path[plen + flen] = 0;
            found = 1;
            break;
        }
    }

    if (!found) { printf("cheats: no cht for %s in %s\n", rom_name, dir); return 0; }

    // Читаем
    uint8_t buf[8192];
    int r = fat_read_file(&found_entry, 0, buf, sizeof(buf) - 1);
    if (r <= 0) { printf("cheats: read fail %s\n", found_path); return 0; }
    buf[r] = 0;
    printf("cheats: loaded %s (%d bytes)\n", found_path, r);

    // Парсим: до "cheats = N" пропускаем, дальше деск/код
    char* data = (char*)buf;
    int in_block = 0;
    char line[256];

    while (*data && g_count < CHEATS_MAX) {
        int li = 0;
        while (*data && *data != '\n' && li < 255) line[li++] = *data++;
        if (*data == '\n') data++;
        line[li] = 0;

        if (!in_block) {
            // ищем строку "cheats = N" (конец шапки)
            if (strncmp(line, "cheats", 6) == 0) in_block = 1;
            continue;
        }

        int num;
        const char* field;
        if (!cheat_field(line, &num, &field)) continue;

        if (num >= CHEATS_MAX) continue;

        if (strncmp(field, "desc", 4) == 0) {
            const char* eq = strchr(line, '=');
            if (eq) extract_value(eq, g_cheats[num].desc, CHEAT_DESC_LEN);
        } else if (strncmp(field, "code", 4) == 0) {
            const char* eq = strchr(line, '=');
            if (eq) {
                extract_value(eq, g_cheats[num].code, CHEAT_CODE_LEN);
                g_cheats[num].enabled = 0;
                if (num >= g_count) g_count = num + 1;
            }
        }
    }

    // Отсекаем пустые записи (desc/code отсутствуют)
    int w = 0;
    for (int i = 0; i < g_count; i++) {
        if (g_cheats[i].code[0]) {
            if (w != i) g_cheats[w] = g_cheats[i];
            w++;
        }
    }
    g_count = w;
    g_loaded = (g_count > 0) ? 1 : 0;
    printf("cheats: %d cheats\n", g_count);
    return g_count;
}

int cheats_manual_add(const char* code, const char* desc) {
    if (g_count >= CHEATS_MAX) return 0;
    if (!code || !code[0]) return 0;
    int cl = strlen(code);
    if (cl >= CHEAT_CODE_LEN) cl = CHEAT_CODE_LEN - 1;
    memcpy(g_cheats[g_count].code, code, cl);
    g_cheats[g_count].code[cl] = 0;
    if (desc) {
        int dl = strlen(desc);
        if (dl >= CHEAT_DESC_LEN) dl = CHEAT_DESC_LEN - 1;
        memcpy(g_cheats[g_count].desc, desc, dl);
        g_cheats[g_count].desc[dl] = 0;
    } else {
        g_cheats[g_count].desc[0] = 0;
    }
    g_cheats[g_count].enabled = 1;
    g_count++;
    return 1;
}

int cheats_dir_exists(const char* system_folder) {
    char dir[FAT_NAME_LEN + 40];
    int dp = 0;
    const char* pfx = "/cheats/";
    while (*pfx) dir[dp++] = *pfx++;
    for (const char* s = system_folder; *s; s++) dir[dp++] = *s;
    dir[dp] = 0;
    fat_entry_t* list = fat_scratch();
    int n = fat_list(dir, list, 1);
    return (n > 0) ? 1 : 0;
}