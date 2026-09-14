#ifndef STATEGUARD_H
#define STATEGUARD_H
/* stubs for state save */
extern void StateSav_SaveUBYTE(const void* v);
extern void StateSav_SaveUWORD(const void* v);
extern void StateSav_SaveINT(const void* v);
extern void StateSav_ReadUBYTE(const void* v, int size);
extern void StateSav_ReadUWORD(const void* v, int size);
extern void StateSav_ReadINT(const void* v, int size);
extern void MEMORY_StateSave(void);
extern void MEMORY_StateRead(void);
#endif