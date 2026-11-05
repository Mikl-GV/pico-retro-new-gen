/* ms1504_com.c — эмуляция COM1 (UART 8250) для вшитого BIOS PK300 (МС1504).
 *
 * BIOS ПК300/T1100+ (сигнатура «ALFA-DEBUG V2.4»): это ROM-монитор с выводом
 * на последовательный терминал. Код старта опрашивает COM1 (порты 0x3F8-0x3FF,
 * дизассемблировано: 0x3FD LSR -> loop ожидания готовности, 0x3F8 RBR/THR),
 * без ответа UART эмулятор виснет на spin-loop либо спит в HLT.
 *
 * Даём минимальный 8250:
 *   - RBR (0x3F8 r)  — RX-data: коды от USB-клавиатуры (см. m15_feeds_kbd),
 *     которые монитор воспринимает как команды терминала.
 *   - THR (0x3F8 w)  — TX-data: печатаем в UART (vuart_puts) для отладки.
 *   - IER (0x3F9)    — бит 0 (RX-int) храним, IRQ4 по нему не генерим
 *                      (polling достаточно, CPU и так в ~вечном цикле).
 *   - IIR (0x3FA r)  — 0x06/0xC6: нет ожидающих прерываний.
 *   - LCR (0x3FB)    — регистр линии (pace 128: DLAB). Только храним:
 *                      настройка скорости не влияет на виртуальный FIFO.
 *   - MCR (0x3FC)    — регистр модема (храним no-op).
 *   - LSR (0x3FD r)  — бит 1 (THRE: передатчик готов) ВСЕГДА = 1, бит 0
 *                      (DR: есть данные) = есть ли байт во входном FIFO.
 *   - MSR (0x3FE r)  — 0x20 (DSR) + 0x10 (CTS): «модем жив».
 *
 * Входной FIFO (16 байт) кормится НЕ из IRQ: host зовёт m15_feed_com_byte()
 * для каждого скакода клавиатуры (ms1504_host.c), монитор опрашивает LSR.
 */

#include <stdint.h>

#include "fake86_src/ports.h"
#include "fake86_src/i8259.h"
#include "uart.h"

/* TX-вывод монитора ПК300 — печатаем в diagnostic UART (отладка). */
static void ms1504_uart_putc(char c) { uart_putc(c); }

/* --- состояние 8250 --- */
static uint8_t c_ier = 0;          /* IER: только бит 0 (RX) имеет смысл */
static uint8_t c_lcr = 0;          /* LCR: 8N1 по умолчанию 0x03 */
static uint8_t c_mcr = 0;          /* MCR: no-op (DTR/RTS игнорируем) */
static uint8_t c_msr = 0x30;       /* MSR: DSR|CTS всегда */
static uint8_t c_dlab_low  = 0x0C; /* DLL: 9600 (0x0C) — только для виду */
static uint8_t c_dlab_high = 0;    /* DLM */
static int      c_tx_pending = 0;  /* неиспользуем: TX мгновенен */

#define COM_FIFO 16
static uint8_t  c_rx[COM_FIFO];
static unsigned c_rx_head = 0, c_rx_tail = 0, c_rx_len = 0;
static int      c_rx_overflow = 0;

static int rx_avail(void) { return (int)c_rx_len; }

void m15_feed_com_byte(uint8_t b)
{
    if (c_rx_len >= COM_FIFO) { c_rx_overflow = 1; return; }
    unsigned i = c_rx_head;
    c_rx[i] = b;
    c_rx_head = (c_rx_head + 1) % COM_FIFO;
    c_rx_len++;
}

static uint8_t rx_byte(void)
{
    uint8_t b = c_rx[c_rx_tail];
    c_rx_tail = (c_rx_tail + 1) % COM_FIFO;
    c_rx_len--;
    return b;
}

static void com_tx(uint8_t v)
{
    ms1504_uart_putc((char)v);
}

