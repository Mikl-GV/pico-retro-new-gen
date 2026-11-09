#ifndef RK86_MACHINE_H
#define RK86_MACHINE_H

#include <stdint.h>

// r1185: Радио-86РК — КР580ВМ80А, 32К ОЗУ, ВГ75, ВВ55, ПЗУ монитора 2К.
// Карта памяти/портов (как в RK8266):
//   0x0000..0x7FFF — ОЗУ;
//   0x8000..0x9FFF — ВВ55 (порты 0..3);
//   0xA000..0xBFFF — дополнительное ОЗУ 8К (дисковые системы);
//   0xC000..0xDFFF — ВГ75 (порт 0/1) и знакогенератор (A10=1, страницы A12..A11);
//   0xE000..0xFFFF — ПЗУ (монитор с 0xF800), 0xE000..0xE00F — КР580ВТ57.

#define RK86_ROWS 30
#define RK86_COLS 78

typedef struct {
    uint8_t screen_w;      // знакомест в строке (ВГ75, по умолчанию 78)
    uint8_t screen_h;      // строк (30)
    uint8_t underline_y;   // строка подчёркивания внутри знакоместа
    uint8_t char_h;        // высота знакоместа
    uint8_t attr_visible;  // управляющий байт занимает позицию на экране
    uint8_t cursor_x;
    uint8_t cursor_y;
    uint8_t cursor_type;
    uint8_t dma_burst;
    uint8_t sreg;
    const uint8_t* vram;   // окно видео-ОЗУ (задаётся ВТ57)
} rk86_screen_t;

extern rk86_screen_t rk86_screen;

// init: rom==NULL — только вшитый монитор; 2К — образ ПЗУ монитора,
// до 32К — образ ОЗУ (программа/дамп памяти).
void rk86_init(const uint8_t* rom, uint32_t size);
void rk86_run(uint32_t cycles);

// Конец кадра: ВГ75 выставляет запрос прерывания (SREG_IR), который монитор и
// игры ждут опросом порта 1 перед выводом на экран.
void rk86_vg75_vsync(void);

// Страница знакогенератора (0..3) — выбирается атрибутами GPA в тексте.
const uint8_t* rk86_font_page(int n);

// Загрузка программы (.rk/.rkr/.gam/.pki — заголовок start/end BE, .bin —
// сырой образ с адреса 0) и переход на стартовый адрес. Возвращает entry+1
// (0 — образ не распознан/не влез). Имя нужно для определения формата.
uint32_t rk86_load_program(const uint8_t* data, uint32_t size, const char* name);

// Клавиатура: матрица 8x8, активный уровень 0, код = (строка << 8) | бит.
void rk86_kbd_press(uint16_t code);
void rk86_kbd_release(uint16_t code);
void rk86_kbd_release_all(void);
void rk86_kbd_set_mods(int ss, int us, int rl);

// Модификаторы (те же коды годятся для press/release).
#define RK86_MOD_SS 0x2000u   // СС (Shift)
#define RK86_MOD_US 0x4000u   // УС (Ctrl)
#define RK86_MOD_RL 0x8000u   // РУС/ЛАТ

// Строка 0
#define RK86_HOME  0x0001u   // СБР
#define RK86_STR   0x0002u   // СТР
#define RK86_AR2   0x0004u   // АР2
#define RK86_F1    0x0008u
#define RK86_F2    0x0010u
#define RK86_F3    0x0020u
#define RK86_F4    0x0040u

// Строка 1
#define RK86_TAB   0x0101u
#define RK86_PS    0x0102u
#define RK86_VK    0x0104u
#define RK86_ZB    0x0108u
#define RK86_LEFT  0x0110u
#define RK86_UP    0x0120u
#define RK86_RIGHT 0x0140u
#define RK86_DOWN  0x0180u

// Строка 2: цифры 0..7
#define RK86_0     0x0201u
#define RK86_1     0x0202u
#define RK86_2     0x0204u
#define RK86_3     0x0208u
#define RK86_4     0x0210u
#define RK86_5     0x0220u
#define RK86_6     0x0240u
#define RK86_7     0x0280u

// Строка 3: 8 9 * ; , - . /
#define RK86_8       0x0301u
#define RK86_9       0x0302u
#define RK86_STAR    0x0304u
#define RK86_SEMI    0x0308u
#define RK86_COMMA   0x0310u
#define RK86_MINUS   0x0320u
#define RK86_PERIOD  0x0340u
#define RK86_SLASH   0x0380u

// Строка 4: @ A B C D E F G
#define RK86_AT 0x0401u
#define RK86_A  0x0402u
#define RK86_B  0x0404u
#define RK86_C  0x0408u
#define RK86_D  0x0410u
#define RK86_E  0x0420u
#define RK86_F  0x0440u
#define RK86_G  0x0480u

// Строка 5: H I J K L M N O
#define RK86_H 0x0501u
#define RK86_I 0x0502u
#define RK86_J 0x0504u
#define RK86_K 0x0508u
#define RK86_L 0x0510u
#define RK86_M 0x0520u
#define RK86_N 0x0540u
#define RK86_O 0x0580u

// Строка 6: P Q R S T U V W
#define RK86_P 0x0601u
#define RK86_Q 0x0602u
#define RK86_R 0x0604u
#define RK86_S 0x0608u
#define RK86_T 0x0610u
#define RK86_U 0x0620u
#define RK86_V 0x0640u
#define RK86_W 0x0680u

// Строка 7: X Y Z [ \ ] ^ пробел
#define RK86_X     0x0701u
#define RK86_Y     0x0702u
#define RK86_Z     0x0704u
#define RK86_LBRK  0x0708u
#define RK86_BSLASH 0x0710u
#define RK86_RBRK  0x0720u
#define RK86_CARET 0x0740u
#define RK86_SPACE 0x0780u

#endif
