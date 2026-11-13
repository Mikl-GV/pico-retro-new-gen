#ifndef AGAT_SHIM_TCHAR_H
#define AGAT_SHIM_TCHAR_H

/* r1297: tchar.h для bare-metal сборки ядра Agat (ASCII-ветка, UNICODE не
   определён). Имена _tcs и _stprintf нужны только ядру для строк. */

#include "windows.h"

#define _tcscpy  strcpy
#define _tcsncpy strncpy
#define _tcscat  strcat
#define _tcscmp  strcmp
#define _tcsicmp strcasecmp
#define _stricmp strcasecmp
#define _tcslen  strlen
#define _tstof   atof
#define _ttoi    atoi
#define _stprintf sprintf
#define _tprintf  printf

#endif /* AGAT_SHIM_TCHAR_H */
