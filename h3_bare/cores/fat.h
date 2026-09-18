#ifndef FAT_H3_H
#define FAT_H3_H

#include <stdint.h>

#define FAT_MAX_ENTRIES 128
#define FAT_NAME_LEN    128

typedef struct {
    uint32_t first_cluster;
    uint32_t size;
    char     name[FAT_NAME_LEN];
} fat_entry_t;

int  fat_init(void);
int  fat_list(const char* dir, fat_entry_t* out, int max);
int  fat_find(const char* dir, const char* name, fat_entry_t* out);

// Чтение файла по байтам. Возвращает количество прочитанных (или -1).
int  fat_read_file(const fat_entry_t* f, uint32_t offset, uint8_t* buf, uint32_t len);
int  fat_mkdir(const char* parent_path, const char* name);

// Запись/перезапись файла (dir_path — например "/", name — "retro.cfg").
// Создаёт новые кластеры по цепочке, пишет LFN+8.3 если нужно.
// Возвращает кол-во записанных байт, -1 при ошибке.
int  fat_write_file(const char* dir_path, const char* name, const uint8_t* data, uint32_t len);

// Удаление файла: помечает как 0xE5 и освобождает кластеры в FAT
int  fat_delete_file(const char* dir, const char* name);

// Разделяемый буфер директорий (вместо fat_entry_t dirs[FAT_MAX_ENTRIES] на стеке).
// Одновременно используется только один контекст (меню/браузер/FAT).
fat_entry_t* fat_scratch(void);

#endif