/* --- редиректоры портов --- */
static uint8_t com_in(uint16_t port)
{
    port &= 7;   /* 0x3F8..0x3FF */
    if ((c_lcr & 0x80)) {          /* DLAB: DLL/DLM на позициях RBR/THR */
        if (port == 0) return c_dlab_low;
        if (port == 1) return c_dlab_high;
        /* при DLAB остальные не определены — 0 */
        return 0;
    }
    switch (port) {
    case 0: return rx_avail() ? rx_byte() : 0;
    case 1: return c_ier;
    case 2: return 0x06;           /* IIR: no pending interrupts */
    case 3: return c_lcr;
    case 4: return c_mcr;
    case 5: return (uint8_t)(0x02 | (rx_avail() ? 0x01 : 0));  /* THRE | DR */
    case 6: return c_msr;
    case 7: return 0;              /* SCR */
    }
    return 0;
}

static void com_out(uint16_t port, uint8_t value)
{
    port &= 7;
    if ((c_lcr & 0x80)) {
        if (port == 0) { c_dlab_low  = value; return; }
        if (port == 1) { c_dlab_high = value; return; }
        return;
    }
    switch (port) {
    case 0: com_tx(value); return;          /* THR */
    case 1: c_ier = (uint8_t)(value & 0x01); return;  /* IER: только RX */
    case 3: c_lcr = value; return;          /* LCR */
    case 4: c_mcr = value; return;          /* MCR */
    default:
        /* IIR (чтение), LSR, MSR, SCR — write-only в 8250 или no-op */
        return;
    }
}

/* --- CMOS/RTC (MC146818) — минимальный набор для POST PK300 --- */
static uint8_t cmos_idx = 0;
static uint8_t cmos_ram[128];   // 64 байта стандарт + расширение

static void cmos_addr_out(uint16_t port, uint8_t value)
{
    (void)port;
    cmos_idx = value & 0x7F;    // NMI-бит (0x80) игнорируем
}

static uint8_t cmos_data_in(uint16_t port)
{
    uint8_t v;
    (void)port;
    switch (cmos_idx) {
    case 0x00: v = 0x00; break;  // секунды
    case 0x02: v = 0x30; break;  // минуты
    case 0x04: v = 0x0A; break;  // часы (10:00)
    case 0x0A: v = 0x26; break;  // статус A: дел. 16, 32.768 кГц
    case 0x0B: v = 0x02; break;  // статус B
    case 0x0C: v = 0x00; break;  // статус C
    case 0x0D: v = 0x80; break;  // статус D: данные валидны
    case 0x07: v = 0x06; break;  // день недели
    case 0x08: v = 0x01; break;  // месяц
    case 0x09: v = 0x01; break;  // год (2001)
    default:   v = cmos_ram[cmos_idx] & 0xFF; break;
    }
    return v;
}

static void cmos_data_out(uint16_t port, uint8_t value)
{
    (void)port;
    cmos_ram[cmos_idx] = value;
    /* статус B/D держим «правильными» всегда */
    if (cmos_idx == 0x0B) cmos_ram[0x0B] = (uint8_t)(value | 0x02);
    if (cmos_idx == 0x0D) cmos_ram[0x0D] = (uint8_t)(value | 0x80);
}

void initcom1(void)
{
    set_port_write_redirector(0x3F8, 0x3FF, &com_out);
    set_port_read_redirector(0x3F8, 0x3FF, &com_in);
    /* r1022: CMOS/RTC (MC146818) — порты 0x70/0x71. BIOS PK300 в POST читает
     * регистры статуса RTC (статус D=0x0D и т.п.) и ждёт нормальных значений;
     * у нас без эмуляции IN 71 возвращал 0, BIOS зацикливался на чтении
     * (лог: бесконечные OUT 70,8D/8E/8A; IN 71). Даём минимальный набор:
     * статус A=0x26 (32.768 кГц, дел. 16), B=0x02, C=0, D=0x80 (данные валидны),
     * время/дата — фиксированные (BIOS их только читает, не критично). */
    set_port_write_redirector(0x70, 0x70, &cmos_addr_out);
    set_port_read_redirector(0x71, 0x71, &cmos_data_in);
    set_port_write_redirector(0x71, 0x71, &cmos_data_out);
}

