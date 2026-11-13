#ifndef STREAMS_H
#define STREAMS_H
/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	streams - file input/output library
*/

#include "types.h"

struct _STREAM;
typedef struct _STREAM ISTREAM;
typedef struct _STREAM OSTREAM;
typedef struct _STREAM IOSTREAM;

enum {
	SSEEK_SET, SSEEK_CUR, SSEEK_END
};

ISTREAM*isfopen(const char_t*name);
ISTREAM*isfopen_res(const char_t*rname);
ISTREAM*isopenstdin();
void isclose(ISTREAM*f);
int  isread(ISTREAM*s,void*p,int nb);
int  isseek(ISTREAM*s,long pos,int where);
long istell(ISTREAM*s);

OSTREAM*osfopen(const char_t*name);
OSTREAM*osopenstdout();
int  oswrite(OSTREAM*s,const void*p,int nb);
void osclose(OSTREAM*f);
int  osseek(OSTREAM*s,int pos,int where);

IOSTREAM*iosfopen(const char_t*name);
void iosclose(IOSTREAM*f);
int  iosread(IOSTREAM*s,void*p,int nb);
int  ioswrite(IOSTREAM*s,const void*p,int nb);
int  iosseek(IOSTREAM*s,int pos,int where);

#define WRITE_FIELD(st,fld) oswrite(st,&(fld),sizeof(fld))
#define READ_FIELD(st,fld) isread(st,&(fld),sizeof(fld))
#define WRITE_ARRAY(st,fld) oswrite(st,fld,sizeof(fld))
#define READ_ARRAY(st,fld) isread(st,fld,sizeof(fld))

#endif //STREAMS_H
