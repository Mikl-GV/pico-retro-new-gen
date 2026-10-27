#ifndef PLATFORM_UART_H
#define PLATFORM_UART_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char* s);
char uart_getc(void);
int  uart_rx_ready(void);
void uart_rx_flush(void);

// r795: совместимость с WiFi-драйвером эталона (h3_sdc.c/wifi_net.c):
// эталон печатает через uart_print/uart_print_int/uart_print_hex.
void uart_print(const char* s);        /* как uart_puts (без \n→\r\n) */
void uart_print_uint(unsigned int v);  /* десятичное беззнаковое */
void uart_print_int(int v);            /* десятичное со знаком */
void uart_print_hex(uint32_t v);       /* 8 hex-цифр */
void uart_flush(void);                 /* эталон: слив лога (у нас no-op, FIFO) */

#endif