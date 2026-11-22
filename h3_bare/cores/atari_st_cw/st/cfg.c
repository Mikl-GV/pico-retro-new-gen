/*
 * cfg.c - Configuration file management routines
 *
 * Copyright 1999 Gilles Kohl
 * Copyright 2002 Martin Doering
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "cfg.h"

#define DEBUG 0         /* 0, if no debugging output wanted */

#define REPLACE 0x01
#define ADD     0x02

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif



struct ConfigEntry {
   struct ConfigEntry *prev;
   struct ConfigEntry *next;
   char *section;
   char *key;
   char *Value;
   int LineNbr;
};



struct Config {
    char * name;
    ConfigEntry *firstentry;
    ConfigEntry *lastentry;
    ConfigEntry *tmpentry;
    ConfigEntry *tmpsentry;
    char * tmpsection;     /* used for first/next searching */
};



/*
 * strip_space - Remove white space from start and end of string
 */
static char *
strip_space(char *s)
{
    char *e;

    for (; isspace(*s); ++s)
        ;
    if (!*s)
        return s;
    for (e = s + strlen(s)-1; e > s && isspace(*e); --e)
        *e = '\0';
    return s;
}



/*
 * strip_quotes - Remove quotes from string if present
 */
static char *
strip_quotes(char *s)
{
    char *q;

    if (strlen(s) < 2)
        return s;
    q = s + strlen(s) - 1;
    if (*s == '\'' && *q == '\'') {
        ++s;
        *q = '\0';
    }
    return s;
}

/*
 * cfg_add_entry - Add an entry to the config list
 *
 * All three char pointers given to this function must point to a valid
 * memory address! The memory for this needs to be malloced before.
 */
static void
cfg_add_entry(Config * cfg, char *section, char *key, char *Value, int Line)
{
    ConfigEntry *entry;

    entry = (ConfigEntry *)malloc(sizeof(ConfigEntry));
    if (entry == NULL) {
        fprintf(stderr, "out of mem in cfg_add_entry\n");
        exit(-1);
    }
    /* make copies in memory to, be save...*/
    entry->section = section;
    entry->key     = key;
    entry->Value   = Value;
    entry->LineNbr = Line;
    entry->next    = NULL;

    /* Is this the start of the list */
    if (cfg->firstentry == NULL) {
        cfg->firstentry = entry;
        cfg->firstentry->prev = NULL;
    }
    /* Add a node, and link nodes */
    else {
        cfg->lastentry->next = entry;
        entry->prev = cfg->lastentry;
    }

    /* Nice to have a pointer to the end of the list */
    cfg->lastentry = entry;
}



/*
 * cfg_insert_entry - Insert a new entry
 *
 * Builds a config entry, inserts it After entry, adjusts line numbers.
 */
static void
cfg_insert_entry(Config * cfg, char *section, char *key, char *Value,
                       ConfigEntry *entry)
{
    ConfigEntry *pNew;

    pNew = (ConfigEntry *)malloc(sizeof(ConfigEntry));
    if (pNew  == NULL ) {
        fprintf(stderr, "out of mem in cfg_insert_entry\n");
        exit(-1);
    }

    /* Slide this new entry after entry */
    pNew->prev = entry;
    pNew->next = entry->next;

    if (entry->next) {
        entry->next->prev = pNew;
    }
    entry->next = pNew;

    /* Fill in its data */

    /*
     * We only want to allocate a new section string if this is a new section
     * entry. This is determined by comparing the section and the value. If they
     * are the same we have a new section. If not, use the section of the
     * entry passed in.
     */
    if (strcmp(section,Value) == 0) {
        pNew->section = strdup(section);
    } else {
        pNew->section = entry->section;
    }

    if ((pNew->key = strdup(key))== NULL) {
        fprintf(stderr, "out of mem in cfg_insert_entry\n");
        exit(-1);
    }

    if ((pNew->Value = strdup(Value))== NULL) {
        fprintf(stderr, "out of mem in cfg_insert_entry\n");
        exit(-1);
    }

    pNew->LineNbr = entry->LineNbr + 1;

    /* Adjust last entry pointer
     */
    if (entry == cfg->lastentry){
        cfg->lastentry = pNew;
    }

    /* Update following line numbers
     */
    pNew = pNew->next;
    while (pNew) {
        pNew->LineNbr++;
        pNew = pNew->next;
    }
}



