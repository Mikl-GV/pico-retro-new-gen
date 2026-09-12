/* startup.S — минимальный entry для Allwinner H3 (Orange Pi Lite).
 * Загрузчик (sunxi-fel или u-boot) прыгает сюда в 0x40000000, ARM state.
 * Делаем: SVC mode, MMU/caches off, стек в конце DRAM, BSS=0, main().
 */
.syntax unified
.arch armv7-a
.arm

.section .vectors, "ax"
.global _vectors
_vectors:
    ldr pc, =_start           /* 0x00 Reset */
    ldr pc, =_undef           /* 0x04 Undefined */
    ldr pc, =_hang            /* 0x08 SWI */
    ldr pc, =_prefetch        /* 0x0C Prefetch Abort */
    ldr pc, =_dataabort       /* 0x10 Data Abort */
    nop
    ldr pc, =_hang            /* 0x18 IRQ */
    ldr pc, =_hang            /* 0x1C FIQ */

/* ===== Вспомогательная функция: вывод буквы в UART0 THR (без ожидания) ===== */
/* Вход: r0 = char. Портим r1. */
.section .text
.global uart_dbg
uart_dbg:
    ldr r1, =0x01C28000
    strb r0, [r1]
    bx lr

/* Обработчик Undefined: 'U' + hang */
.global _undef
_undef:
    mov r0, #'U'
    bl uart_dbg
1:  b 1b

/* Обработчик Prefetch Abort: 'P' + hang */
.global _prefetch
_prefetch:
    mov r0, #'P'
    bl uart_dbg
1:  b 1b

/* Обработчик Data Abort: 'D' + hang */
.global _dataabort
_dataabort:
    mov r0, #'D'
    bl uart_dbg
1:  b 1b

.section .text
.global _start
_start:
    /* SVC mode, IRQ/FIQ off */
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD3
    msr cpsr, r0

    /* DGN '1': вошли в _start */
    mov r0, #'1'
    bl uart_dbg

    /* VBAR = наша таблица векторов (0x40000000) */
    ldr r0, =_vectors
    mcr p15, 0, r0, c12, c0, 0
    dsb
    isb

    /* DGN '2': VBAR установлен */
    mov r0, #'2'
    bl uart_dbg

    /* Включаем VFP/NEON (CP10/CP11) — иначе NEON-инструкции в C дают undefined */
    mrc p15, 0, r0, c1, c0, 2
    orr r0, r0, #(0xF << 20)      /* CP10, CP11 full access */
    mcr p15, 0, r0, c1, c0, 2
    isb
    mov r0, #0x40000000
    vmsr fpexc, r0                /* FPEXC.EN = 1 */
    isb

    /* DGN '3': VFP/NEON включён */
    mov r0, #'3'
    bl uart_dbg

    /* Stack: в конце 512MB DRAM (0x60000000) */
    ldr sp, =0x60000000

    /* DGN '3': стек готов */
    mov r0, #'3'
    bl uart_dbg

    /* Инвалидируем I-cache + DSB+ISB */
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    isb

    /* BSS = 0 (ARM mode) */
    ldr r0, =_bstart1
    ldr r1, =_bend1
    mov r2, #0
1:  cmp r0, r1
    bhs 2f
    str r2, [r0], #4
    b 1b
2:
    /* DGN 'A': перед main */
    mov r0, #'A'
    bl uart_dbg
    bl main

_hang:
    b _hang
.end