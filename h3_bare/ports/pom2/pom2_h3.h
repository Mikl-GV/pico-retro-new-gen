// pom2_h3.h — платформенный слой POM2 для bare-metal H3.
// Наружу отдаёт C-интерфейс: слой Apple II (C) не должен видеть C++/STL.
#ifndef POM2_H3_H
#define POM2_H3_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Запуск машины. ROM и образ диска передаются БАЙТАМИ: файловой системы на
// bare-metal нет, их читает наш SD-слой (см. agat_set_disk_image_ex).
// rom — 2/4/8 КБ ПЗУ Apple II (II+ либо //e-класса), iie — режим //e.
// disk/disk_size могут быть NULL/0 (машина без диска: Applesoft).
// disk_name — имя файла: по расширению определяется порядок секторов
// (.dsk и .po одного размера) и формат (.woz/.nib/.2mg).
// Возвращает 0 при успехе, отрицательное — код ошибки.
int pom2_h3_boot(const uint8_t *rom, unsigned rom_size, int iie,
                 const uint8_t *disk, unsigned disk_size,
                 const char *disk_name);

// Один видеокадр: CPU гонится до границы кадра, дисплей перерисовывается.
// Возвращает 0 при успехе.
int pom2_h3_frame(void);

// Кадр после pom2_h3_frame: RGBA 0x00RRGGBB (как отдаёт Apple2Display),
// размеры в пикселях (280×192 или 560×192). Указатель валиден до следующего
// вызова pom2_h3_frame(): хост выводит кадр сразу, без промежуточной копии.
const uint32_t *pom2_h3_pixels(int *w, int *h);

// Биты видеорежима для диагностики (Memory::DisplayState + картинка карты):
// 0 текст, 1 mixed, 2 page2, 3 hi-res, 4 80 col, 5 AN3, 6 altchar, 7 DHGR,
// 8 80STORE, 9 карта показывает своё (Videx/VidHD).
unsigned pom2_h3_display_flags(void);

// Модель машины: бит 0 = режим //e включён, биты 8..15 = iieMemMode// (80STORE/RAMRD/RAMWRT/... ). Нужно, чтобы отличать «//e-бит выставил
// софт» от «порт случайно стартовал в //e».
unsigned pom2_h3_machine_flags(void);

// Программный счётчик CPU (PC) — для heartbeat-диагностики зависаний:
// по нему видно, крутится ли машина в ожидании и где именно.
unsigned pom2_h3_cpu_pc(void);

// Клавиатура: ch — ASCII/код Apple II, down != 0 — нажатие.
// У ][/][+ нет отпускания клавиши, down игнорируется для машин без //e.
void pom2_h3_key(int ch, int down);

// Джойстик: paddles[0..3] — аналоговые значения 0..255 ($C064-$C067/$C070),
// btn0/btn1 — кнопки паддла 0/1 ($C061-$C063, Open/Solid Apple — отдельно).
void pom2_h3_paddles(const uint8_t *paddles, int btn0, int btn1);

// Звук: динамик Apple II ($C030) в стерео-ПАРАХ (L,R) — формат i2s_push_block.
// Возвращает число записанных ПАР; буфер обязан быть int16[2*max_pairs].
int pom2_h3_audio(int16_t *pairs, unsigned max_pairs);

// Свап диска на ходу: образ ставится в привод 0/1 без перезапуска машины.
// 0 — успех, -1 нет машины/привода, -2 образ не разобран, -3 не установлен.
int pom2_h3_insert_disk(int drive, const uint8_t *data, unsigned len,
                        const char *name);

// Освобождение: объекты ядра живут в статике, но кучу std-контейнеров
// надо отпустить между запусками эмуляторов (см. emu.c, _sbrk).
void pom2_h3_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