/*
 * cfg_new -Initialize structures for a new in-memory config file
 */
Config *
cfg_new(char *CfgName)
{
    Config * self;

    self = malloc(sizeof(Config));

    self->name = strdup(CfgName);
    self->firstentry  = NULL;
    self->lastentry  = NULL;
    self->tmpentry = NULL;
    self->tmpsentry = NULL;
    self->tmpsection = NULL;     /* used for first/next searching */

    return self;
}



/*
 * cfg_read - Read a configuration file into memory.
 */
Config *
cfg_read(char *CfgName)
{
    Config * self;
    FILE *f;
    int Line = 0;
    char buf[MAX_LINE];
    char *s, *p;
    char *CurSection;

    f = fopen(CfgName, "r");
    if (f == NULL)
        return NULL;

#if DEBUG
    fprintf(stderr, "DEBUG: cfg_read, CfgName : %s\n", CfgName);
#endif

    self = cfg_new(CfgName);

    CurSection = NULL;
    while( (s = fgets(buf, sizeof(buf), f)) ) {
        ++Line;

        s = strip_space(s);
#if DEBUG
        fprintf(stderr, "DEBUG: cfg_read, s: %s\n", s);
#endif

        /* New Section found */
        if (*s == '[' && s[strlen(s)-1] == ']') {
            ++s;
            s[strlen(s)-1] = '\0';
            CurSection = strdup(s);
        }

        if ( (p = strchr(s, '=')) ) { /* found assignment ? */
            *p++ = '\0'; /* insert separator */
            p = strip_quotes(strip_space(p));
            s = strip_space(s);
        } else {
            p = strip_quotes(strip_space(s));
            s = "";
        }
        cfg_add_entry(self, CurSection, strdup(s), strdup(p), Line);
    }
    fclose(f);
    return self;
}



/*
 * cfg_read_mycfg - Read config
 *
 * It is given programs name (usually argv[0])
 */
Config *
cfg_read_mycfg(char *MyName)
{
    Config * self;
    char DefPath[MAX_PATH];
    char * home;

    home = getenv("HOME");
    if (home != NULL) {
        /* Use $HOME/.* for configuration */
        sprintf(DefPath, "%s/.%s.ini", home, MyName);
    }
    else {
        fprintf(stderr, "ERROR: cfg_read_mycfg, no File DefPath: %s\n", DefPath);
        exit(1);
    }
#if 0
    if (home != NULL) {
        /* Use $HOME/.cfg dir for configuration */
        sprintf(DefPath, "%s/.ini/%s.ini", home, MyName);
    } else {
        /* Use /etc/pal dir for configuration */
        sprintf(DefPath, "/etc/ini/%s.ini", MyName);
    }
#endif
    self = cfg_read(DefPath);
#if DEBUG
    fprintf(stderr, "DEBUG: cfg_read_mycfg, DefPath: %s\n", DefPath);
#endif
    return self;
}



/*
 * cfg_get_str - Return pointer to value of given Config entry.
 *
 * If not found, return def.
 */

char *
cfg_get_str(Config * cfg, char *section, char *key, char *Default)
{
    ConfigEntry *entry;

    for(entry = cfg->firstentry; entry; entry = entry->next) {

        if (entry->section != NULL) {
            assert(entry != NULL);
            assert(entry->section != NULL);
            assert(entry->key != NULL);
            if (strcmp(section, entry->section) == 0 && strcmp(key, entry->key) == 0)
                return entry->Value;
        }
        else {
            if (section == NULL && strcmp(key, entry->key) == 0)
                return entry->Value;
        }
    }
    return Default;
}



/*
 * DiscardList -  Free memory used by config list structures.
 */
