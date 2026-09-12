#ifndef PLATFORM_UART_H
#define PLATFORM_UART_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char* s);
char uart_getc(void);
int  uart_rx_ready(void);

#endif