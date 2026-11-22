/*
 * Castaway
 *  (C) 1994 - 2002 Martin Doering, Joachim Hoenig
 *
 * fdc.c - wd1772/dma emulation
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * revision history
 *  23.05.2002  JH  FAST1.0.1 code import: KR -> ANSI, restructuring
 *  09.06.2002  JH  Renamed io.c to st.c again (io.h conflicts with system headers)
 *  29.10.2002  JH  Second guess disk boot sector information
 *                  (thx to schtruck@@users.sourceforge.net)
 */
static char     sccsid[] = "$Id: fdc.c,v 1.2 2002/06/08 23:31:58 jhoenig Exp $";
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include "st.h"
#include "cw_host.h"    /* r1566: cw_disk_open/read/size (FAT) */

/*
 * FDC Registers
 */
unsigned char   fdc_data, fdc_track, fdc_sector, fdc_status, fdc_command;
unsigned char   fdc_int = 0;
struct Disk     disk[2] = {
    {
        NULL, DISKA, 0, SIDES, TRACKS, SECTORS, SECSIZE
    },
    {
        NULL, DISKB, 0, SIDES, TRACKS, SECTORS, SECSIZE
    },
};

void            FDCInit(void)
{
    unsigned char  *buf;
    int             i;
    buf = (unsigned char *) malloc (512);
    for (i = 0; i < 2; i++) {
        /* r1566: образы читаются через FAT (cw_disk_*), а не stdio.
         * Пустой дисковод — норма (B: часто отсутствует), не exit(1). */
        if (cw_disk_open(i, disk[i].name) != 0) {
            continue;
        }
        if (512 != cw_disk_read(i, 0, buf, 512)) {
            continue;
        }
        disk[i].head = 0;
        disk[i].sides = (int) *(buf + 26);
        disk[i].sectors = (int) *(buf + 24);
        disk[i].secsize = (int) (*(buf + 12) << 8) | *(buf + 11);
        disk[i].tracks = (int) ((*(buf + 20) << 8) | *(buf + 19)) /
            (disk[i].sectors * disk[i].sides);
		/*
         * Second guess the boot sector information.
         * (thx to schtruck@@users.sourceforge.net)
         */
        {
            int len, sides;
    		            /* r1566: fseek больше не нужен — размер берём из FAT */
            len = cw_disk_size(i);
	    	if (len > (500*1024)) {
		    	 sides = 2;
    		} else {
	    		 sides = 1;
		    }
            if (disk[i].sides != sides) {
                disk[i].sides = sides;
                if (!(((len/disk[i].sides)/512)%9)&&(((len/disk[i].sides)/512)/9)<86) disk[i].sectors=9;
                else if (!(((len/disk[i].sides)/512)%10)&&(((len/disk[i].sides)/512)/10)<86) disk[i].sectors=10;
                else if (!(((len/disk[i].sides)/512)%11)&&(((len/disk[i].sides)/512)/11)<86) disk[i].sectors=11;
                else if (!(((len/disk[i].sides)/512)%12)) disk[i].sectors=12;
                disk[i].tracks =((len/disk[i].sides)/512)/disk[i].sectors;
            }
        }
        /* r1566: лог диска убран — в релизе UART молчит (правило владельца). */
    }
    free (buf);
}

