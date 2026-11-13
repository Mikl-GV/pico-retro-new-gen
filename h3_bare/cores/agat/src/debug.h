/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	debug - output of debug information
*/
#ifndef DEBUG_H
#define DEBUG_H
#include "types.h"


void debug_init();
void debug_term();

void logprint(int l,const char_t*fmt,...);
void errprint(const char_t*fmt,...);

int DD_FAILED(int code,const char_t*fmt,...);
int DS_FAILED(int code,const char_t*fmt,...);
int ACM_FAILED(int code,const char_t*fmt,...);
int IC_FAILED(int code,const char_t*fmt,...);

int WIN_ERROR(int err,const char_t*fmt,...);

#endif //DEBUG_H
