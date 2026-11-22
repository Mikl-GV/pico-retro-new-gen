/* cw_input.h — ввод USB -> IKBD (ST) для Castaway (r1566) */
#ifndef CW_INPUT_H
#define CW_INPUT_H

void cw_input_init(void);
void cw_input_poll(void);   /* вызвать ~200 Гц: читает USB, шлёт IKBD press/release */

#endif /* CW_INPUT_H */
