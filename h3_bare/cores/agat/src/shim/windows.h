#ifndef AGAT_SHIM_WINDOWS_H
#define AGAT_SHIM_WINDOWS_H

/* r1297: минимальный windows.h для bare-metal сборки ядра Agat Emulator.
   Ядро (cpu/video/fdd/sound/baseram) не использует Win32 API по существу —
   только базовые типы и несколько строковых хелперов. GUI-слой
   (dialogs/msgloop/videow/keyb/sound_acm) в прошивку не входит. */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>

typedef int BOOL;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t UINT;
typedef void* HWND;
typedef void* HDC;
typedef void* HINSTANCE;
typedef void* HBITMAP;
typedef void* HICON;
typedef void* HFONT;
typedef void* HANDLE;
typedef void* HMENU;
typedef void* HMODULE;
typedef void* LPVOID;
typedef uint32_t ATOM;
typedef uintptr_t UINT_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;

typedef struct { uint32_t length; uint32_t width; uint32_t height; } SIZE;
typedef struct { uint32_t length; } WINDOWPLACEMENT;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef long long LONGLONG;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;

typedef char TCHAR;
typedef const char* LPCTSTR;
typedef char* LPTSTR;
typedef const char* LPCSTR;
typedef char* LPSTR;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TEXT
#define TEXT(s) s
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define MAKEINTRESOURCE(x) ((LPCTSTR)(uintptr_t)(x))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define LOWORD(l) ((WORD)((DWORD)(l) & 0xFFFF))
#define HIWORD(l) ((WORD)(((DWORD)(l) >> 16) & 0xFFFF))
#define ZeroMemory(p, n) memset((p), 0, (n))
#define SND_NODEFAULT 0x0002u
#define SND_RESOURCE 0x00040004u
#define SND_ASYNC 0x0001u
#define SND_NOWAIT 0x00002000u
#define SND_NOSTOP 0x00000010u
#define MF_BYCOMMAND 0x00000000u
#define MF_STRING 0x00000000u
#define MF_POPUP 0x00000010u
#define MF_ENABLED 0x00000000u
#define MF_GRAYED 0x00000001u
#define WM_COMMAND 0x0111u
#define MAKEWPARAM(a, b) ((WPARAM)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
#define SW_HIDE 0

/* Ресурсы/звук Windows в прошивке отсутствуют — реализации в agat_hal.c. */
int load_buf_res(int no, void* buf, int len);
BOOL PlaySound(LPCTSTR s, HANDLE h, DWORD f);
BOOL MessageBeep(UINT u);

/* Иконки систем в прошивке не нужны: GDI-объекты отсутствуют. */
static inline HBITMAP LoadBitmap(HANDLE inst, LPCTSTR name)
{
    (void)inst;
    (void)name;
    return NULL;
}
static inline HDC CreateCompatibleDC(HDC dc) { (void)dc; return NULL; }
static inline BOOL DeleteDC(HDC dc) { (void)dc; return TRUE; }
static inline BOOL DeleteObject(HANDLE o) { (void)o; return TRUE; }