void
cfg_del(Config * self)
{
    char *HSection;
    ConfigEntry *Ptr, *Ptr2;

    if (self->firstentry == NULL)
        return;

    Ptr = self->firstentry;
    HSection = NULL;

    do {
        /* If new section comes, free it's memory */
        if ( Ptr->section != HSection ) {
            HSection = Ptr->section;
            free(HSection);
        }
        free(Ptr->Value);
        free(Ptr->key);
        Ptr2 = Ptr->next;
        free(Ptr);
        Ptr = Ptr2;
    } while (Ptr != NULL);

    free(self);
}



/*
 * cfg_mod_str - Adds or changes an entry in a config section
 *
 * Adds a key=Value pair to the specified Section.
 *
 * If flag = ADD then the pair is added as the last pair for that section.
 * If flag = REPLACE then all Keys for the section are searched and the
 *           first match found is replaced.
 * In all cases if the section does not exist, it is created.
 */
void
cfg_mod_str(Config * cfg, char *section, char *key, char *Value, int flag )
{
    ConfigEntry *entry = cfg->firstentry;
    int fFound = FALSE;
    int fDone = FALSE;

    /* Go to start of given section */
    while (entry) {
        if (strcmp(section,entry->section) == 0) {
            fFound = TRUE;
            break;
        }
        entry = entry->next;
    }

    if (fFound) {
        /*
         * Section Found. Find start of next section, if we pass by the
         * key, and if we are in replace mode then replace it.
         */
        while (entry && !fDone) {
            if (strcmp(section,entry->section) != 0){
                break;
            }
            /* found the key */
            if (strcmp(key, entry->key) == 0 && flag & REPLACE) {
                if (entry->Value)
                    free(entry->Value);
                entry->Value = strdup(Value);
                fDone = TRUE;
            }
            entry = entry->next;
        }
        /*
         * We are either at the very end or the next section or we replaced
         * the Value for a key. If we replaced a Key, we are done. If not,
         * we need to back up to the last non-blank line, add the key.
         */
        if (!fDone) {
            /* if we are past the end back up */
            if (!entry) {
                entry = cfg->lastentry;
            }

            /* Walk back to a non blank key */
            //FIXED: have to look, if we are at the start of list!!!
            while (strlen(entry->key) == 0 && entry != cfg->firstentry) {
                entry = entry->prev;
            }

            /* Insert the new key */
            cfg_insert_entry(cfg, section, key, Value, entry);
        }
    }
    else {
        /* Need to create new section */
        cfg_insert_entry(cfg, section, "", section, cfg->lastentry);
        cfg_insert_entry(cfg, section, key, Value, cfg->lastentry);
    }
}

/*
 * cfg_add_str
 *
 * This function will add a new key=value pair to the currently opened
 * config file. It will search for the Section provided and if found add
 * the new pair as the last entry in that section. If the Section dows
 * not exist, it will be created and the entry added to it.
 *
 * cfg_add_str does not check to see if there is another key of the
 * same name already there. This allows multiple values with the same
 * key. If you need to just replace the value for a key use
 * ReplaceConfigString.
 */
void
cfg_add_str(Config * cfg, char *section, char *key, char *Value )
{
    cfg_mod_str(cfg, section, key, Value, ADD );
}

/*
 * cfg_replace_str
 *
 * This function will replace the value from an existing key=value pair
 * if it exists in a config file. If the key does not exist it will be
 * added similar to AddConfigString().
 */
void
cfg_replace_str(Config * cfg, char *section, char *key, char *Value )
{
    cfg_mod_str(cfg, section, key, Value, REPLACE );
}

/*
 * cfg_add_int
 *
 * This function will add a new key=value pair to the currently opened
 * config file. It will search for the Section provided and if found add
 * the new pair as the last entry in that section. If the Section dows
 * not exist, it will be created and the entry added to it.
 *
 * cfg_add_int does not check to see if there is another key of the
 * same name already there. This allows multiple values with the same
 * key. If you need to just replace the value for a key use
 * ReplaceConfigString.
 */
void
cfg_add_int(Config * cfg, char *section, char *key, int Value )
{
    char buf[20];
    sprintf(buf, "%i", Value);
    cfg_mod_str(cfg, section, key, buf , ADD );
}
/*
 * cfg_replace_int
 *
 * This function will replace the value from an existing key=value pair
 * if it exists in a config file. If the key does not exist it will be
 * added similar to AddConfigString().
 */
