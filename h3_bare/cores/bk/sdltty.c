#include "defines.h"
#include "tty.h"
#include "SDL/SDL.h"
#include "SDL/SDL_keysym.h"
#include "SDL/SDL_events.h"
#include <ctype.h>
#include <libintl.h>
#define _(String) gettext (String)

int special_keys[SDLK_LAST], shifted[256];

int grab_mode;

static int joystick_state_buttons = 0, joystick_state_hat = 0;

void tty_keyevent(int c);

void
joystick_event(SDL_Event * pev) {
        switch (pev->type) {
        case SDL_JOYBUTTONDOWN:
                joystick_state_buttons |= JOYSTICK_BUTTON(pev->jbutton.button);
                break;
        case SDL_JOYBUTTONUP:
                joystick_state_buttons &= ~(JOYSTICK_BUTTON(pev->jbutton.button));
                break;
        case SDL_JOYHATMOTION: {
                int hat = pev->jhat.value;
                int newval = 0;
                if (hat & SDL_HAT_UP)
                        newval |= JOYSTICK_UP;
                if (hat & SDL_HAT_DOWN)
                        newval |= JOYSTICK_DOWN;
                if (hat & SDL_HAT_LEFT)
                        newval |= JOYSTICK_LEFT;
                if (hat & SDL_HAT_RIGHT)
                        newval |= JOYSTICK_RIGHT;
                joystick_state_hat = newval;
        }
        }
}

void platform_joystick_init() {
}

d_word platform_joystick_get_state() {
        return joystick_state_buttons | joystick_state_hat;
}

void
mouse_event(SDL_Event * pev) {
	switch (pev->type) {
	case SDL_MOUSEBUTTONDOWN:
		if (pev->button.button == 3) {
			/* middle button switches grab mode */
			grab_mode ^= 1;
			SDL_WM_GrabInput(grab_mode ? SDL_GRAB_ON : SDL_GRAB_OFF);			return;
		}
		mouse_button_state |= mouse_but0 << pev->button.button;
		break;
	case SDL_MOUSEBUTTONUP:
		mouse_button_state &= ~(mouse_but0 << pev->button.button);
		break;
	case SDL_MOUSEMOTION:
		relx += pev->motion.xrel;
		rely += pev->motion.yrel;
		break; 
	}
}

int handle_special_key(int k) {
	if (SDLK_UNKNOWN == k) {
	    return 0;
	}
	switch (k) {
	case SDLK_F12: {
		ui_download();
		return 1;
	}
	case SDLK_SCROLLOCK:
		scr_switch(0, 0);
		return 1;
	default:
		return 0;
	}
}

static int ar2 = 0;

tty_keyevent_transform(int ksym, int kmod, int is_keyup) {
	int k, c;
	k = ksym;

	if (SDLK_UNKNOWN == k) {
		return;
	}
	
	if (k == SDLK_LCTRL)
		k = SDLK_RETURN;
	else if (k == SDLK_LALT)
		k = SDLK_SPACE;
	else if (k == SDLK_BACKSPACE)
		k = 265;
	else if (k == SDLK_TAB)
		k = 257;
	else if (k == SDLK_LSHIFT)
		k = 259;
		
	if(is_keyup) {
		if (special_keys[k] == TTY_AR2) ar2 = 0;
		tty_keyevent(-1);
		return;
	}
	/* modifier keys handling */
	if (special_keys[k] != -1) {
		if (special_keys[k] == TTY_AR2) {
			ar2 = 1;
			return;
		}
		c = special_keys[k];
	} else {
	    // Keysym follows ASCII
	    c = k;
	    if ((kmod & KMOD_CAPS) && isalpha(c)) {
		c &= ~040;  /* make it capital */
	    }
	    if (kmod & KMOD_SHIFT) {
		c = shifted[c];
	    }
	    if ((kmod & KMOD_CTRL) && (c & 0100)) {
		c &= 037;
	    }
	}
	// Win is AP2
	if (ar2) {
	    c |= 0200;
	}
	tty_keyevent(c);
}

/*
 * tty_recv() - Called at various times during simulation to
 * set if the user typed something.
 */

