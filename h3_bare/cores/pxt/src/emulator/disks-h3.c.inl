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

// r1413: потоковый образ (HDD с носителя SD/USB). Образ 256 МБ не влезает в RAM-пул,
// поэтому секторы читаются по требованию через колбэк (fat_read_file).
typedef int (*pxt_disk_read_t)(uint32_t offset, uint8_t* buf, uint32_t len);

struct struct_drive {
    const uint8_t* base;      // RAM-адрес образа (NULL = не вставлен/stream)
    pxt_disk_read_t read_cb;  // !=0: потоковое чтение секторов с носителя
    size_t filesize;          // размер образа в байтах
    uint16_t cyls;
    uint16_t sects;
    uint16_t heads;
    uint8_t inserted;
    uint8_t readonly;
} disk[PXT_DISK_MAX_DRIVES];

// ---- внутренняя механика (идентична win32, но по RAM) ----
static inline void ejectdisk(uint8_t drivenum);

// r1445: геометрия HDD берётся из BPB самого образа (MBR -> раздел -> boot sector).
// DOS считает CHS по BPB, а BIOS переводит его по disk[].sects/heads. При
// расхождении (hdd.img: BPB 63x8, BIOS считал 63x16) все чтения за пределами
// нулевого цилиндра попадают в чужие секторы: DOS читает мусор вместо FAT и
// файлов и падает с Divide overflow. hdd2.img уже 63x16, hdd.img — 63x8.
static int hdd_read_sector(const uint8_t* base, pxt_disk_read_t cb,
                           uint32_t off, uint8_t* buf) {
    if (base) { memcpy(buf, base + off, 512); return 0; }
    return cb ? cb(off, buf, 512) : -1;
}

static void hdd_geometry(uint16_t* sects, uint16_t* heads, uint32_t len,
                         const uint8_t* base, pxt_disk_read_t cb) {
    *sects = 63; *heads = 16;                 // умолчание (как win32-версия)
    uint8_t buf[512];
    uint32_t part = 0;
    if (hdd_read_sector(base, cb, 0, buf) == 0 &&
        buf[510] == 0x55 && buf[511] == 0xAA) {
        for (int i = 0; i < 4; i++) {
            const uint8_t* e = buf + 446 + i * 16;
            if (!e[4]) continue;
            part = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                   ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
            break;
        }
    }
    if (!part || part * 512UL + 512UL > (size_t)len) return;
    if (hdd_read_sector(base, cb, part * 512UL, buf) != 0) return;
    const uint16_t spt = (uint16_t)(buf[24] | ((uint16_t)buf[25] << 8));
    const uint16_t nh = (uint16_t)(buf[26] | ((uint16_t)buf[27] << 8));
    if (spt >= 8 && spt <= 63 && nh >= 1 && nh <= 255) { *sects = spt; *heads = nh; }
}

// ---- конфигурация из pxt_host.c ----
void pxt_disk_set(uint8_t drivenum, const uint8_t* base, size_t len) {
    if (drivenum & 0x80) drivenum -= 126;
    if (drivenum >= PXT_DISK_MAX_DRIVES) return;
    ejectdisk(drivenum);
    disk[drivenum].base = base;
    disk[drivenum].read_cb = 0;
    disk[drivenum].filesize = len;
    disk[drivenum].inserted = (base && len) ? 1 : 0;
    // r1732 (D-09): RAM-образ (флоппи A:) живёт в ROM_BUF; INT13h AH=03 писал
    // в него, но на SD образ после выхода оставался прежним. Честный
    // write-protect вместо «успешной» записи, которая теряется.
    disk[drivenum].readonly = 1;
    if (!disk[drivenum].inserted) return;
    // Геометрия: как win32-версия (флоппи 360К/720К/1.2М, HDD по BPB образа).
    uint16_t cyls = 0, heads = 0, sects = 0;
    if (drivenum >= 2) {        // HDD
        hdd_geometry(&sects, &heads, (uint32_t)len, base, 0);
        cyls = (uint16_t)(len / ((size_t)sects * heads * 512));
    } else {                    // флоппи
        cyls = 80; sects = 18; heads = 2;
        if (len <= 368640UL)      { cyls = 40; sects = 9;  heads = 2; }
        else if (len <= 737280UL) { cyls = 80; sects = 9; }
        else if (len <= 1228800UL){ cyls = 80; sects = 15; }
        else if (len <= 1474560UL){ cyls = 80; sects = 18; }
        else if (len <= 2949120UL){ cyls = 80; sects = 36; }  // r1419: 2.88M ED
    }
    disk[drivenum].cyls = cyls;
    disk[drivenum].heads = heads;
    disk[drivenum].sects = sects;
    if (drivenum >= 2) hdcount++; else fdcount++;
}