void
cfg_replace_int(Config * cfg, char *section, char *key, int Value )
{
   char buf[20];
   sprintf(buf, "%i", Value);
   cfg_mod_str(cfg, section, key, buf, REPLACE );
}

/*
 * cfg_get_int - Return int value of a config entry
 *
 * Return a default value if key is not found
 */
int
cfg_get_int(Config * cfg, char *section, char *key, int Default)
{
    char *s;
    char *p;
    long val;

    s = cfg_get_str(cfg, section, key, "NAN");
    val = strtol(s, &p, 0);
    if (strchr(" \t;", *p))
        return val;
    return Default;
}

/*
 * cfg_get_line - Return line number of last entry got via GetFirst/NextEntry
 */
int
cfg_get_line(Config * self)
{
    if (self->tmpentry == NULL)
        return 0;
    return self->tmpentry->LineNbr;
}

/*
 * cfg_renumber_lines
 *
 * Since we may have to delete many entries it is not efficient to
 * renumber after each delete, so this is called after last delete.
 */
void
cfg_renumber_lines(Config * self)
{
    ConfigEntry *entry = self->firstentry;
    int i;

    for (i = 1; entry; entry = entry->next, i++) {
        entry->LineNbr = i;
    }
}

/*
 * cfg_del_entry - Deletes a config entry.
 */
static void
cfg_del_entry(Config * self, ConfigEntry *entry)
{
    /* Is this the start of the list? */
    if (entry != self->firstentry ) {
        /* Adjust earlier entry's next Pointer */
        entry->prev->next = entry->next;
    }

    /* Adjust next entry's prev Pointer if it exists */
    if (entry != self->lastentry) {
        entry->next->prev = entry->prev;
    }

#if 0 //FIXME: Handle, if Entry is the last AND first
    /* If we deleted the first entry, reset lastentry */
    if (entry->prev == NULL) {
        self->firstentry = entry->prev; //FIXED, was entry
    }
#endif

    /* If we deleted the last entry, reset lastentry */
    if (entry->next == NULL) {
        self->lastentry = entry->prev; //FIXED, was entry
    }

    /*
     * free strings and structure. Be careful not to call this routine for
     * the section entry before deleting ALL the entries in the section.
     */
    if (strcmp(entry->section, entry->Value ) == 0) {
        free(entry->section);
    }
    free(entry->key);
    free(entry->Value);
    free(entry);
}

/*
 * cfg_del_str - Deletes a ConfigEntry
 *
 * Note: section must be provided and found or nothing is done.
 *       If section is found then the action depends on key.
 *       If key is NULL then the complete section is removed.
 *       If key is provided and is not found nothing is done.
 *       If key is found then the action depends on Value.
 *       If Value is NULL then all Matching keys are deleted.
 *       If Value is provided and it is not found then nothing is done.
 *       If Value is provided and found it is deleted.
 *
 * Returns: Number of lines/entries deleted.
 */
int
cfg_del_str(Config * self, char *section, char *key, char *Value)
{
    ConfigEntry *entry = self->firstentry;
    ConfigEntry *pTmp, *pSection;
    int fFound = FALSE;
    int iDeleted = 0;

    if (!section)
        return(iDeleted);

    /* Find start of this section */
    while (entry) {
        if (strcmp(section,entry->section) == 0) {
            fFound = TRUE;
            break;
        }
        entry = entry->next;
    }
    
    /* section Not Found */
    if (!fFound)
        return(iDeleted);

    /* No key so delete all entries in section */
    if (!key) {
        /* Save the section entry for later */
        pSection = entry;
        entry = entry->next;

        while(entry && entry->section && strcmp(entry->section, section)==0) {
            pTmp = entry;
            entry = entry->next;
            cfg_del_entry(self, pTmp);
            iDeleted++;
        }

        /* Now delete the section entry */
        cfg_del_entry(self, pSection);
        cfg_renumber_lines(self);
        return(++iDeleted);
    }

    /* If we get here we have a section and a key */
    while( entry && strcmp( section, entry->section ) == 0 ) {
        if ( (strcmp( key, entry->key ) == 0) &&
             ((strcmp( Value, entry->Value ) == 0 ) || !Value)) {

            pTmp = entry;
            entry = entry->next;
            cfg_del_entry(self, pTmp);
            iDeleted++;
        } else {
            entry = entry->next;
        }
    }
    cfg_renumber_lines(self);
    return(iDeleted);
}