/* r1025: «клавиатура ответила на reset» — заявить IRQ1 в PIC (IRR+ISR),
   чтобы BIOS-спин `IN 20,0x20; AND 2; LOOP` (F6430) увидел прерывание и
   прочитал порт 0x60 (куда host уже записал 0xAA). */
void m15_irq1_fake(void)
{
    i8259.irr |= 0x02;
    i8259.isr |= 0x02;
    i8259.imr &= ~0x02;
}

/* --- HID-сканкод USB-клавиатуры -> ASCII (для терминала монитора) ---
 * Монитор «ALFA-DEBUG» ждёт символы с терминала; превращаем основной набор
 * клавиш в печатные ASCII/управляющие. Следим за Shift для регистра букв.
 */
static uint8_t m15_shift = 0;   /* 0x02 LShift, 0x20 RShift */

static uint8_t hid_to_com_char(uint8_t sc, uint8_t shift)
{
    switch (sc) {
    case 0x04: return shift ? 'A' : 'a'; case 0x05: return shift ? 'B' : 'b';
    case 0x06: return shift ? 'C' : 'c'; case 0x07: return shift ? 'D' : 'd';
    case 0x08: return shift ? 'E' : 'e'; case 0x09: return shift ? 'F' : 'f';
    case 0x0A: return shift ? 'G' : 'g'; case 0x0B: return shift ? 'H' : 'h';
    case 0x0C: return shift ? 'I' : 'i'; case 0x0D: return shift ? 'J' : 'j';
    case 0x0E: return shift ? 'K' : 'k'; case 0x0F: return shift ? 'L' : 'l';
    case 0x10: return shift ? 'M' : 'm'; case 0x11: return shift ? 'N' : 'n';
    case 0x12: return shift ? 'O' : 'o'; case 0x13: return shift ? 'P' : 'p';
    case 0x14: return shift ? 'Q' : 'q'; case 0x15: return shift ? 'R' : 'r';
    case 0x16: return shift ? 'S' : 's'; case 0x17: return shift ? 'T' : 't';
    case 0x18: return shift ? 'U' : 'u'; case 0x19: return shift ? 'V' : 'v';
    case 0x1A: return shift ? 'W' : 'w'; case 0x1B: return shift ? 'X' : 'x';
    case 0x1C: return shift ? 'Y' : 'y'; case 0x1D: return shift ? 'Z' : 'z';
    case 0x1E: return shift ? '!' : '1'; case 0x1F: return shift ? '@' : '2';
    case 0x20: return shift ? '#' : '3'; case 0x21: return shift ? '$' : '4';
    case 0x22: return shift ? '%' : '5'; case 0x23: return shift ? '^' : '6';
    case 0x24: return shift ? '&' : '7'; case 0x25: return shift ? '*' : '8';
    case 0x26: return shift ? '(' : '9'; case 0x27: return shift ? ')' : '0';
    case 0x28: return 0x0D;            /* Enter */
    case 0x29: return 0x1B;            /* Esc */
    case 0x2A: return 0x08;            /* Backspace */
    case 0x2C: return 0x20;            /* Space */
    case 0x2D: return shift ? '_' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x2F: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default:   return 0;              /* прочие клавиши — не текст */
    }
}

/* host зовёт из ms1504_kbd_update: отчёты клавиатуры -> FIFO монитора */
void m15_feed_kbd_report(const uint8_t* keys, int n, uint8_t mods)
{
    m15_shift = mods;
    for (int i = 0; i < n; i++) {
        uint8_t ch = hid_to_com_char(keys[i], (mods & 0x22) ? 1 : 0);
        if (ch) m15_feed_com_byte(ch);
    }
}