tty_recv()
{
    SDL_Event ev;
    /* fprintf(stderr, "Polling events..."); */
    while (SDL_PollEvent(&ev)) {
	extern void mouse_event(SDL_Event * pev);

	    switch (ev.type) {
	    case SDL_KEYDOWN:
		    if (handle_special_key(ev.key.keysym.sym))
			    break;
		    /* Fallthrough */
	    case SDL_KEYUP:
		    tty_keyevent_transform(ev.key.keysym.sym, ev.key.keysym.mod, ev.type == SDL_KEYUP);
		    break;
	    case SDL_VIDEOEXPOSE:
	    case SDL_ACTIVEEVENT:
		    /* the visibility changed */
		    scr_dirty  = 256;
		    break;
	    case SDL_MOUSEBUTTONDOWN:
	    case SDL_MOUSEBUTTONUP:
	    case SDL_MOUSEMOTION:
		    mouse_event(&ev);
		    break;
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
            case SDL_JOYHATMOTION:
            case SDL_JOYAXISMOTION:
            case SDL_JOYBALLMOTION:
		    joystick_event(&ev);
		    break;
	    case SDL_VIDEORESIZE:
		    scr_switch(ev.resize.w, ev.resize.h);
		    break;
	    case SDL_QUIT:
		    exit(0);
	    default:;
		    fprintf(stderr, _("Unexpected event %d\n"), ev.type);
	    }
    }
    /* fprintf(stderr, "done\n"); */
}

tty_open()
{
    int i;
    /* initialize the keytables */
    for (i = 0; i < SDLK_LAST; i++) {
	special_keys[i] = -1;
    }
    special_keys[SDLK_BACKSPACE] = 030;
    special_keys[SDLK_TAB] = 011;
    special_keys[SDLK_RETURN] = 012;
    special_keys[SDLK_CLEAR] = 014;        /* sbr */

    for (i = SDLK_NUMLOCK; i <= SDLK_COMPOSE; i++)
	special_keys[i] = TTY_NOTHING;

    special_keys[SDLK_SCROLLOCK] = TTY_SWITCH;
    special_keys[SDLK_LSUPER] = TTY_AR2;
    special_keys[SDLK_LALT] = TTY_AR2;
    special_keys[SDLK_ESCAPE] = TTY_STOP;

    special_keys[SDLK_DELETE] = -1;
    special_keys[SDLK_LEFT] = 010;
    special_keys[SDLK_UP] = 032;
    special_keys[SDLK_RIGHT] = 031;
    special_keys[SDLK_DOWN] = 033;
    special_keys[SDLK_HOME] = 023;         /* vs */
    special_keys[SDLK_PAGEUP] = -1;     /* PgUp */
    special_keys[SDLK_PAGEDOWN] = -1;    /* PgDn */
    special_keys[SDLK_END] = -1;
    special_keys[SDLK_INSERT] = -1;
    special_keys[SDLK_BREAK] = TTY_STOP;
    special_keys[SDLK_F1] = 0201;          /* povt */
    special_keys[SDLK_F2] = 003;           /* kt */
    special_keys[SDLK_F3] = 0213;          /* -|--> */
    special_keys[SDLK_F4] = 026;           /* |<--- */
    special_keys[SDLK_F5] = 027;           /* |---> */
    special_keys[SDLK_F6] = 0202;          /* ind su */
    special_keys[SDLK_F7] = 0204;          /* blk red */
    special_keys[SDLK_F8] = 0200;          /* shag */
    special_keys[SDLK_F9] = 014;           /* sbr */
    special_keys[SDLK_F10] = TTY_STOP;
    special_keys[SDLK_F11] = TTY_RESET;
    special_keys[SDLK_F12] = TTY_DOWNLOAD;
    for (i = 0; i < 256; i++) {
	shifted[i] = i;
    }
    for (i = 'A'; i <= 'Z'; i++) {
	shifted[i] = i ^ 040;
	shifted[i ^ 040] = i;
    }
    shifted['1'] = '!';
    shifted['2'] = '@';
    shifted['3'] = '#';
    shifted['4'] = '$';
    shifted['5'] = '%';
    shifted['6'] = '^';
    shifted['7'] = '&';
    shifted['8'] = '*';
    shifted['9'] = '(';
    shifted['0'] = ')';
    shifted['-'] = '_';
    shifted['='] = '+';
    shifted['\\'] = '|';
    shifted['['] = '{';
    shifted[']'] = '}';
    shifted[';'] = ':';
    shifted['\''] = '"';
    shifted['`'] = '~';
    shifted[','] = '<';
    shifted['.'] = '>';
    shifted['/'] = '?';
}
