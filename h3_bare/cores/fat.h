#ifndef FAT_H3_H
#define FAT_H3_H

#include <stdint.h>

// r1355: запас оставлен намеренно. Изначально (r1354) /roms/enterprise содержал
// 939 каталогов + 592 файла; после чистки набора (2026-11-15) — 249 + 17.
// Значение не уменьшаем: остальным системам хватает с запасом, а память под
// g_scratch_dir уже выделена в .bss.
#define FAT_MAX_ENTRIES 1600
#define FAT_NAME_LEN    128

/* r1271 (D-13): 1 = последний read_dir оборвался (упёрлись в FAT_MAX_ENTRIES
   или ошибка чтения) — список неполон, вызывающий может предупредить. */
extern int g_fat_dir_truncated;

typedef struct {
    uint32_t first_cluster;
    uint32_t size;
    char     name[FAT_NAME_LEN];
    uint8_t  attr;          // r818: атрибут FAT (0x10 = каталог)
} fat_entry_t;

int  fat_init(void);
int  fat_list(const char* dir, fat_entry_t* out, int max);
int  fat_find(const char* dir, const char* name, fat_entry_t* out);

// Чтение файла по байтам. Возвращает количество прочитанных (или -1).
int  fat_read_file(const fat_entry_t* f, uint32_t offset, uint8_t* buf, uint32_t len);

// r1425/r1430: последовательное чтение с сохранением текущего кластера (аналог
// FATFS f_lseek+f_read в эталоне pico-286). Без этого каждый сектор HDD
// проходил цепочку FAT от первого кластера.
typedef struct {
    const fat_entry_t* f;
    uint32_t cl;        // текущий кластер
    uint32_t cl_off;    // файловое смещение начала кластера
} fat_stream_t;
void fat_stream_init(fat_stream_t* s, const fat_entry_t* f);
int  fat_stream_read(fat_stream_t* s, uint32_t offset, uint8_t* buf, uint32_t len);
int  fat_mkdir(const char* parent_path, const char* name);

// Запись/перезапись файла (dir_path — например "/", name — "retro.cfg").
// Создаёт новые кластеры по цепочке, пишет LFN+8.3 если нужно.
// Возвращает кол-во записанных байт, -1 при ошибке.
int  fat_write_file(const char* dir_path, const char* name, const uint8_t* data, uint32_t len);

// r832: потоковая запись (FTP STOR неограниченного размера). Кластеры
// выделяются по мере заполнения, промежуточный буфер не нужен.
//   fat_write_begin — создать/перезаписать запись каталога (size=0);
//   fat_write_data  — дописать произвольный кусок (любая длина);
//   fat_write_end   — закрыть цепочку и записать итоговый размер
//                     (возвращает число байт).
int  fat_write_begin(const char* dir_path, const char* name);
int  fat_write_data(const uint8_t* data, uint32_t len);
int  fat_write_end(void);

// r845: аборт потоковой записи при ошибке (см. STOR). Сбрасывает g_wr и
// помечает начатый файл как удалённый (0xE5) — на диске не остаётся
// частичного файла после неудачного приёма.
void fat_abort_write(void);

// Удаление файла: помечает как 0xE5 и освобождает кластеры в FAT
int  fat_delete_file(const char* dir, const char* name);

// Разделяемый буфер директорий (вместо fat_entry_t dirs[FAT_MAX_ENTRIES] на стеке).
// Контракт (аудит A-10, 17 мест): буфер один на порт и НЕ реентерабельный —
// пока держим указатель, нельзя вызывать fat_list/fat_find/fat_delete_file/
// path_lookup (они его перезаполняют). Имя/поля копировать ДО такого вызова
// (так сделано в rom_browser.c, cheatdb.c, fat_find). Вложенных вызовов в
// текущем коде нет — проверено по всем 17 точкам.
fat_entry_t* fat_scratch(void);

// r788: блочный источник. По умолчанию — SD (sd_read/sd_write_sector).
// Вызывающий (main.c) может назначить кастомный источник (USB-флешка):
// rd/wr — функции чтения/записи 512-байтного сектора. rd==0 → снова SD.
typedef int (*fat_blk_read_t)(uint32_t lba, void* buf);
typedef int (*fat_blk_write_t)(uint32_t lba, const void* buf);
void fat_set_block_source(fat_blk_read_t rd, fat_blk_write_t wr);

#endif