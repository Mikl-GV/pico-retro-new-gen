/*
 * Castaway
 *  (C) 1994 - 2002 Martin Doering, Joachim Hoenig
 *
 * ikbd.c - ST keyboard processor emulator (german keymap)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * revision history
 *  23.05.2002  JH  FAST1.0.1 code import: KR -> ANSI, restructuring
 *  09.06.2002  JH  Renamed io.c to st.c again (io.h conflicts with system headers)
 *  16.06.2002  JH  New function: IkbdQueryBuffer(). X11 keysym conversion removed.
 *  29.10.2002  JH  Added joystick interrogation mode and Joystick functions.
 */
static char     sccsid[] = "$Id: ikbd.c,v 1.4 2002/11/04 21:37:38 jhoenig Exp $";
#include "config.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>     /* r1566: memcpy; <memory.h> конфликтует с mcume */
#include "st.h"
#define IBS 10                  /* Ikbd input buffer size */
#define OBS 20                  /* Ikbd output buffer size */
#define max(A,B) ((A>B)?A:B)

int             pause = 0;
struct {
    char            buttons;    /* real buttons */
    char            stbuttons;  /* buttons as known to st */
    int             xscale, yscale;
    int             xmax, ymax;
    int             xkcm, ykcm;
    int             xth, yth;
    int             x, y;       /* real mouse position */
    int             stx, sty;   /* mouse position st */
    int             button_action;
    int             mode;
    int             yinv;
    int             flag;
}               mouse;

struct {
    int             mode;
    int             rate;
    int             state0; /* joystick 0 state */
    int             state1; /* joystick 1 state */
    int             rx, ry, tx, ty, vx, vy;
}               joystick;
unsigned char   inbuff[IBS];
unsigned char   outbuff[OBS];
int             inbuffi = 0;
int             outbuffi = 0;

/* r1603: снимок состояния IKBD и счётчики — диагностика ввода в ST: inp. */
int      cw_ikbd_mode, cw_ikbd_pause, cw_ikbd_x, cw_ikbd_stx, cw_ikbd_th;
int      cw_ikbd_joymode, cw_ikbd_joy;
unsigned cw_ikbd_bytes, cw_ikbd_last, cw_ikbd_keys, cw_ikbd_mpack;

/* r1609: сброс IKBD на входе в эмулятор. Без него повторный запуск
 * наследует mouse.mode/pause/очереди и положение мыши прошлой игры. */
void cw_ikbd_reset(void)
{
    memset(inbuff, 0, sizeof(inbuff));
    memset(outbuff, 0, sizeof(outbuff));
    inbuffi = 0;
    outbuffi = 0;
    pause = 0;

    memset(&mouse, 0, sizeof(mouse));
    mouse.stx = 160;            /* как после команды IKBD 0x80, low-res */
    mouse.sty = 100;

    memset(&joystick, 0, sizeof(joystick));

    cw_ikbd_mode = cw_ikbd_pause = cw_ikbd_x = cw_ikbd_stx = 0;
    cw_ikbd_th = cw_ikbd_joymode = cw_ikbd_joy = 0;
    cw_ikbd_bytes = cw_ikbd_last = cw_ikbd_keys = cw_ikbd_mpack = 0;
}

