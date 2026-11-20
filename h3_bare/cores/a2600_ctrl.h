// a2600_ctrl.h — тип контроллера A2600 по MD5 рома.
//
// Зачем: в Stella тип контроллера берётся из базы свойств (Controller.Left/
// Right). Наша сборка базу DefProps не подключает (273 КБ .rodata), поэтому
// все рома получали JOYSTICK — а paddle-игры читают TIA INPT0-3 (аналог), и
// управление в них не работало. Здесь компактная таблица (179 записей,
// ~3.6 КБ) только для не-джойстиковых типов: PADDLES* и KEYBOARD/KEYPAD.
//
// Недостающие в базе 2014 года paddle-тайтлы (Video Pinball, Arcade Pong и
// т.п.) определяются по имени файла (см. a2600_ctrl.c).
#ifndef A2600_CTRL_H
#define A2600_CTRL_H

// Коды типов (совпадают с порядком в таблице).
#define A26C_JOY        0
#define A26C_PADDLES    1
#define A26C_PAD_IAXIS  2
#define A26C_PAD_IDIR   3
#define A26C_PAD_IAXDR  4
#define A26C_KEYBOARD   5

// Ищет тип контроллеров по MD5 (32 hex) и имени рома.
// Возвращает 1, если найден не-джойстиковый тип (или сработал эвристик по
// имени), иначе 0. left/right/swap всегда заполняются (по умолчанию JOY/0).
int a2600_ctrl_lookup(const char* md5hex, const char* rom_name,
                      int* left, int* right, int* swap);

#endif // A2600_CTRL_H
