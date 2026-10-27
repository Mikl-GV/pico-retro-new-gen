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
    // r0.417 (L3 аудита): таймаут вместо вечного спина — при сбое тактирования
    // UART байт теряется, но система не зависает.
    uint32_t t = 0;
    while (!(uart->USR & 2)) {      // TX FIFO not full
        if (++t > 100000) return;
    }
    uart->O00.THR = (uint8_t)c;
}

void uart_puts(const char* s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

char uart_getc(void) {
    // r0.417 (L3 аудита): таймаут вместо вечного спина; при отсутствии данных
    // возвращаем 0. Portfolio вызывает только после uart_rx_ready() — не мешает.
    uint32_t t = 0;
    while (!(uart->LSR & 1)) {      // RX FIFO not empty
        if (++t > 200000) return 0;
    }
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

// r795: WiFi-драйвер эталона (h3_sdc.c/wifi_net.c) использует
// uart_print/uart_print_int/uart_print_uint/uart_print_hex.
void uart_print(const char* s) {
    while (*s) uart_putc(*s++);
}

static void uart_print_u32(uint32_t v) {
    char buf[12];
    int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v && i < (int)sizeof(buf) - 1) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i) uart_putc(buf[--i]);
}

void uart_print_uint(unsigned int v) { uart_print_u32(v); }

void uart_print_int(int v) {
    if (v < 0) { uart_putc('-'); uart_print_u32((uint32_t)(-(v + 1)) + 1u); }
    else uart_print_u32((uint32_t)v);
}

void uart_print_hex(uint32_t v) {
    static const char hexd[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        uart_putc(hexd[(v >> i) & 0xF]);
}

// r795: эталон вызывает uart_flush() для слива лога перед переходами.
// У нас FIFO-вывод синхронный (uart_putc ждёт готовности) — no-op.
void uart_flush(void) {}