void            IkbdRecv(unsigned char inchar)
{
    cw_ikbd_bytes++;
    cw_ikbd_last = inchar;
#ifdef DEBUG
    fprintf (stderr, "IkbdRecv: %d\n", inchar);
#endif
    if (inbuffi == IBS) {
        fprintf (stderr, "IkbdRecv: Input buffer overrun!\n");
        inbuffi = 0;
    }
    inbuff[inbuffi++] = inchar;
    switch (inbuff[0]) {
    case 0x07:                  /* set mouse button action */
        if (inbuffi == 2) {
            inbuffi = 0;
            mouse.button_action = inbuff[1];
        }
        break;
    case 0x08:                  /* set relative mouse position reporting */
        inbuffi = 0;
        mouse.mode = 1;
        break;
    case 0x09:                  /* set absolute mouse positioning */
        if (inbuffi == 5) {
            mouse.mode = 2;
            mouse.xmax = (inbuff[1] << 8) | inbuff[2];
            mouse.ymax = (inbuff[3] << 8) | inbuff[4];
            inbuffi = 0;
        }
        break;
    case 0x0a:                  /* set mouse keycode mode */
        if (inbuffi == 3) {
            mouse.mode = 3;
            mouse.xkcm = inbuff[1];
            mouse.ykcm = inbuff[2];
            inbuffi = 0;
        }
        break;
    case 0x0b:                  /* set mouse threshold */
        if (inbuffi == 3) {
            mouse.xth = inbuff[1];
            mouse.yth = inbuff[2];
            inbuffi = 0;
        }
        break;
    case 0x0c:                  /* set mouse scale */
        if (inbuffi == 3) {
            mouse.xscale = inbuff[1];
            mouse.yscale = inbuff[2];
            inbuffi = 0;
        }
        break;
    case 0x0d:                  /* interrogate mouse position */
        inbuffi = 0;
        IkbdSend (0xf7);
        IkbdSend (mouse.buttons);
        IkbdSend (mouse.x >> 8);
        IkbdSend (mouse.x);
        IkbdSend (mouse.y >> 8);
        IkbdSend (mouse.y);
        break;
    case 0x0e:                  /* load mouse position */
        if (inbuffi == 6) {
            inbuffi = 0;
            mouse.x = (inbuff[2] << 8) | inbuff[3];
            mouse.y = (inbuff[4] << 8) | inbuff[5];
        }
        break;
    case 0x0f:                  /* set Y=0 at bottom */
        inbuffi = 0;
        mouse.yinv = 1;
        break;
    case 0x10:                  /* set Y=0 at top */
        inbuffi = 0;
        mouse.yinv = 0;
        break;
    case 0x11:                  /* resume */
        inbuffi = 0;
        pause = 0;
        break;
    case 0x12:                  /* disable mouse */
        inbuffi = 0;
        mouse.mode = 0;
        break;
    case 0x13:                  /* pause output */
        inbuffi = 0;
        pause = 1;
        break;
    case 0x14:                  /* set joystick event reporting */
        inbuffi = 0;
        joystick.mode = inbuff[0];
        break;
    case 0x15:                  /* set joystick interrogation mode */
        inbuffi = 0;
        joystick.mode = inbuff[0];
        break;
    case 0x16:                  /* joystick interrogation */
        inbuffi = 0;
        IkbdSend(0xfd);
        IkbdSend(joystick.state0);
        IkbdSend(joystick.state1);
        break;
    case 0x17:                  /* set joystick monitoring */
        if (inbuffi == 2) {
            inbuffi = 0;
            joystick.rate = inbuff[1];
            joystick.mode = inbuff[0];
        }
        break;
    case 0x18:                  /* set fire button monitoring */
        inbuffi = 0;
        joystick.mode = inbuff[0];
        break;
    case 0x19:                  /* set joystick keycode mode */
        if (inbuffi == 7) {
            inbuffi = 0;
            joystick.mode = inbuff[0];
        }
        break;
    case 0x1a:                  /* disable joysticks */
        inbuffi = 0;
        joystick.mode = inbuff[0];
        break;
    case 0x1b:                  /* time-of-day clock set */
        if (inbuffi == 7) {
            inbuffi = 0;
        }
        break;
    case 0x1c:                  /* interrogate time-of-day clock */
        inbuffi = 0;
        IkbdSend (0xfc);
        IkbdSend (0x0);
        IkbdSend (0x0);
        IkbdSend (0x0);
        IkbdSend (0x0);
        IkbdSend (0x0);
        IkbdSend (0x0);
        break;
    case 0x20:                  /* memory load */
    case 0x21:                  /* memory read */
    case 0x22:                  /* controller execute */
        inbuffi = 0;
        break;
    case 0x80:                  /* reset */
        if (inbuffi == 2) {
            IkbdSend (0xf0);
            inbuffi = 0;
            mouse.buttons = 0;
            mouse.stbuttons = 0;
            switch (vid_shiftmode) {
            case 0:
                mouse.stx = 160;
                mouse.sty = 100;
                break;
            case 1:
                mouse.stx = 320;
                mouse.sty = 100;
                break;
            case 2:
                mouse.stx = 320;
                mouse.sty = 200;
                break;
            }
        }
        break;
    case 0x87:                  /* request button action */
        IkbdSend (0x07);
        IkbdSend (mouse.button_action);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        break;
    case 0x88:                  /* request mouse mode */
    case 0x89:
    case 0x8a:
        switch (mouse.mode) {
        case 1:
            IkbdSend (0x08);
            IkbdSend (0);
            IkbdSend (0);
            IkbdSend (0);
            IkbdSend (0);
            IkbdSend (0);
            IkbdSend (0);
            break;
        case 2:
            IkbdSend (0x09);
            IkbdSend (mouse.xmax >> 8);
            IkbdSend (mouse.xmax);
            IkbdSend (mouse.ymax >> 8);
            IkbdSend (mouse.ymax);
            IkbdSend (0);
            IkbdSend (0);
            break;
        case 3:
            IkbdSend (0x09);
            IkbdSend (mouse.xkcm);
            IkbdSend (mouse.ykcm);
            IkbdSend (0);
            IkbdSend (0);
            IkbdSend (0);
            IkbdSend (0);
            break;
        }
        inbuffi = 0;
        mouse.mode = 1;
        break;
    case 0x8b:                  /* request mouse threshold */
        IkbdSend (0x0b);
        IkbdSend (mouse.xth);
        IkbdSend (mouse.yth);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        inbuffi = 0;
        break;
    case 0x8c:                  /* request mouse scale */
        IkbdSend (0x0b);
        IkbdSend (mouse.xscale);
        IkbdSend (mouse.yscale);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        inbuffi = 0;
        break;
    case 0x8f:                  /* request mouse vertical coordinates */
    case 0x90:
        if (mouse.yinv)
            IkbdSend (0x0f);
        else
            IkbdSend (0x10);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        inbuffi = 0;
        break;
    case 0x92:                  /* disable mouse */
        if (mouse.mode)
            IkbdSend (0);
        else
            IkbdSend (0x12);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        IkbdSend (0);
        inbuffi = 0;
        break;
    case 0x94:                  /* request joystick mode */
    case 0x95:
    case 0x99:                  /* set joystick keycode mode */
        inbuffi = 0;
        switch (joystick.mode) {
        case 0x14:
        case 0x15:
            IkbdSend (joystick.mode);
            IkbdSend (0);
            IkbdSend (0);
            IkbdSend (0);
            IkbdSend (0);
            IkbdSend (0);
            IkbdSend (0);
            break;
        case 0x19:
            IkbdSend (0x19);
            IkbdSend (joystick.rx);
            IkbdSend (joystick.ry);
            IkbdSend (joystick.tx);
            IkbdSend (joystick.ty);
            IkbdSend (joystick.vx);
            IkbdSend (joystick.vy);
            break;
        }
        inbuffi = 0;
        break;
    case 0x9a:                  /* request joystick availability */
        if (joystick.mode != 0x1a)
            IkbdSend (0);
        else
            IkbdSend (0x1a);
        IkbdSend (joystick.rx);
        IkbdSend (joystick.ry);
        IkbdSend (joystick.tx);
        IkbdSend (joystick.ty);
        IkbdSend (joystick.vx);
        IkbdSend (joystick.vy);
        inbuffi = 0;
        break;
    }
}

