// emulog.h — единый каркас диагностического лога сессий эмуляторов.
// Структура (см. docs/HANDOVER.md, раздел «Лог сессий эмуляторов»):
//   ==== <SYS>: start ====                |  ==== <SYS>: exit ====
//   <SYS>: rom       путь, размер          |  <SYS>: frames    кадры, fps
//   <SYS>: machine   модель/регион/период  |  <SYS>: audio     pairs/fill/drop/under
//   <SYS>: video     разрешение кадра      |  <SYS>: done
//   <SYS>: audio     частота/каналы
//   <SYS>: init      OK | FAILED: причина
//   <SYS>: running                          (+ отдельные события: disk, set/roms, ...)
//
// Все host-слои обязаны выдавать ровно один блок start и один блок exit
// на сессию, через эти хелперы. Формат: строка = "<SYS>: <field><tab><data>",
// поле выровнено до 8 символов — фильтруемо grep'ом по "<SYS>:".
//
// r1410: глобальный флаг g_emulog — диагностика включается/выключается ОДНОЙ
// переменной. Раньше каждая система печатала по-своему и не всегда.
#ifndef EMULOG_H
#define EMULOG_H

#include <stdint.h>

#ifndef __cplusplus
#include <stddef.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Единственный гейт всего сессионного лога. 1 = выводить (релиз по умолчанию,
// «значимые логи остаются»), 0 = весь каркас молчит. Меняется в отладке одной
// строкой/переменной. Объявлен здесь, определён в emulog.c.
extern int g_emulog;

// ==== <tag>: start ====
void emulog_begin(const char* tag);
// ==== <tag>: exit ====
void emulog_end(const char* tag);

// <tag>: <field>\t<data> — выравнивание field до 8 символов.
// Никакой проверки g_emulog внутри field-вызовов нет (горячие вызовы кадра
// должны быть редкими/одноразовыми); начало и конец сессии гейтятся сами.
void emulog_field(const char* tag, const char* field, const char* fmt, ...);

// Сводка сессии: кадры/fps. Вызывается на выходе.
void emulog_frames(const char* tag, uint32_t frames, uint32_t duration_us);

#ifdef __cplusplus
}
#endif

#endif // EMULOG_H