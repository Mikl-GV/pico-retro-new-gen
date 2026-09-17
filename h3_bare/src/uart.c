// uart.c — Console UART0 (PA4=TX, PA5=RX), 115200 8N1.
// Это консольный UART (тот же, куда U-Boot пишет лог) — работает всегда.
#include "h3.h"
#include "h3_ccu.h"

static H3_UART_TypeDef* const uart = H3_UART0;

static void uart_clk_enable(void) {
    // Gate: BUS_CLK_GATING3 bit 16 = UART0
    H3_CCU->BUS_CLK_GATING3 |= CCU_BUS_CLK_GATING3_UART0;
    // Soft reset de-assert: BUS_SOFT_RESET4 bit 16 = UART0
    H3_CCU->BUS_SOFT_RESET4 |= CCU_BUS_SOFT_RESET4_UART0;
    // пауза на стабилизацию тактирования
    volatile uint32_t n = 2000;
    while (n--) {}
}

void uart_init(void) {
    uart_clk_enable();

    // PA4=TX (alt 2), PA5=RX (alt 2) — в CFG0 (PA0..PA7)
    uint32_t cfg0 = H3_PIO_PORTA->CFG0;
    cfg0 &= ~(0xF << 16);            // PA4
    cfg0 |= (2 << 16);
    cfg0 &= ~(0xF << 20);            // PA5
    cfg0 |= (2 << 20);
    H3_PIO_PORTA->CFG0 = cfg0;

    // 115200 8N1: APB2 ~= 24MHz, divisor = 13
    uart->LCR = (1 << 7) | 3;        // DLAB=1, 8N1
    uart->O00.DLL = 13;
    uart->O04.DLH = 0;
    uart->LCR = 3;
    uart->O08.FCR = 0x01;            // FIFO enable
}

void uart_putc(char c) {
    while (!(uart->USR & 2)) {}      // TX FIFO not full
    uart->O00.THR = (uint8_t)c;
}

void uart_puts(const char* s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

char uart_getc(void) {
    while (!(uart->LSR & 1)) {}
    return (char)(uart->O00.RBR & 0xFF);
}

int uart_rx_ready(void) {
    return (uart->LSR & 1) ? 1 : 0;
}

// Сброс RX FIFO — чтобы случайные символы из терминала
// не попали в Portfolio при старте
void uart_rx_flush(void) {
    while (uart_rx_ready()) uart->O00.RBR;
}