/*
 * cfg_first_entry - Used to search entire sections.
 *
 * Initiates search.
 */
char *
cfg_first_entry(Config * self, char *section, char **pkey)
{
    ConfigEntry *entry;

    for(entry = self->firstentry; entry; entry = entry->next) {
        if (entry->section) {
            if (strcmp(section, entry->section) == 0 && (pkey) && strlen(entry->key)) {
                self->tmpentry   = entry;
                self->tmpsection = section;
                if (pkey)
                    *pkey = entry->key;
                return entry->Value;
            }
        }
    }
    self->tmpentry   = NULL;
    self->tmpsection = NULL;
    return NULL;
}

/*
 * cfg_next_entry - Search entire section
 *
 * Used to search entire sections. Continuous search.
 */
char *
cfg_next_entry(Config * self, char **pkey)
{
    do {
        /*
         * Return NULL if the current entry, the current section, the next entry
         * is NULL or the next section no longer matches
         */
        if (self->tmpentry == NULL || self->tmpsection == NULL
            || self->tmpentry->next == NULL ||
           strcmp(self->tmpentry->next->section, self->tmpsection) != 0) {
            self->tmpentry   = NULL;
            self->tmpsection = NULL;
            return NULL;
        }

        /* Since we have a next entry point to it */
        self->tmpentry = self->tmpentry->next;

        /* The section must match, but it might be a comment or blank line */
        if ((pkey) && (strlen(self->tmpentry->key) != 0)) {
            *pkey = self->tmpentry->key;
            return self->tmpentry->Value;
        }
    } while( strcmp(self->tmpentry->next->section, self->tmpsection) == 0);

    /* if we get here the section changed. */
    return NULL;
}



/*
 * GetSectionEntry - Search entire config file for sections.
 *
 * Initiates search.
 */
char *
cfg_first_section( Config * self )
{
    ConfigEntry *entry;

    for(entry = self->firstentry; entry; entry = entry->next) {
        /* New section */
        if (strcmp(entry->section, entry->Value) == 0 &&
           strlen(entry->key) == 0) {

            self->tmpsentry   = entry;
            return entry->section;
        }
    }
    self->tmpsentry = NULL;
    return NULL;
}



/*
 * cfg_next_section - search entire config file for sections.
 *
 * Continues search.
 */
char *
cfg_next_section( Config * self )
{
    ConfigEntry *entry;

    for(entry = self->tmpsentry->next; entry; entry = entry->next) {
        /* New section */
        if (strcmp(entry->section, entry->Value) == 0 &&
           strlen(entry->key) == 0) {

            self->tmpsentry   = entry;
            return entry->section;
        }
    }
    self->tmpsentry = NULL;
    return NULL;
}



/*
 * cfg_write - Writes current configuration from memory to file.
 */
int
cfg_write(Config * self)
{
    FILE *f;
    char buf[MAX_LINE];

    ConfigEntry *entry = self->firstentry;

    f = fopen(self->name, "w");
    if (f == NULL)
        return 0;

    while(entry) {
        if (strlen(entry->key)) {
            /* Handle a valid key */
            sprintf(buf,"%s=%s\n",entry->key, entry->Value);
        } else {
            if (strcmp(entry->section, entry->Value) == 0) {
                /* New section found */
                sprintf(buf,"[%s]\n",entry->section);
            } else {
                /* Handle comments & blank lines */
                sprintf(buf,"%s\n",entry->Value);
            }
        }
        if (fputs(buf, f) == EOF){
            return 0;
        }
        entry = entry->next;
    }
    fclose(f);
    return 1;
}



char *
cfg_check_str(Config * self, char * section, char * key)
{
    char * val;

    val = cfg_get_str(self, section, key, NULL);
    if ( val == NULL) {
        printf("No key '%s' specified in [%s] section! Exiting...\n", key, section);
        exit (1);
    }

    return val;
}
