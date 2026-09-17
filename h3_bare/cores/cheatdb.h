#ifndef CHEATS_H3_H
#define CHEATS_H3_H

#include <stdint.h>

#define CHEATS_MAX      64
#define CHEAT_DESC_LEN  64
#define CHEAT_CODE_LEN  48

typedef struct {
    char desc[CHEAT_DESC_LEN];
    char code[CHEAT_CODE_LEN];
    int  enabled;
} cheat_t;

// ---- менеджер списка читов текущей сессии ----
void cheats_reset(void);
int  cheats_count(void);
const cheat_t* cheats_get(int idx);
const char* cheats_desc(int idx);
const char* cheats_code(int idx);
int  cheats_enabled(int idx);
void cheats_toggle(int idx);
void cheats_set_enabled(int idx, int on);

// ---- загрузка .cht-файла базы libretro с SD ----
// system_folder — имя папки в /cheats (напр. "Sega - Game Gear").
// rom_name — имя ROM-файла (с расширением), ищем регистронезависимо.
// Возвращает число загруженных читов (0 — файл не найден/пуст).
int  cheats_load(const char* system_folder, const char* rom_name);

// ---- ручной ввод кода ----
// code в формате базы (напр. "000-46F-F7A"), desc — произвольная подпись.
// Возвращает 1 при успехе, 0 если список полон.
int  cheats_manual_add(const char* code, const char* desc);

// сопоставление, существует ли каталог читов для папки системы
int  cheats_dir_exists(const char* system_folder);

#endif