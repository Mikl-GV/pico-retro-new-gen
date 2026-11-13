#define TID_FLASH 2
#define TID_IRQ 3
#define TID_NMI 4

#ifdef DOUBLE_X
#define CHAR_W 16
#define PIX_W 2
#else
#define CHAR_W 8
#define PIX_W 1
#endif

#ifdef DOUBLE_Y
#define CHAR_H 16
#define PIX_H 2
#else
#define CHAR_H 8
#define PIX_H 1
#endif

int init_video_window(struct SYS_RUN_STATE*sr);
void set_video_size(struct SYS_RUN_STATE*sr, int w, int h);
int term_video_window(struct SYS_RUN_STATE*sr);
int invalidate_video_window(struct SYS_RUN_STATE*sr, RECT *r);


int  video_init(struct SYS_RUN_STATE*sr);
void vid_invalidate_addr(struct SYS_RUN_STATE*sr, dword addr);
void set_fullscreen(struct SYS_RUN_STATE*sr, int fs);
const byte*video_get_font(struct SYS_RUN_STATE*sr);
int video_select_font(struct SYS_RUN_STATE*sr, int fnt);
int video_select_80col(struct SYS_RUN_STATE*sr, int set);
int video_get_flash(struct SYS_RUN_STATE*sr);
int video_get_flags(struct SYS_RUN_STATE*sr, word addr); // apple 2e flags
int is_keyb_english(struct SYS_RUN_STATE*sr);
int is_shift_pressed(struct SYS_RUN_STATE*sr);
int is_ctrl_pressed(struct SYS_RUN_STATE*sr);
int is_alt_pressed(struct SYS_RUN_STATE*sr);


#ifndef FLASH_INTERVAL
#ifdef UNDER_CE
#define FLASH_INTERVAL 500
#else
#define FLASH_INTERVAL 200
#endif
#endif //!defined FLASH_INTERVAL
