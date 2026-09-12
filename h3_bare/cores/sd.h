#ifndef SD_H3_H
#define SD_H3_H

#include <stdint.h>

int  sd_init(void);
int  sd_read_sector(uint32_t lba, void* buf);

#endif