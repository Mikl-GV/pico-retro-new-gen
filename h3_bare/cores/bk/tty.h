extern d_word mouse_button_state;
extern int relx, rely;
extern unsigned short mouse_but0;

#define TTY_NOTHING     0377
#define TTY_STOP        0376
#define TTY_RESET	0375	/* "reset buton", only with AR2 */
#define TTY_AR2		0374	/* AP2 */
#define TTY_SWITCH	0373	/* video mode switch */
#define TTY_DOWNLOAD	0372	/* direct file load */
