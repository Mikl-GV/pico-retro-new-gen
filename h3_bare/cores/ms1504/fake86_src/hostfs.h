/* Override hostfs.h для порта МС1504 на bare-metal H3.
   Убирает SDL_RWops; диски/файлы на старте не открываются (образы — задача
   следующей итерации); функции сваливаются в fail. */
#ifndef FAKE86_PORT_HOSTFS_H_INCLUDED
#define FAKE86_PORT_HOSTFS_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

typedef struct hh_file HOSTFS_FILE;

extern int hostfs_was_fallback_mode;

/* остаток SDL, который использует disk.c (только сообщения об ошибках) */
const char *SDL_GetError ( void );

extern int         hostfs_init ( void );
extern HOSTFS_FILE *hostfs_open ( const char *fn, const char *mode );
extern HOSTFS_FILE *hostfs_open_with_feedback ( const char *fn, const char *mode, const char *msg );
extern int         hostfs_load_binary ( const char *fn, void *buf, int min_size, int max_size, const char *msg );

/* Операции, которые диск.c звал через SDL_RW*: та же сигнатура. */
#define hostfs_read(a,b,c,d)    ms1504_hfs_read((a),(b),(c),(d))
#define hostfs_write(a,b,c,d)   ms1504_hfs_write((a),(b),(c),(d))
#define hostfs_close(a)         ms1504_hfs_close((a))
#define hostfs_size(a)          ms1504_hfs_size((a))
#define hostfs_seek_set(a,b)    ms1504_hfs_seek((a),(b),0)
#define hostfs_seek_end(a,b)    ms1504_hfs_seek((a),(b),2)
#define hostfs_seek_cur(a,b)    ms1504_hfs_seek((a),(b),1)
#define hostfs_tell(a)          ms1504_hfs_seek((a),0,1)

size_t ms1504_hfs_read  ( HOSTFS_FILE *f, void *p, size_t size, size_t nmemb );
size_t ms1504_hfs_write ( HOSTFS_FILE *f, const void *p, size_t size, size_t nmemb );
int    ms1504_hfs_close ( HOSTFS_FILE *f );
int64_t ms1504_hfs_size ( HOSTFS_FILE *f );
int64_t ms1504_hfs_seek ( HOSTFS_FILE *f, int64_t offset, int whence );

#endif