// r1413: HDD-образ читается с носителя по секторам (read-only), в RAM не грузится.
void pxt_disk_set_stream(uint8_t drivenum, pxt_disk_read_t cb, uint32_t len) {
    if (drivenum & 0x80) drivenum -= 126;
    if (drivenum >= PXT_DISK_MAX_DRIVES || drivenum < 2) return;
    ejectdisk(drivenum);
    disk[drivenum].base = 0;
    disk[drivenum].read_cb = cb;
    disk[drivenum].filesize = len;
    disk[drivenum].inserted = (cb && len) ? 1 : 0;
    disk[drivenum].readonly = 1;
    if (!disk[drivenum].inserted) return;
    // r1445: геометрия из BPB образа — иначе DOS и BIOS считают CHS по-разному.
    hdd_geometry(&disk[drivenum].sects, &disk[drivenum].heads, len, 0, cb);
    disk[drivenum].cyls = (uint16_t)(len /
        ((size_t)disk[drivenum].sects * disk[drivenum].heads * 512));
    /* r1487 (S5): cyls==0 (образ меньше геометрии) даёт DOS Divide overflow —
       лог, чтобы не выглядело «зависанием». */
    if (disk[drivenum].cyls == 0) {
        extern void emulog_field(const char*, const char*, const char*, ...);
        emulog_field("PXT", "disk", "WARNING: drive %u cyls=0 (len=%u sects=%u heads=%u)",
                     (unsigned)drivenum, (unsigned)len,
                     (unsigned)disk[drivenum].sects, (unsigned)disk[drivenum].heads);
    }
    hdcount++;
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
        disk[drivenum].read_cb = 0;
        if (drivenum >= 2) hdcount--; else fdcount--;
    }
}

static inline size_t chs2ofs(int drivenum, int cyl, int head, int sect) {
    return (((size_t)cyl * (size_t)disk[drivenum].heads + (size_t)head)
            * (size_t)disk[drivenum].sects + (size_t)sect - 1) * 512UL;
}

// r1434: образы pico-xt содержат в MBR неверные CHS-поля разделов: start LBA
// 63, а записано C=1,H=0,S=1 (при 63 sect/16 heads это LBA 1008). DOS читает
// раздел по CHS, попадает в мусор и не создаёт C:. Правим поля на лету под
// фактическую геометрию диска; сам образ не меняется (read-only).
static void fix_mbr_chs(uint8_t* m, uint16_t sects, uint16_t heads) {
    if (!sects || !heads) return;
    const uint32_t per_cyl = (uint32_t)sects * (uint32_t)heads;
    for (int i = 0; i < 4; i++) {
        uint8_t* e = m + 446 + i * 16;
        if (e[4] == 0) continue;
        const uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                             ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        const uint32_t cnt = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                             ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
        const uint32_t end = lba + (cnt ? cnt - 1u : 0u);
        uint32_t c = lba / per_cyl, rem = lba % per_cyl;
        e[1] = (uint8_t)(c & 0xFF);
        e[2] = (uint8_t)(((rem % sects) + 1u) & 0x3F) | (uint8_t)((c >> 2) & 0xC0);
        e[3] = (uint8_t)(rem / sects);
        c = end / per_cyl; rem = end % per_cyl;
        e[5] = (uint8_t)(c & 0xFF);
        e[6] = (uint8_t)(((rem % sects) + 1u) & 0x3F) | (uint8_t)((c >> 2) & 0xC0);
        e[7] = (uint8_t)(rem / sects);
    }
}

