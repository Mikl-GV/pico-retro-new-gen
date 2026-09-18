// remap.h — ремап клавиатуры -> кнопок эмуляторов (по платформам).
// Пользователь настраивает в Settings: выбирает платформу, выбирает кнопку,
// жмёт клавишу на клавиатуре — соответствие запоминается в RAM и на SD
// (/retro.cfg). Дедолт — текущие встроенные раскладки (Z=A, X=B, ...).
//
// Хосты эмуляторов вместо хардкода "if (sc==29)" используют
// remap_kbd_pressed(plat, BTN_*, keys, n) — единая точка переназначения.
#ifndef REMAP_H
#define REMAP_H

#include <stdint.h>

// ---- Платформы (порядок важен: совпадает с индексами в remap.c) ----
#define REMAP_PLAT_MD     0   // Sega Mega Drive / Genesis
#define REMAP_PLAT_SMS    1   // Master System
#define REMAP_PLAT_GG     2   // Game Gear
#define REMAP_PLAT_SNES   3   // SNES / Super Famicom
#define REMAP_PLAT_NES    4   // NES / Famicom
#define REMAP_PLAT_GB     5   // Game Boy / Game Boy Color
#define REMAP_PLAT_GBA    6   // Game Boy Advance
#define REMAP_PLAT_LYNX   7   // Atari Lynx
#define REMAP_PLAT_NGP    8   // Neo Geo Pocket / Color
#define REMAP_PLAT_A2600  9   // Atari 2600
#define REMAP_PLAT_A5200  10  // Atari 5200
#define REMAP_PLAT_A7800  11  // Atari 7800
#define REMAP_PLAT_COUNT  12

// ---- Индексы кнопок в списке каждой платформы ----
// (общие для всех, где есть; у платформы может не быть некоторых — смотри remap.c)
#define BTN_UP     0
#define BTN_DOWN   1
#define BTN_LEFT   2
#define BTN_RIGHT  3
// "основные" кнопки: A/B/C/X/Y/Z/Start/Select/Mode/L/R и т.п.
#define BTN_A      4
#define BTN_B      5
#define BTN_C      6
#define BTN_X      7
#define BTN_Y      8
#define BTN_Z      9
#define BTN_START  10
#define BTN_SELECT 11
#define BTN_MODE   12
#define BTN_L      13
#define BTN_R      14
// корпусные/спец. кнопки консолей:
//   A2600: Reset, Difficulty
//   A5200: Pause, Key3, Fire2 (второй огонь)
//   A7800: (Pause на корпусе)
//   SMS/GG: Pause (корпусная у SMS), Start (у GG)
//   MD: (нет корпусных)
#define BTN_RESET     15
#define BTN_DIFF      16
#define BTN_PAUSE     17
#define BTN_KEY3      18
#define BTN_OPT1      19   // Lynx Option 1, NGP Select
#define BTN_OPT2      20   // Lynx Option 2, NGP Start
#define BTN_FIRE      21   // A2600/A5200 Fire
#define BTN_FIRE2     22   // A5200 второй огонь (нижняя кнопка)
#define BTN_MAX       23

// Флаг Shift-комбинации (SNES и прочие с >8 кнопок):
// если установлен — привязка срабатывает ТОЛЬКО при удержании Shift + клавиша.
#define REMAP_MOD_SHIFT 0x01

// Описание одной кнопки платформы
typedef struct {
    const char* label;   // как показать в меню
    uint8_t     def;     // скан-код по умолчанию (0 = не назначено)
} remap_btn_t;

typedef struct {
    const char* name;         // имя платформы для меню
    const remap_btn_t* btns;  // массив BTN_MAX — по индексу кнопки
} remap_plat_t;

// ---- API ----
// Текущий скан-код кнопки платформы (после ремапа)
uint8_t remap_get(int plat, int btn);

// Нажата ли кнопка платформы на текущем состоянии клавиатуры
// (keys — массив скан-кодов HID от usb_kbd_get_raw)
int remap_kbd_pressed(int plat, int btn, const uint8_t* keys, int n);

// Назначить кнопке платформы скан-код (0 = снять)
void remap_set(int plat, int btn, uint8_t sc);

// Сброс к дедолту
void remap_defaults(void);

// Загрузить/сохранить /retro.cfg (SD). load вызывается в main после fat_init.
void remap_load(void);
void remap_save(void);

// Интерактивное меню настройки (Settings). Вызывается как settings пункт.
void remap_menu(void);

// Доступ к таблицам для меню
const remap_plat_t* remap_plats(void);   // массив [PLAT_COUNT]

#endif