/* Win32-вызовы, которых нет в bare-metal (меню, ресурсы, таймеры). */
static inline DWORD GetLastError(void) { return 0; }
static inline int GetWindowText(HWND w, LPTSTR buf, int n)
{
    (void)w;
    if (n > 0) buf[0] = 0;
    return 0;
}
static inline int SetWindowText(HWND w, LPCTSTR s) { (void)w; (void)s; return 1; }
static inline BOOL PostMessage(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    (void)w; (void)m; (void)wp; (void)lp;
    return TRUE;
}
static inline LRESULT SendMessage(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    (void)w; (void)m; (void)wp; (void)lp;
    return 0;
}
static inline BOOL ShowWindow(HWND w, int c) { (void)w; (void)c; return TRUE; }
static inline void UpdateWindow(HWND w) { (void)w; }
static inline BOOL InvalidateRect(HWND w, const RECT* r, BOOL erase)
{
    (void)w; (void)r; (void)erase;
    return TRUE;
}
static inline HWND SetFocus(HWND w) { (void)w; return NULL; }
static inline HWND GetFocus(void) { return NULL; }
static inline HWND SetActiveWindow(HWND w) { (void)w; return NULL; }
static inline HWND GetActiveWindow(void) { return NULL; }
static inline BOOL GetWindowRect(HWND w, RECT* r)
{
    (void)w;
    if (r) { r->left = r->top = 0; r->right = 640; r->bottom = 400; }
    return TRUE;
}
static inline BOOL GetClientRect(HWND w, RECT* r)
{
    (void)w;
    if (r) { r->left = r->top = 0; r->right = 640; r->bottom = 400; }
    return TRUE;
}
static inline BOOL MoveWindow(HWND w, int x, int y, int cx, int cy, BOOL repaint)
{
    (void)w; (void)x; (void)y; (void)cx; (void)cy; (void)repaint;
    return TRUE;
}
static inline BOOL SetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    (void)w; (void)after; (void)x; (void)y; (void)cx; (void)cy; (void)flags;
    return TRUE;
}
static inline BOOL ClientToScreen(HWND w, void* p) { (void)w; (void)p; return TRUE; }
static inline BOOL ScreenToClient(HWND w, void* p) { (void)w; (void)p; return TRUE; }
static inline int GetSystemMetrics(int i) { (void)i; return 640; }
static inline HMODULE GetModuleHandle(LPCTSTR name) { (void)name; return NULL; }
static inline BOOL DeleteMenu(HMENU m, UINT pos, UINT flags) { (void)m; (void)pos; (void)flags; return TRUE; }
static inline BOOL UnionRect(RECT* dst, const RECT* a, const RECT* b){
    dst->left = a->left < b->left ? a->left : b->left;
    dst->top = a->top < b->top ? a->top : b->top;
    dst->right = a->right > b->right ? a->right : b->right;
    dst->bottom = a->bottom > b->bottom ? a->bottom : b->bottom;
    return TRUE;
}
static inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* f) { if (f) f->QuadPart = 1000000; return TRUE; }
static inline BOOL SetRectEmpty(RECT* r) { r->left = r->top = r->right = r->bottom = 0; return TRUE; }

/* Выбор образа диска: в прошивке путь задаёт ROM-браузер, диалог не нужен. */
static inline int select_disk(HWND wnd, char* name, int* readonly)
{
    (void)wnd; (void)name; (void)readonly;
    return 0;
}
static inline HMENU CreatePopupMenu(void) { return NULL; }
static inline BOOL AppendMenu(HMENU m, UINT flags, UINT id, LPCTSTR s)
{
    (void)m; (void)flags; (void)id; (void)s;
    return TRUE;
}
static inline BOOL EnableMenuItem(HMENU m, UINT id, UINT flags)
{
    (void)m; (void)id; (void)flags;
    return TRUE;
}
static inline int wsprintf(LPTSTR buf, LPCTSTR fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

/* Вызовные соглашения MSVC на ARM не нужны. __declspec(thread) в bare-metal
   не поддержан: CPU работает в одном потоке (см. адаптацию cpu.c). */
#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef __declspec
#define __declspec(x)
#endif

static inline int lstrcmpi(const char* a, const char* b) { return strcasecmp(a, b); }
#define lstrcat strcat
#define lstrcpy strcpy

static inline char* _tcsdup(const char* s)
{
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static inline void SetParent(HWND parent, HWND child) { (void)parent; (void)child; }
static inline DWORD GetTickCount(void) { return 0; }
static inline void Sleep(DWORD ms) { (void)ms; }

/* r1297: однопоточное исполнение CPU Агата на core0 — атомарность не нужна. */
static inline LONG InterlockedExchange(volatile LONG* p, LONG v)
{
    LONG old = *p;
    *p = v;
    return old;
}

#endif /* AGAT_SHIM_WINDOWS_H */
