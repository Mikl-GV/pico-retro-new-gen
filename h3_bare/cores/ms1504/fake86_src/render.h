/* Override render.h для порта МС1504 (fake86) на bare-metal H3 —
   убирает зависимость от SDL. Реальный рендер — в ms1504_host.c. */
#ifndef FAKE86_PORT_RENDER_H_INCLUDED
#define FAKE86_PORT_RENDER_H_INCLUDED

#include <stdint.h>

/* MS1504-порт: плейсхолдер пиксельного формата (в оригинале SDL_PixelFormat).
   Сдвиги имитируют RGB888 (R=16, G=8, B=0, A=24) — DAC-палитра VGA. */
struct ms1504_pix_fmt { int Rshift, Gshift, Bshift, Ashift; };
extern struct ms1504_pix_fmt *sdl_pixfmt;

extern uint8_t  renderbenchmark;
extern uint8_t  scrmodechange;
extern uint32_t  framedelay;
extern uint64_t  totalframes;
extern uint8_t  noscale, nosmooth;

extern int      sdl_error        ( const char *msg );
extern void     sdl_shutdown     ( void );
extern int      initscreen       ( const char *ver );
extern void     setwindowtitle   ( const char *extra );
extern void     doscrmodechange  ( void );

#endif