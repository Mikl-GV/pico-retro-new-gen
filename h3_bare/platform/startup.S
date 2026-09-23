/* startup.S — минимальный entry для Allwinner H3 (Orange Pi Lite).
 * Загрузчик (sunxi-fel или u-boot) прыгает сюда в 0x40000000, ARM state.
 * Делаем: SVC mode, стек в конце DRAM, BSS=0, main().
 * MMU и D-cache остаются как U-Boot (включены). OHCI-структуры (ED/TD/HCCA,
 * буферы) вынесены в libh3_coherent_region и помечены uncached через MMU
 * (см. mmu_mark_uncached в usb_ohci.c) — поэтому DMA/HC не конфликтуют
 * с write-back кэшем, а остальная память работает на полной скорости.
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

/* Обработчик Prefetch Abort: 'P:' + IFAR + ':' + IFSR + hang */
.global _prefetch
_prefetch:
    push {r0-r3, lr}
    mov r0, #'P'
    bl uart_dbg
    mov r0, #':'
    bl uart_dbg
    mrc p15, 0, r0, c6, c0, 2    /* IFAR */
    bl hex32_uart
    mov r0, #':'
    bl uart_dbg
    mrc p15, 0, r0, c5, c0, 1    /* IFSR */
    bl hex8_uart
    mov r0, #'\n'
    bl uart_dbg
    pop {r0-r3, lr}
1:  b 1b

/* Обработчик Data Abort: 'D:' + DFAR + ':' + DFSR + ' ' + 'I:' + IFAR + '\n' + hang */
.global _dataabort
_dataabort:
    push {r0-r3, lr}
    mov r0, #'D'
    bl uart_dbg
    mov r0, #':'
    bl uart_dbg
    mrc p15, 0, r0, c6, c0, 0    /* DFAR */
    bl hex32_uart
    mov r0, #':'
    bl uart_dbg
    mrc p15, 0, r0, c5, c0, 0    /* DFSR */
    bl hex8_uart
    mov r0, #' '
    bl uart_dbg
    mov r0, #'I'
    bl uart_dbg
    mov r0, #':'
    bl uart_dbg
    mrc p15, 0, r0, c6, c0, 2    /* IFAR */
    bl hex32_uart
    mov r0, #':'
    bl uart_dbg
    mrc p15, 0, r0, c5, c0, 1    /* IFSR */
    bl hex8_uart
    mov r0, #'\n'
    bl uart_dbg
    pop {r0-r3, lr}
1:  b 1b

/* Вывести число r0 (мл. 8 бит) как 2 hex-цифры (0-9A-F) */
.global hex8_uart
hex8_uart:
    push {r1, lr}
    mov r1, r0
    lsr r0, r1, #4
    and r0, r0, #0xF
    cmp r0, #10
    addlo r0, r0, #'0'
    addhs r0, r0, #('A' - 10)
    bl uart_dbg
    and r0, r1, #0xF
    cmp r0, #10
    addlo r0, r0, #'0'
    addhs r0, r0, #('A' - 10)
    bl uart_dbg
    pop {r1, pc}

/* Вывести число r0 как 8 hex-цифр (32 бита) */
.global hex32_uart
hex32_uart:
    push {r1, r4, lr}
    mov r4, r0
    mov r1, #8
1:  mov r0, r4, lsr #28
    and r0, r0, #0xF
    cmp r0, #10
    addlo r0, r0, #'0'
    addhs r0, r0, #('A' - 10)
    bl uart_dbg
    lsl r4, r4, #4
    subs r1, r1, #1
    bne 1b
    pop {r1, r4, pc}

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

    /* Стеки исключений: каждый режим имеет СВОЙ банковский SP (r13_<mode>).
     * Устанавливаем SP в его режиме через CPSR.M; IRQ/FIQ остаются
     * запрещёнными (биты I/F = 1). Стеки растут вниз:
     *   SVC: 0x60000000 (основной, верх)
     *   IRQ: 0x5FF03000
     *   FIQ: 0x5FF02000
     *   ABT: 0x5FF01000
     *   UND: 0x5FF00000
     * Зона 0x5FF00000..0x5FF04000 свободна (EMU_FB 0x5F800000, FB
     * 0x5F900000, стек SVC сверху) — SVC-стек при глубоком кадре не
     * наедет на банки исключений (запас >16 МБ). */
    /* FIQ (CPSR.M=0x11, I|F=1 -> 0xD1) */
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD1
    msr cpsr, r0
    ldr sp, =0x5FF02000
    /* IRQ (0x12 -> 0xD2) */
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD2
    msr cpsr, r0
    ldr sp, =0x5FF03000
    /* ABT (0x17 -> 0xD7) */
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD7
    msr cpsr, r0
    ldr sp, =0x5FF01000
    /* UND (0x1B -> 0xDB) */
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xDB
    msr cpsr, r0
    ldr sp, =0x5FF00000
    /* Возврат в SVC (0x13 -> 0xD3) */
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD3
    msr cpsr, r0

    /* Stack SVC: в конце 512MB DRAM (0x60000000) */
    ldr sp, =0x60000000

    /* DGN '4': стек готов */
    mov r0, #'4'
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

    /* Zero-init когерентной области (.coherent, NOLOAD — BSS-цикл её
     * не покрывает). Идёт ПОСЛЕ кучи, но до main. */
    ldr r0, =_coherent_start
    ldr r1, =_coherent_end
    cmp r0, r1
    beq 7f
6:  cmp r0, r1
    bhs 7f
    str r2, [r0], #4
    b 6b
7:

    /* DGN '5': BSS обнулён */
    mov r0, #'5'
    bl uart_dbg

    /* C++ static constructors (__init_array) — иначе глобалы вроде
     *   unsigned char *scanlineY = get_address(0x8009)
     * остаются NULL, и NGP-рендер пишет в SRAM вместо VRAM.
     * init_array лежит в .text (не в BSS), поэтому не затрётся.
     * Формат: массив ARM-функций (4 байта каждая), blx работает из ARM. */
    ldr r0, =__init_array_start
    ldr r1, =__init_array_end
3:  cmp r0, r1
    beq 4f
    ldr r3, [r0]
    add r0, r0, #4
    push {r0, r1}
    blx r3
    pop {r0, r1}
    b 3b
4:

    /* DGN '6': конструкторы выполнены */
    mov r0, #'6'
    bl uart_dbg
    /* DGN 'A': перед main */
    mov r0, #'A'
    bl uart_dbg

    bl main

_hang:
    b _hang
.end