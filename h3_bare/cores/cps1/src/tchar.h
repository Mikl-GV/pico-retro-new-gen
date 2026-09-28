// tchar.h — минимальный шим FBNeo-TCHAR для H3 bare-metal (вендор CPS-1).
// TCHAR = простой char, всё сводится к ANSI C.
#ifndef CPS1_TCHAR_H
#define CPS1_TCHAR_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TCHAR   char
#define _TCHAR  char
#define _T(x)   x

#define _tmain      main
#define _tcslen     strlen
#define _tcsclen    strlen
#define _tcsncpy    strncpy
#define _tcscpy     strcpy
#define _tcscat     strcat
#define _tcsncmp    strncmp
#define _tcscmp     strcmp
#define _tcsstr     strstr
#define _tcsrchr    strrchr
#define _tcsicmp    strcasecmp
#define _tcsnccmp   strncmp
#define _stprintf   sprintf
#define _sntprintf  snprintf
#define _vsntprintf vsnprintf
#define _tprintf    printf
#define _ftprintf   fprintf
#define _ttoi       atoi
#define _ttol       atol

#endif