void            FDCCommand(void)
{
    static char     dir = 1, motor = 1;
    int             sides, drives;
    long            address;    /* dma target/source address */
    long            offset;     /* offset in disk file */
    unsigned long   count;      /* number of byte to transfer */
    char           *buffer;

    /* DMA target/source address */
    address = (dma_adrh << 16) + (dma_adrm << 8) + dma_adrl;
    buffer = membase + address;
    /* status of side select and drive select lines */
    sides = (~snd_regs[14]) & 0x1;
    drives = (~snd_regs[14]) & 0x6;
    switch (drives) {
    case 2:                     /* Drive A */
        drives = 0;
        break;
    case 4:                     /* Drive B */
        drives = 1;
        break;
    case 6:                     /* both, error */
    case 0:                     /* no drive selected */
        drives = -1;
        break;
    }
    fdc_status = 0;             /* clear fdc status */
    if (fdc_command < 0x80) {   /* TYPE-I fdc commands */
        if (drives >= 0) {      /* drive selected */
            switch (fdc_command & 0xf0) {
            case 0x00:          /* RESTORE */
                disk[drives].head = 0;
                fdc_track = 0;
                break;
            case 0x10:          /* SEEK */
                disk[drives].head += (fdc_data - fdc_track);
                fdc_track = fdc_data;
                if (disk[drives].head < 0
                        || disk[drives].head >= disk[drives].tracks)
                    disk[drives].head = 0;
                break;
            case 0x30:          /* STEP */
                fdc_track += dir;
            case 0x20:
                disk[drives].head += dir;
                break;
            case 0x50:          /* STEP-IN */
                fdc_track++;
            case 0x40:
                if (disk[drives].head < disk[drives].tracks)
                    disk[drives].head++;
                dir = 1;
                break;
            case 0x70:          /* STEP-OUT */
                fdc_track--;
            case 0x60:
                if (disk[drives].head > 0)
                    disk[drives].head--;
                dir = -1;
                break;
            }
            if (disk[drives].head == 0) {
                fdc_status |= 0x4;
            }
            if (disk[drives].head != fdc_track && fdc_command & 0x4) {  /* Verify? */
                fdc_status |= 0x10;
            }
            if (motor) {
                fdc_status |= 0x20;     /* spin-up flag */
            }
        } else {                /* no drive selected */
            fdc_status |= 0x10;
        }
    } else if ((fdc_command & 0xf0) == 0xd0) {  /* FORCE INTERRUPT */
        if (fdc_command == 0xd8) {
            fdc_int = 1;
        } else if (fdc_command == 0xd0) {
            fdc_int = 0;
        }
    } else {                    /* OTHERS */
        if (drives >= 0) {      /* drive selected */
            /* offset within floppy-file */
            offset = disk[drives].secsize *
                (((disk[drives].sectors * disk[drives].sides * disk[drives].head))
                 + (disk[drives].sectors * sides) + (fdc_sector - 1));
            switch (fdc_command & 0xf0) {
            case 0x80:          /* READ SECTOR */
                count = dma_scr * 512;
                if (!0 /* r1566: смещение передаётся в cw_disk_read напрямую */) {
                    if (count == cw_disk_read(drives, (uint32_t)offset, buffer, (uint32_t)count)) {
                        address += count;
                        dma_adrl = address & 0xff;
                        dma_adrm = (address >> 8) & 0xff;
                        dma_adrh = (address >> 16) & 0xff;
                        dma_scr = 0;
                        dma_sr = 1;
                        break;
                    }
                }
                fdc_status |= 0x10;
                dma_sr = 1;
                break;
            case 0x90:          /* READ SECTOR multiple */
                count = dma_scr * 512;
                if (!0 /* r1566: смещение передаётся в cw_disk_read напрямую */) {
                    if (count == cw_disk_read(drives, (uint32_t)offset, buffer, (uint32_t)count)) {
                        address += count;
                        dma_adrl = address & 0xff;
                        dma_adrm = (address >> 8) & 0xff;
                        dma_adrh = (address >> 16) & 0xff;
                        dma_scr = 0;
                        dma_sr = 1;
                        fdc_sector += dma_scr * (512 / disk[drives].secsize);
                        break;
                    }
                }
                fdc_status |= 0x10;
                dma_sr = 1;
                break;
            case 0xa0:          /* WRITE SECTOR */
                count = dma_scr * 512;
                if (!0 /* r1566: смещение передаётся в cw_disk_read напрямую */) {
                    if (count == (size_t)-1 /* r1566: запись на SD не поддерживаем */) {
                        address += count;
                        dma_adrl = address & 0xff;
                        dma_adrm = (address >> 8) & 0xff;
                        dma_adrh = (address >> 16) & 0xff;
                        dma_scr = 0;
                        dma_sr = 1;
                        break;
                    }
                }
                fdc_status |= 0x10;
                dma_sr = 1;
                break;
            case 0xb0:          /* WRITE SECTOR multiple */
                count = dma_scr * 512;
                if (!0 /* r1566: смещение передаётся в cw_disk_read напрямую */) {
                    if (count == (size_t)-1 /* r1566: запись на SD не поддерживаем */) {
                        address += count;
                        dma_adrl = address & 0xff;
                        dma_adrm = (address >> 8) & 0xff;
                        dma_adrh = (address >> 16) & 0xff;
                        dma_scr = 0;
                        dma_sr = 1;
                        fdc_sector += dma_scr * (512 / disk[drives].secsize);
                        break;
                    }
                }
                fdc_status |= 0x10;
                dma_sr = 1;
                break;
            case 0xc0:          /* READ ADDRESS */
                fdc_status |= 0x10;
                break;
            case 0xe0:          /* READ TRACK */
                fdc_status |= 0x10;
                break;
            case 0xf0:          /* WRITE TRACK */
                fdc_status |= 0x10;
                break;
            }
            if (disk[drives].head != fdc_track) {
                fdc_status |= 0x10;
            }
        } else {
            fdc_status |= 0x10; /* no drive selected */
        }
    }
    if (motor)
        fdc_status |= 0x80;     /* motor on flag */
    if (!(fdc_status & 0x01)) { /* not busy */
        mfp_iprb |= (0x80 & mfp_ierb);  /* Request Interrupt */
        mfp_gpip &= ~0x20;
        SetInterruptPriority ();
    }
}
