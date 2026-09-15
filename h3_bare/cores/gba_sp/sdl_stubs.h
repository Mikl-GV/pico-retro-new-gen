/* SDL stubs для gpSP на H3 bare-metal.
 * Имитирует минимальное подмножество SDL, которое использует ядро gpSP,
 * чтобы компилировать ядро без изменений. Всё фактическое взаимодействие
 * с железом (клавиатура, экран, таймер) делает gba_h3_host.c. */

#ifndef SDL_STUBS_H
#define SDL_STUBS_H

#include <stdint.h>

typedef struct { void *pixels; int pitch; int w, h; } SDL_Surface;

/* типы событий (не полные, но достаточно для компиляции) */
typedef union {
    struct { int type; } generic;
    struct { int type; struct { unsigned sym; } key; } key;
    struct { int type; struct { unsigned button; } button; } button;
    struct { int type; struct { int axis, value; } axis; } axis;
} SDL_Event;

typedef struct { int type; } SDL_EventType;

typedef enum { SDL_QUIT, SDL_KEYDOWN, SDL_KEYUP, SDL_JOYBUTTONDOWN,
               SDL_JOYBUTTONUP, SDL_JOYAXISMOTION } SDL_event_codes;

typedef void *SDL_mutex;
typedef void *SDL_cond;

typedef struct {
    int freq;
    int format;
    int channels;
    int samples;
    int size;
    void (*callback)(void *userdata, void *stream, int len);
} SDL_AudioSpec;

#define AUDIO_S16 0x8010

#define SDL_Init(flags) 0
#define SDL_Quit() (void)0
#define SDL_ShowCursor(toggle) 0
#define SDL_SetVideoMode(w,h,bpp,flags) ((SDL_Surface*)0)
#define SDL_Flip(screen) (void)0
#define SDL_BlitSurface(src, srcrect, dst, dstrect) (void)0
#define SDL_PollEvent(event) 0
#define SDL_NumJoysticks() 0
#define SDL_JoystickOpen(dev) ((void*)0)
#define SDL_JoystickEventState(state) 0
#define SDL_WM_SetCaption(title, icon) (void)0

#define SDL_CreateMutex() ((SDL_mutex*)0)
#define SDL_CreateCond() ((SDL_cond*)0)
#define SDL_LockMutex(m) (void)0
#define SDL_UnlockMutex(m) (void)0
#define SDL_DestroyMutex(m) (void)0
#define SDL_DestroyCond(c) (void)0
#define SDL_PauseAudio(pause) (void)0
#define SDL_CloseAudio() (void)0
#define SDL_OpenAudio(desired, obtained) (-1)

/* клавиши, используемые в input.c/key_map */
enum {
    SDLK_a = 0x61, SDLK_s, SDLK_d, SDLK_f, SDLK_g, SDLK_h, SDLK_j, SDLK_k,
    SDLK_l, SDLK_z, SDLK_x, SDLK_c, SDLK_v, SDLK_b, SDLK_n, SDLK_m,
    SDLK_RETURN = 0x0D, SDLK_BACKSPACE = 0x08, SDLK_ESCAPE = 0x1B,
    SDLK_DOWN = 0x108, SDLK_UP, SDLK_LEFT, SDLK_RIGHT,
    SDLK_F1 = 0x110, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F7,
    SDLK_BACKQUOTE = '`', SDLK_LSHIFT = 0x1A0, SDLK_RSHIFT, SDLK_LCTRL,
    SDLK_LALT = 0x1A3
};

#endif /* SDL_STUBS_H */