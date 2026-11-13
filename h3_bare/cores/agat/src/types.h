#ifndef TYPES_H
#define TYPES_H
#include <windows.h>
/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	types - base types declarations
*/


typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned long  dword;

#ifdef UNICODE
typedef short char_t;
#define s_len wcslen
#ifndef TEXT
#define TEXT(s) L#s
#endif
#else
typedef char char_t;
#define s_len strlen
#ifndef TEXT
#define TEXT(s) s
#endif
#endif

#ifdef UNDER_CE

#ifndef UNDER_CE3
#define WS_MINIMIZEBOX 0
#define IDC_ARROW 0
#define RUSSIAN_CHARSET 0
#define TextOut(dc,x,y,buf,cnt) ExtTextOut(dc,x,y,0,NULL,buf,cnt,NULL)
#endif // !UNDER_CE3

#endif

#endif //TYPES_H
