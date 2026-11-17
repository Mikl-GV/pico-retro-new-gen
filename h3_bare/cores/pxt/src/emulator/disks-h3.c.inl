#pragma once
/*
 * disks-h3.c.inl — H3-адаптация дискового INT 13h ядра pico-286 к RAM-образам.
 *
 * Оригинал (disks-win32.c.inl / disks-rp2350.c.inl) читает диски через
 * FATFS/`FILE*` на хост-FS. На H3 нет мульти-buf FATFS у ядра: образы дисков
 * лежат в RAM-пуле (как у ядра MS1504). Конфигурация дисков задаётся из
 * pxt_host.c через setter-функции:
 *   pxt_disk_set(drive, base, len)  — положить образ в RAM-область;
 *   pxt_disk_clear(drive)           — извлечь диск;
 *   pxt_disk_geometry(drive, &cyls,&heads,&sects) — геометрия по размеру.
 *
 * Механика INT13/diskhandler, readdisk/writedisk, CHS-пересчёт — та же, что
 * в win32-версии, но чтение/запись секторов идут прямыми memcpy в/из RAM.
 * disk[] тут хранит base/len вместо FILE*+filesize.
 */

#include "emulator.h"
#include <string.h>

// s8-размер IMA-образа в байтах — размер дисковых слотов (4 привода).
#define PXT_DISK_MAX_DRIVES 4

int hdcount = 0, fdcount = 0;

struct struct_drive {
    const uint8_t* base;      // RAM-адрес образа (NULL = не вставлен)
    size_t filesize;          // размер образа в байтах
    uint16_t cyls;
    uint16_t sects;
    uint16_t heads;
    uint8_t inserted;
    uint8_t readonly;
} disk[PXT_DISK_MAX_DRIVES];

// ---- внутренняя механика (идентична win32, но по RAM) ----
static inline void ejectdisk(uint8_t drivenum);

// ---- конфигурация из pxt_host.c ----
void pxt_disk_set(uint8_t drivenum, const uint8_t* base, size_t len) {
    if (drivenum & 0x80) drivenum -= 126;
    if (drivenum >= PXT_DISK_MAX_DRIVES) return;
    ejectdisk(drivenum);
    disk[drivenum].base = base;
    disk[drivenum].filesize = len;
    disk[drivenum].inserted = (base && len) ? 1 : 0;
    disk[drivenum].readonly = 0;
    if (!disk[drivenum].inserted) return;
    // Геометрия: как win32-версия (флоппи 360К/720К/1.2М, HDD 63x16).
    uint16_t cyls = 0, heads = 0, sects = 0;
    if (drivenum >= 2) {        // HDD
        sects = 63; heads = 16;
        cyls = (uint16_t)(len / ((size_t)sects * heads * 512));
    } else {                    // флоппи
        cyls = 80; sects = 18; heads = 2;
        if (len <= 368640UL)      { cyls = 40; sects = 9;  heads = 2; }
        else if (len <= 737280UL) { cyls = 80; sects = 9; }
        else if (len <= 1228800UL){ cyls = 80; sects = 15; }
    }
    disk[drivenum].cyls = cyls;
    disk[drivenum].heads = heads;
    disk[drivenum].sects = sects;
    if (drivenum >= 2) hdcount++; else fdcount++;
}

void pxt_disk_clear(uint8_t drivenum) {
    if (drivenum & 0x80) drivenum -= 126;
    if (drivenum >= PXT_DISK_MAX_DRIVES) return;
    ejectdisk(drivenum);
}

// insertdisk — интерфейс, совместимый с win32-версией ядра. На H3 файлов не
// открываем: принимает уже положенный в RAM образ. Часть игр/BIOS в INT 19h
// перечитает приводы через insertdisk(0..129) (cpu.c case 0x19); здесь это
// безопасно-ignored, т.к. host-config дисков делает pxt_host.c до reset86().
uint8_t insertdisk(uint8_t drivenum, const char *pathname) {
    (void)drivenum; (void)pathname;
    return 0;
}

// ---- внутренняя механика (идентична win32, но по RAM) ----
static inline void ejectdisk(uint8_t drivenum) {
    if (drivenum & 0x80) drivenum -= 126;
    if (drivenum >= PXT_DISK_MAX_DRIVES) return;
    if (disk[drivenum].inserted) {
        disk[drivenum].inserted = 0;
        disk[drivenum].base = NULL;
        if (drivenum >= 2) hdcount--; else fdcount--;
    }
}

static inline size_t chs2ofs(int drivenum, int cyl, int head, int sect) {
    return (((size_t)cyl * (size_t)disk[drivenum].heads + (size_t)head)
            * (size_t)disk[drivenum].sects + (size_t)sect - 1) * 512UL;
}

