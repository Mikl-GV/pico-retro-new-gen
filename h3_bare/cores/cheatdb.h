#ifndef CHEATS_H3_H
#define CHEATS_H3_H

#include <stdint.h>

#define CHEATS_MAX      128
#define CHEAT_DESC_LEN  96
#define CHEAT_CODE_LEN  64

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

// ---- RAW-читы (addr:value[:cmp]) для систем без своего движка читов ----
// Поддерживаются A2600/A5200/A7800/Lynx/NGP — применяются в хосте каждый кадр.
// Формат кода: "AAAA:VV" или "AAAA:VV:CC" (hex). Единый для всех таких систем.
// Возвращает 1 если код выглядит как RAW и добавлен, 0 если нет/список полон.
int  cheats_parse_raw(const char* code, uint32_t* addr, uint8_t* value, uint8_t* cmp, int* has_cmp);
// Сколько активных RAW-читов сейчас в списке
int  cheats_raw_count(void);
// Получить i-й активный RAW-чит
int  cheats_raw_get(int i, uint32_t* addr, uint8_t* value, uint8_t* cmp, int* has_cmp);

// сопоставление, существует ли каталог читов для папки системы
int  cheats_dir_exists(const char* system_folder);

#endif