/*
 * Castaway
 *  (C) 1994 - 2002 Martin Doering, Joachim Hoenig
 *
 * $File$ - command line parser & system init
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * revision history
 *  23.05.2002  JH  FAST1.0.1 code import: KR -> ANSI, restructuring
 *  09.06.2002  JH  Renamed io.c to st.c again (io.h conflicts with system headers)
 *  19.06.2002  JH  -d option discontinued.
 *  19.09.2002  JH  -h option discontinued.
 *  22.09.2002  JH  -i option: instructions per second
 */
static char     sccsid[] = "$Id: init.c,v 1.7 2003/02/17 18:31:09 lvl Exp $";
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "st.h"
#include "68000.h"
#include "nf.h"

char datadir[80];

int     CommandlineInit(int argc, char **argv)
{
    int i;
    int err = 0;
    for (i = 1; i < argc; i++) {
        if (strcmp ("-4", argv[i]) == 0) {
            display_mode = COL4;
        } else if (strncmp ("-a", argv[i], 2) == 0) {
            if (strlen(argv[i]) == 2) {
                if (i++ == argc) err = 1;
                strncpy (disk[0].name, argv[i], 80);
            } else {
                strncpy (disk[0].name, argv[i] + 2, 80);
            }
        } else if (strncmp ("-b", argv[i], 2) == 0) {
            if (strlen(argv[i]) == 2) {
                if (i++ == argc) err = 1;
                strncpy (disk[1].name, argv[i], 80);
            } else {
                strncpy (disk[1].name, argv[i] + 2, 80);
            }
        } else if (strncmp ("-c", argv[i], 2) == 0) {
            if (strlen(argv[i]) == 2) {
                if (i++ == argc) err = 1;
                strncpy (cartridge, argv[i], 80);
            } else {
                strncpy (cartridge, argv[i] + 2, 80);
            }
        } else if (strncmp ("-i", argv[i], 2) == 0) {
            if (strlen(argv[i]) == 2) {
                if (i++ == argc) err = 1;
                ips = atol (argv[i]);
            } else {
                ips = atol (argv[i] + 2);
            }
        } else if (strncmp ("-m", argv[i], 2) == 0) {
            if (strlen(argv[i]) == 2) {
                if (i++ == argc) err = 1;
                memsize = 1024 * atol (argv[i]);
            } else {
                memsize = 1024 * atol (argv[i] + 2);
            }
        } else if (strncmp ("-r", argv[i], 2) == 0) {
            if (strlen(argv[i]) == 2) {
                if (i++ == argc) err = 1;
                strncpy (rom, argv[i], 80);
            } else {
                strncpy (rom, argv[i] + 2, 80);
            }
        } else {
            err = 1;
        }
    }
    if (err) {
        fprintf (stderr, "Usage[] = %s [-4] [-<a|b> <disk a/b file>]\n", argv[0]);
        fprintf (stderr, "          [-c <cartridge image>] [-m <kbyte>] [-r <rom image>]\n");
        fprintf (stderr, "          [-i <instructions per second (0=unlimited)>]\n");
        exit(1);
    }
    return 0;
}

int     Init(int argc, char **argv)
{
    MemInit();
    FDCInit();
    IOInit();
    nf_init();	/* initialize the native features */
    HWReset(); 	/* CPU Reset */
    return 0;
}