static void readdisk(uint8_t drivenum,
              uint16_t dstseg, uint16_t dstoff,
              uint16_t cyl, uint16_t sect, uint16_t head,
              uint16_t sectcount, int is_verify)
{
    uint32_t memdest = ((uint32_t)dstseg << 4) + (uint32_t)dstoff;
    if (!disk[drivenum].inserted) { CPU_AH = 0x31; CPU_AL = 0; CPU_FL_CF = 1; return; }
    if (sect == 0 || sect > disk[drivenum].sects || cyl >= disk[drivenum].cyls || head >= disk[drivenum].heads) {
        CPU_AH = 0x04; CPU_AL = 0; CPU_FL_CF = 1; return;
    }
    size_t fileoffset = chs2ofs(drivenum, cyl, head, sect);
    if (fileoffset > disk[drivenum].filesize) { CPU_AH = 0x04; CPU_AL = 0; CPU_FL_CF = 1; return; }
    const uint8_t* src = disk[drivenum].base + fileoffset;
    for (uint16_t cursect = 0; cursect < sectcount; cursect++) {
        if ((size_t)(src + 512 - disk[drivenum].base) > disk[drivenum].filesize) {
            CPU_AH = 0x04; CPU_AL = 0; CPU_FL_CF = 1; return;
        }
        if (is_verify) {
            for (int o = 0; o < 512; o++)
                if (read86(memdest++) != src[o]) { CPU_AL = cursect; CPU_FL_CF = 1; CPU_AH = 0xBB; return; }
        } else {
            for (int o = 0; o < 512; o++) write86(memdest++, src[o]);
        }
        src += 512;
    }
    CPU_AL = sectcount; CPU_FL_CF = 0; CPU_AH = 0;
}

static void writedisk(uint8_t drivenum,
               uint16_t dstseg, uint16_t dstoff,
               uint16_t cyl, uint16_t sect, uint16_t head,
               uint16_t sectcount)
{
    uint32_t memdest = ((uint32_t)dstseg << 4) + (uint32_t)dstoff;
    if (!disk[drivenum].inserted) { CPU_AH = 0x31; CPU_AL = 0; CPU_FL_CF = 1; return; }
    size_t fileoffset = chs2ofs(drivenum, cyl, head, sect);
    if (sect == 0 || sect > disk[drivenum].sects || cyl >= disk[drivenum].cyls || head >= disk[drivenum].heads
        || fileoffset > disk[drivenum].filesize) {
        CPU_AH = 0x04; CPU_AL = 0; CPU_FL_CF = 1; return;
    }
    if (disk[drivenum].readonly) { CPU_AH = 0x03; CPU_AL = 0; CPU_FL_CF = 1; return; }
    uint8_t* dst = (uint8_t*)(disk[drivenum].base + fileoffset);
    for (uint16_t cursect = 0; cursect < sectcount; cursect++) {
        if ((size_t)(dst + 512 - disk[drivenum].base) > disk[drivenum].filesize) {
            CPU_AH = 0x04; CPU_AL = 0; CPU_FL_CF = 1; return;
        }
        for (int o = 0; o < 512; o++) dst[o] = read86(memdest++);
        dst += 512;
    }
    if (sectcount && sectcount == 0) { CPU_AH = 0x04; CPU_AL = 0; CPU_FL_CF = 1; return; }
    CPU_AL = sectcount; CPU_FL_CF = 0; CPU_AH = 0;
}

static INLINE void diskhandler(void) {
    static uint8_t lastdiskah[PXT_DISK_MAX_DRIVES] = { 0 }, lastdiskcf[PXT_DISK_MAX_DRIVES] = { 0 };
    uint8_t drivenum = CPU_DL;
    if (drivenum & 0x80) drivenum -= 126;
    if (drivenum >= PXT_DISK_MAX_DRIVES) { CPU_FL_CF = 1; return; }

    switch (CPU_AH) {
        case 0x00:
            if (disk[drivenum].inserted) { CPU_AH = 0; CPU_FL_CF = 0; }
            else CPU_FL_CF = 1;
            break;
        case 0x01:
            CPU_AH = lastdiskah[drivenum];
            CPU_FL_CF = lastdiskcf[drivenum];
            return;
        case 0x02:
            readdisk(drivenum, CPU_ES, CPU_BX, CPU_CH + (CPU_CL / 64) * 256, CPU_CL & 63, CPU_DH, CPU_AL, 0);
            break;
        case 0x03:
            writedisk(drivenum, CPU_ES, CPU_BX, CPU_CH + (CPU_CL / 64) * 256, CPU_CL & 63, CPU_DH, CPU_AL);
            break;
        case 0x04:
            readdisk(drivenum, CPU_ES, CPU_BX, CPU_CH + (CPU_CL / 64) * 256, CPU_CL & 63, CPU_DH, CPU_AL, 1);
            break;
        case 0x05:
            CPU_FL_CF = 0; CPU_AH = 0;
            break;
        case 0x08:
            if (disk[drivenum].inserted) {
                CPU_FL_CF = 0; CPU_AH = 0;
                CPU_CH = disk[drivenum].cyls - 1;
                CPU_CL = (disk[drivenum].sects & 63) + ((disk[drivenum].cyls / 256) * 64);
                CPU_DH = disk[drivenum].heads - 1;
                if (CPU_DL < 2) { CPU_BL = 4; CPU_DL = 2; }
                else CPU_DL = (uint8_t)hdcount;
            } else { CPU_FL_CF = 1; CPU_AH = 0xAA; }
            break;
        default: CPU_FL_CF = 1; break;
    }
    lastdiskah[drivenum] = CPU_AH;
    lastdiskcf[drivenum] = CPU_FL_CF;
    if (CPU_DL & 0x80) RAM[0x474] = CPU_AH;
}