void            IkbdSend (unsigned char outchar)
{
#if 0
    fprintf (stderr, "IkbdSend: %d\n", outchar);
#endif
    if (outbuffi < (OBS - 1)) {
        outbuff[outbuffi++] = outchar;
    }
}

/*
 * This function tests if there is still
 * Ikbd Output waiting to be written
 */
int             IkbdQueryBuffer(void)
{
    return ((mouse.flag || outbuffi > 0) && !pause);
}

void            IkbdWriteBuffer(void)
{
    /* r1603: состояние для лога (вызывается каждый проход host-цикла). */
    cw_ikbd_mode = mouse.mode;
    cw_ikbd_pause = pause;
    cw_ikbd_x = mouse.x;
    cw_ikbd_stx = mouse.stx;
    cw_ikbd_th = mouse.xth;
    cw_ikbd_joymode = joystick.mode;
    cw_ikbd_joy = joystick.state0;

    if (mouse.flag && (outbuffi < (OBS / 2))) {
        IkbdMouse ();
    }
    if (outbuffi > 0 && !pause && (acia1_sr & 0x1) == 0) {
        acia1_sr |= 1;
        if (acia1_cr & 0x80) {  /* receiver interrupt enabled? */
            acia1_sr |= 0x80;
            mfp_gpip &= (~0x10);
        }
        acia1_dr = outbuff[0];
        memcpy (&outbuff[0], &outbuff[1], (OBS - 1));
        outbuffi--;
    }
}

