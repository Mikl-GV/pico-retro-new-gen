#ifndef FAT_H3_H
#define FAT_H3_H

#include <stdint.h>

#define FAT_MAX_ENTRIES 64
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

#endif