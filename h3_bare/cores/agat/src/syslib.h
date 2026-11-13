/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	syslib - system-specific functions
*/


#ifdef UNDER_X
#define xsprintf sprintf
#define xvsprintf vsprintf
#define getsysmsg(code,buf,sz) 	(strcpy(buf,"Unknown error"))
#define FAILED(code) ((code)<0)
#else
#include <windows.h>
#define xsprintf wsprintf
#define xvsprintf wvsprintf
#define getsysmsg(code,buf,sz) 	(FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,code,MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),buf,sz,NULL))
#endif //UNDER_X


#define memzero(p,sz) (memset(p,0,sz))