void            IkbdKeyPress (unsigned short keysym)
{
    cw_ikbd_keys++;
    IkbdSend (keysym);
}

void            IkbdKeyRelease (unsigned short keysym)
{
    cw_ikbd_keys++;
    IkbdSend (0x80 | keysym);
}

void            IkbdMouse()
{
    int             dx, dy;
    mouse.flag = 0;
    switch (mouse.mode) {
    case 1:
        dx = mouse.x - mouse.stx;
        if (dx > 127) {
            dx = 127;
            mouse.flag = 1;
        }
        if (dx < -127) {
            dx = -127;
            mouse.flag = 1;
        }
        dy = mouse.y - mouse.sty;
        if (dy > 127) {
            dy = 127;
            mouse.flag = 1;
        }
        if (dy < -127) {
            dy = -127;
            mouse.flag = 1;
        }
        if (mouse.stbuttons != mouse.buttons
                || max (dx, -dx) > mouse.xth || max (dy, -dy) > mouse.yth) {
            mouse.stbuttons = mouse.buttons;
            mouse.stx = mouse.stx + dx;
            mouse.sty = mouse.sty + dy;
            if (mouse.stx <= 0) {
                mouse.stx = 0;
                dx = -127;
            }
            if (mouse.sty <= 0) {
                mouse.sty = 0;
                dy = -127;
            }
            switch (vid_shiftmode) {
            case 0:
                if (mouse.stx >= 319) {
                    mouse.stx = 319;
                    dx = 127;
                }
                if (mouse.sty >= 199) {
                    mouse.sty = 199;
                    dy = 127;
                }
                break;
            case 1:
                if (mouse.stx >= 639) {
                    mouse.stx = 639;
                    dx = 127;
                }
                if (mouse.sty >= 199) {
                    mouse.sty = 199;
                    dy = 127;
                }
                break;
            case 2:
                if (mouse.stx >= 639) {
                    mouse.stx = 639;
                    dx = 127;
                }
                if (mouse.sty >= 399) {
                    mouse.sty = 399;
                    dy = 127;
                }
                break;
            }
            IkbdSend (0xf8 | mouse.buttons);
            IkbdSend (dx);
            IkbdSend (dy);
            cw_ikbd_mpack++;
        }
#if 0
        fprintf (stderr, "Mouse: x %d, y %d, dx %d, dy %d\n", mouse.stx, mouse.sty, dx, dy);
#endif
        break;
    default:
        break;
    }
}

void            IkbdMouseMotion (int x, int y)
{
    switch (vid_shiftmode) {
    case 0:
        x /= 2;
    case 1:
        y /= 2;
        break;
    case 2:
        break;
    }
    mouse.x = x;
    mouse.y = y;
    mouse.flag = 1;
}

void            IkbdMousePress (int code)
{
    mouse.buttons |= code;
    IkbdMouse();
}

void            IkbdMouseRelease (int code)
{
    mouse.buttons &= ~code;
    IkbdMouse();
}

void            IkbdJoystickChange(int number, uint8 state)
{
#ifdef DEBUG
    assert(number == 0 || number == 1);
#endif
    /* r1605: состояние обновляем всегда (нужно для 0x16 interrogate),
     * пакеты 0xFF/0xFE шлём только в event reporting (0x14) — иначе они
     * попадают в поток программ, ждущих mouse-пакеты. */
    if (number == 0 && joystick.state0 != state) {
        joystick.state0 = state;
        if (joystick.mode == 0x14) {
            IkbdSend(0xff);
            IkbdSend(state);
        }
    }
    if (number == 1 && joystick.state1 != state) {
        joystick.state1 = state;
        if (joystick.mode == 0x14) {
            IkbdSend(0xfe);
            IkbdSend(state);
        }
    }
}