static void readdisk(uint8_t drivenum,
              uint16_t dstseg, uint16_t dstoff,
              uint16_t cyl, uint16_t sect, uint16_t head,
              uint16_t sectcount, int is_verify)
{
    // r1444: медленное чтение с носителя — причина «Divide overflow» у DOS
    // (таймер тикает, пока BIOS ждёт сектор), поэтому пишем его в UART.
    extern void emulog_field(const char* tag, const char* field, const char* fmt, ...);
    const uint32_t t0 = (uint32_t)i8253_now_us();
    uint32_t memdest = ((uint32_t)dstseg << 4) + (uint32_t)dstoff;
    if (!disk[drivenum].inserted) { CPU_AH = 0x31; CPU_AL = 0; CPU_FL_CF = 1; return; }
    if (sect == 0 || sect > disk[drivenum].sects || cyl >= disk[drivenum].cyls || head >= disk[drivenum].heads) {
        CPU_AH = 0x04; CPU_AL = 0; CPU_FL_CF = 1; return;
    }
    size_t fileoffset = chs2ofs(drivenum, cyl, head, sect);
    if (fileoffset > disk[drivenum].filesize) { CPU_AH = 0x04; CPU_AL = 0; CPU_FL_CF = 1; return; }
    static uint8_t secbuf[512];   // r1413: один сектор для потокового образа
    for (uint16_t cursect = 0; cursect < sectcount; cursect++) {
        const size_t ofs = fileoffset + (size_t)cursect * 512UL;
        if (ofs + 512UL > disk[drivenum].filesize) {
            CPU_AH = 0x04; CPU_AL = cursect; CPU_FL_CF = 1; return;
        }
        const uint8_t* src;
        if (disk[drivenum].base) {
            src = disk[drivenum].base + ofs;
        } else if (disk[drivenum].read_cb) {
            if (disk[drivenum].read_cb((uint32_t)ofs, secbuf, 512) != 0) {
                CPU_AH = 0x04; CPU_AL = cursect; CPU_FL_CF = 1; return;
            }
            src = secbuf;
        } else {
            CPU_AH = 0x04; CPU_AL = cursect; CPU_FL_CF = 1; return;
        }
        uint8_t mbuf[512];
        if (drivenum >= 2 && cyl == 0 && head == 0 && sect == 1 && cursect == 0) {
            memcpy(mbuf, src, 512);
            fix_mbr_chs(mbuf, disk[drivenum].sects, disk[drivenum].heads);
            src = mbuf;
        }
        if (is_verify) {
            for (int o = 0; o < 512; o++)
                if (read86(memdest++) != src[o]) { CPU_AL = cursect; CPU_FL_CF = 1; CPU_AH = 0xBB; return; }
        } else {
            for (int o = 0; o < 512; o++) write86(memdest++, src[o]);
        }
    }
    CPU_AL = sectcount; CPU_FL_CF = 0; CPU_AH = 0;
    const uint32_t dt = (uint32_t)i8253_now_us() - t0;
    if (dt > 5000u)
        emulog_field("PXT", "slow", "d=%u c=%u h=%u s=%u n=%u us=%u",
                     (unsigned)drivenum, (unsigned)cyl, (unsigned)head,
                     (unsigned)sect, (unsigned)sectcount, (unsigned)dt);
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
    if (disk[drivenum].readonly || !disk[drivenum].base) {
        CPU_AH = 0x03; CPU_AL = 0; CPU_FL_CF = 1; return;
    }
    uint8_t* dst = (uint8_t*)(disk[drivenum].base + fileoffset);
    for (uint16_t cursect = 0; cursect < sectcount; cursect++) {
        if ((size_t)(dst + 512 - disk[drivenum].base) > disk[drivenum].filesize) {
            CPU_AH = 0x04; CPU_AL = 0; CPU_FL_CF = 1; return;
        }
        for (int o = 0; o < 512; o++) dst[o] = read86(memdest++);
        dst += 512;
    }
    // r1413: мёртвая проверка sectcount==0 после цикла убрана; успех — AL=число
    // переданных секторов, CF=0.
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