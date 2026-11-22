/*
 * cfg.h - Configuration file management header
 *
 * Copyright 1999 Gilles Kohl
 * Copyright 2002 Martin Doering
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#define MAX_PATH  256
#define MAX_LINE 1024
#define MAX_ARGS  50
#define MAX_TGL  1024



typedef struct ConfigEntry ConfigEntry;
typedef struct Config Config;


Config * cfg_new(char *CfgName);
void cfg_del(Config *);

Config * cfg_read(char *MyName);
Config * cfg_read_mycfg(char *MyName);
int  cfg_write(Config*);


void cfg_add_str(Config*, char *Section, char *Key, char *Value);
void cfg_replace_str(Config*, char *Section, char *Key, char *Value);
char *cfg_get_str(Config *, char *Section, char *Key, char *Default);
char * cfg_check_str(Config *, char *, char *);
int cfg_del_str(Config*, char *Section, char *Key, char *Value);

void cfg_add_int(Config*, char *Section, char *Key, int Value);
void cfg_replace_int(Config*, char *Section, char *Key, int Value);
int cfg_get_int(Config*, char *Section, char *Key, int Default);

char *cfg_first_entry(Config*, char *Section, char **pKey);
char *cfg_next_entry(Config*, char **pKey);
char *cfg_first_section(Config*);
char *cfg_next_section(Config*);



