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
    ldr pc, =_irq_entry       /* 0x18 IRQ (r764: DMA-аудио) */
    ldr pc, =_hang            /* 0x1C FIQ */

/* ===== Вспомогательная функция: вывод буквы в UART0 THR =====
 * r0 = char. Портим r1, r2, r3.
 * r0.184: ждём готовности TX FIFO (USR.bit1), но НЕ дольше ~100K итераций:
 * если передатчик почему-то встал, не вешаем обработчик исключения —
 * строка D: должна дойти до конца (DFSR/LR/SCTLR).
 */
.section .text
.global uart_dbg
uart_dbg:
    ldr r1, =0x01C2807C      /* UART0_USR */
    movw r2, #0xFFFF         /* таймаут ~65K итераций */
0:  ldr r3, [r1]
    tst r3, #2               /* TX FIFO not full */
    bne 2f
    subs r2, r2, #1
    bne 0b
2:  ldr r1, =0x01C28000
    strb r0, [r1]
    bx lr

/* one-shot-гейт диагностики исключений (r0.185):
 * печатаем D:/P:/U-строку ТОЛЬКО для ПЕРВОГО входа в каждый обработчик.
 * Флаг — SRAM A1 0x18 (вне кэшей, переживает сбой DRAM). Если исключение
 * рекурсивное (повторные аборты на одном сломанном состоянии), повторные
 * входы молча висят — иначе UART заливается мегабайтами «нулей». */
.macro exc_once bail
    ldr r1, =0x18
    ldr r2, [r1]
    cmp r2, #0
    bne \bail
    mov r2, #1
    str r2, [r1]
    dsb
.endm

/* Обработчик Undefined: 'U' + hang */
 .global _undef
 _undef:
     /* r736-ДИАГ: счётчик Undefined в SRAM 0x884 + MPIDR(0x898)/LR_und(0x89C).
      * Адреса 0x880..0x8A8 — вне почты CPU0<->CPU1 (0x18..0x8C) и вне
      * трамплина (0x00..0x14); обнуляются в _start (счётчик = «с бут-а»).
      * r735 писал в 0x28 рядом с почтой — перенесено сюда же блоком. */
1:   ldr  r3, =0x880
     ldr  r2, [r3, #4]
     add  r2, r2, #1
     str  r2, [r3, #4]
     mrc  p15, 0, r2, c0, c0, 5    /* MPIDR — какое ядро упало */
     str  r2, [r3, #24]
     mov  r2, lr                   /* LR_und (фолтящая инструкция ≈ lr-4..8) */
     str  r2, [r3, #28]
     dsb
     exc_once 9f
     mov r0, #'U'
     bl uart_dbg
 9:  b 9b

/* Обработчик Prefetch Abort: 'P:' + IFAR + ':' + IFSR + hang */
.global _prefetch
_prefetch:
    exc_once 9f
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
    /* LR_abt: адрес вызывающего кода — откуда перешли на битый PC.
     * Как F5 в _dataabort: если IFAR=00000000 (переход на NULL), LR укажет
     * место вызова (реальный фолтящий сайт ≈ LR-8). */
    mov r0, #'L'
    bl uart_dbg
    mov r0, #'R'
    bl uart_dbg
    mov r0, #' '
    bl uart_dbg
    ldr r0, [sp, #16]
    bl hex32_uart
    mov r0, #'\n'
    bl uart_dbg
    /* SPSR_abt: CPSR на момент аборта — видно режим и бит Thumb (0x20) */
    mov r0, #'S'
    bl uart_dbg
    mov r0, #'P'
    bl uart_dbg
    mov r0, #' '
    bl uart_dbg
    mrs r0, spsr
    bl hex32_uart
    mov r0, #'\n'
    bl uart_dbg
    /* Адрес возврата из SVC-режима (кто вызвал битый указатель): временно
     * переключаемся в SVC и читаем banked LR_svc. r0-r7 общие для режимов. */
    cps #0x13
    mov r0, lr
    cps #0x17
    push {r0}
    mov r0, #'B'
    bl uart_dbg
    mov r0, #'L'
    bl uart_dbg
    mov r0, #' '
    bl uart_dbg
    pop {r0}
    bl hex32_uart
    mov r0, #'\n'
    bl uart_dbg
    pop {r0-r3, lr}
9:  b 9b

/* Обработчик Data Abort (r0.189): каждое поле ОТДЕЛЬНОЙ строкой — в потоке
 * «нулей» строки F1..F6 находятся и не теряются:
 *   F1 <DFAR>   08 hex  адрес сбойного доступа
 *   F2 <DFSR>   02 hex  статус (01=выравнивание, 04/0E=таблица, 08/0B=доступ)
 *   F3 <IFAR>   08 hex
 *   F4 <IFSR>   02 hex
 *   F5 <LR>     08 hex  ← адрес возврата; фолтящая инструкция ≈ LR-8
 *   F6 <SCTLR>  08 hex
 * Затем hang (exc_once печатает только первый вход). */
.global _dataabort
 _dataabort:
     /* r736-ДИАГ: счётчик Data Abort в SRAM 0x880 + MPIDR(0x888)/DFAR(0x88C)/
      * LR_abt(0x890)/DFSR(0x894) — см. блок в _undef. Данные сохраняем ДО
      * exc_once (иначе повторные аборты на том же состоянии их затирают).
      * r735 писал в 0x24 (конфликт со статусом CPU1) — перенесено. */
1:   ldr  r3, =0x880
     ldr  r2, [r3]
     add  r2, r2, #1
     str  r2, [r3]
     mrc  p15, 0, r2, c0, c0, 5    /* MPIDR */
     str  r2, [r3, #8]
     mrc  p15, 0, r2, c6, c0, 0    /* DFAR */
     str  r2, [r3, #12]
     mov  r2, lr                   /* LR_abt */
     str  r2, [r3, #16]
     mrc  p15, 0, r2, c5, c0, 0    /* DFSR */
     str  r2, [r3, #20]
     dsb
     exc_once 9f
    push {r0-r3, lr}
    /* F1 = DFAR */
    mov r0, #'F'
    bl uart_dbg
    mov r0, #'1'
    bl uart_dbg
    mov r0, #' '
    bl uart_dbg
    mrc p15, 0, r0, c6, c0, 0
    bl hex32_uart
    mov r0, #'\n'
    bl uart_dbg
    /* F2 = DFSR */
    mov r0, #'F'
    bl uart_dbg
    mov r0, #'2'
    bl uart_dbg
    mov r0, #' '
    bl uart_dbg
    mrc p15, 0, r0, c5, c0, 0
    bl hex8_uart
    mov r0, #'\n'
    bl uart_dbg
    /* F3 = IFAR */
    mov r0, #'F'
    bl uart_dbg
    mov r0, #'3'
    bl uart_dbg
    mov r0, #' '
    bl uart_dbg
    mrc p15, 0, r0, c6, c0, 2
    bl hex32_uart
    mov r0, #'\n'
    bl uart_dbg
    /* F4 = IFSR */
    mov r0, #'F'
    bl uart_dbg
    mov r0, #'4'
    bl uart_dbg
    mov r0, #' '
    bl uart_dbg
    mrc p15, 0, r0, c5, c0, 1
    bl hex8_uart
    mov r0, #'\n'
    bl uart_dbg
    /* F5 = LR_abt */
    mov r0, #'F'
    bl uart_dbg
    mov r0, #'5'
    bl uart_dbg
    mov r0, #' '
    bl uart_dbg
    ldr r0, [sp, #16]
    bl hex32_uart
    mov r0, #'\n'
    bl uart_dbg
    /* F6 = SCTLR */
    mov r0, #'F'
    bl uart_dbg
    mov r0, #'6'
    bl uart_dbg
    mov r0, #' '
    bl uart_dbg
    mrc p15, 0, r0, c1, c0, 0
    bl hex32_uart
    mov r0, #'\n'
    bl uart_dbg
    pop {r0-r3, lr}
9:  b 9b

/* Вывести число r0 (мл. 8 бит) как 2 hex-цифры (0-9A-F).
 * r0.249: значение держим в r4, не в r1 — uart_dbg затирает r1, из-за чего
 * младшая цифра (IFSR/DFSR) всегда печаталась как '0'. */
.global hex8_uart
hex8_uart:
    push {r1, r4, lr}
    mov r4, r0
    lsr r0, r4, #4
    and r0, r0, #0xF
    cmp r0, #10
    addlo r0, r0, #'0'
    addhs r0, r0, #('A' - 10)
    bl uart_dbg
    and r0, r4, #0xF
    cmp r0, #10
    addlo r0, r0, #'0'
    addhs r0, r0, #('A' - 10)
    bl uart_dbg
    pop {r1, r4, pc}

/* Вывести число r0 как 8 hex-цифр (32 бита).
 * r0.249: счётчик храним НА СТЕКЕ, не в r1 — uart_dbg затирает r1
 * (ldr r1,=UART_USR / UART_THR), поэтому прежний цикл (mov r1,#8 …
 * subs r1,#1; bne) НИКОГДА не заканчивался и заливал UART мегабайтами '0'
 * после первого же F1/P: — из-за этого не видели ни IFSR, ни LR. */
.global hex32_uart
hex32_uart:
    push {r1, r4, lr}
    mov r4, r0
    mov r1, #8
    str r1, [sp, #-4]!
1:  mov r0, r4, lsr #28
    and r0, r0, #0xF
    cmp r0, #10
    addlo r0, r0, #'0'
    addhs r0, r0, #('A' - 10)
    bl uart_dbg
    lsl r4, r4, #4
    ldr r1, [sp]
    subs r1, r1, #1
    str r1, [sp]
    bne 1b
    add sp, sp, #4
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

    /* r713: сброс one-shot гейта исключений (SRAM A1 0x18, exc_once).
     * Раньше флаг НЕ обнулялся: U-Boot оставлял в нём мусор (наненулевое),
     * из-за чего ПЕРВЫЙ же Data Abort (напр. у CPU2 при чтении I2S_FIFO_STA)
     * считался «повторным» и уходил в ТИХИЙ hang без печати F1..F6 — адрес
     * падения терялся. SRAM вне кэшей, сразу после загрузчика в 0x40000000. */
    mov r1, #0x18
    mov r2, #0
    str r2, [r1]
    dsb
    /* r736-ДИАГ: обнуляем блок счётчиков исключений 0x880..0x8A8 (8 слов).
     * SRAM не zero-инициализируется — без этого значения «с прошлого бут-а»
     * (мусор) искажали бы диагностику. exc_once (0x18) тоже входит в очистку. */
    ldr r1, =0x880
    ldr r4, =0x8A8
    mov r2, #0
3:  str r2, [r1], #4
    cmp r1, r4
    blo 3b
    dsb

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

    /* r722 (КОРЕНЬ аудио-проблемы): перенос L1-таблицы страниц ИЗ зоны стека.
     * U-Boot кладёт TTBR0 в 0x5FFF4000 — всего в 48 КБ ниже SVC-стека core0
     * (0x60000000, растёт ВНИЗ). Глубокие вызовы (printf/fat/GBA-загрузка)
     * уводили стек ниже 0x5FFFB000 и ЗАТИРАЛИ таблицу страниц → MMU деградировал,
     * секция .coherent теряла uncached (core0 читал stale из D-cache и не видел
     * записи CPU2 — «канал рвётся», «CPU2 завис», шум, зависания в разных местах;
     * на стенде: coherent_l1=0xFFF9FFF3 — мусор вместо легального 0x4A4xxxx3).
     * Переносим таблицу в uncached-хвост .coherent (0x4A40C000 — после
     * _coherent_end, до кучи 0x4A500000) и переключаем TTBR0. Стек core0
     * остаётся на 0x60000000 — теперь он безопасен. Выполняем ДО установки
     * стека (стек ещё не используется) и ДО любых глубоких C-вызовов. */
    mrc p15, 0, r0, c2, c0, 0        /* текущий TTBR0 (база L1 U-Boot) */
    ldr r1, =0x4A40C000              /* новый адрес таблицы (.coherent, uncached) */
    mov r2, #4096                    /* 4096 слов = 16 КБ */
1:  ldr r4, [r0], #4
    str r4, [r1], #4
    subs r2, r2, #1
    bne 1b
    dsb
    /* clean+invalidate копии (иначе аппаратный walker, идущий в DRAM
     * напрямую, не увидит dirty-строки D-cache core0 до mmu_mark_uncached) */
    ldr r0, =0x4A40C000
    ldr r1, =0x4A410000              /* +16 КБ */
2:  mcr p15, 0, r0, c7, c14, 1       /* DCCIMVAC */
    add r0, r0, #32
    cmp r0, r1
    blo 2b
    dsb
    ldr r1, =0x4A40C000
    mcr p15, 0, r1, c2, c0, 0        /* TTBR0 = новая таблица */
    dsb
    isb
    mov r4, #0
    mcr p15, 0, r4, c8, c7, 0        /* invalidate TLB (все) */
    dsb
    isb

    /* Stack SVC: в конце 512MB DRAM (0x60000000) */
    ldr sp, =0x60000000

    /* DGN '4': стек готов */
    mov r0, #'4'
    bl uart_dbg

    /* Инвалидируем I-cache + DSB+ISB */
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    isb

    /* BSS = 0 (ARM mode). r0.417 (O3): NEON-обнуление — до 16-байтной
     * границы словами, дальше 16 байт за раз (VST1), хвост — словами.
     * VFP/NEON включён выше, q0 = 0. */
    ldr r0, =_bstart1
    ldr r1, =_bend1
    mov r2, #0
    vmov.i32 q0, #0
1:  cmp r0, r1
    bhs 5f
    tst r0, #15
    bne 2f
    sub r3, r1, r0
    cmp r3, #16
    bhs 3f
2:  str r2, [r0], #4
    b 1b
3:  vst1.8 {q0}, [r0]!
    b 1b
5:

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

/* ---- r764: IRQ-диспетчер (GIC-400, DMA-аудио на CPU2) ----
 * Эталонная реализация (внешний аудит): srsdb/rfeia + выравнивание стека 8
 * (AAPCS) + spurious 1023 + EOI сразу в asm (мин. лаг, не зависит от C).
 * gic_dispatch(intid) — C-обработчик в gic.c (r0 = INTID).
 */
.section .text
.align 5
.global _irq_entry
_irq_entry:
    sub     lr, lr, #4              /* IRQ: LR = PC+8 → минус 4 = адрес возврата */
    srsdb   sp!, #0x12              /* сохранить SPSR_irq и LR_irq на сп. IRQ */
    push    {r0-r3, r12}            /* scratch-регистры */
    mov     r0, sp
    and     r1, r0, #4
    sub     sp, sp, r1              /* выравнивание стека по 8 (AAPCS) */
    push    {r1, lr}

    ldr     r0, =0x01C82000         /* GIC CPUIF */
    ldr     r1, [r0, #0x0C]         /* GICC_IAR = INTID */
    ldr     r2, =1023
    cmp     r1, r2
    beq     _irq_spurious           /* ложное прерывание — не обрабатывать */

    mov     r0, r1                  /* r0 = INTID → C-диспетчеру */
    bl      gic_dispatch

    ldr     r0, =0x01C82000
    str     r1, [r0, #0x10]         /* GICC_EOIR = INTID (тот же ID!) */

_irq_spurious:
    pop     {r1, lr}
    add     sp, sp, r1              /* вернуть выравнивание */
    pop     {r0-r3, r12}
    rfeia   sp!                     /* восстановить SPSR+PC, вернуться */

/* ---- Точка входа CPU1 (TFT core) ----
 * Запускается h3_cpu_start(1, cpu1_entry):
 *  1) ядро выходит из сброса с выключенными MMU/кэшами
 *  2) ставим SVC-mode стек (отдельная область ниже исключений CPU0)
 *  3) прыгаем в tft_core_main — инициализация ILI9486 + цикл даунскейла
 */
.section .text
.global cpu1_entry
cpu1_entry:
    /* «Почта» в SRAM A1: ядро пишет magic первой инструкцией —
     * core0 ждёт его в h3_secondary_start (SRAM вне кэшей, видно сразу).
     * Статус процессора — в 0x24 (1=вошёл). */
    mov r2, #0x20
    ldr r3, =0x13579BDF
    str r3, [r2]
    mov r2, #0x24
    mov r3, #1
    str r3, [r2]
    /* VBAR = таблица векторов core0 (0x40000000). Иначе исключение на CPU1
     * уходит в SRAM-трамплин (0x0) тихо, без следов — P:/D: в UART не увидим. */
    ldr r0, =_vectors
    mcr p15, 0, r0, c12, c0, 0
    dsb
    isb
    /* DGN 'C': CPU1 вошёл в трамплин (MMU выключен, сырые адреса).
     * Ждём, пока TX FIFO UART не заполнен — иначе байт теряется,
     * если core0 в этот момент допечатывает свою строку. */
    ldr r1, =0x01C2807C      /* UART0_USR */
0:  ldr r0, [r1]
    tst r0, #2               /* TX FIFO not full */
    beq 0b
    mov r0, #'C'
    ldr r1, =0x01C28000      /* UART0_THR */
    strb r0, [r1]
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD3       /* SVC, IRQ/FIQ off */
    msr cpsr, r0
    ldr sp, =0x5FE01000     /* стек CPU1: 4 МБ ниже верхнего банка */
    /* Стеки исключений CPU1. Обязательно: обработчики _undef/_prefetch/
     * _dataabort (UART-лог P:/D:) используют банковские SP режимов, иначе
     * сам обработчик упадёт. Адреса — ниже SVC-стека CPU1 (0x5FE01000),
     * не пересекаются с банками core0 (0x5FF0x000) и с EMU_FB. */
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xDF       /* FIQ (0x11), I|F=1 */
    msr cpsr, r0
    ldr sp, =0x5FE00000
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD2       /* IRQ (0x12) */
    msr cpsr, r0
    ldr sp, =0x5FDFF000
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD7       /* ABT (0x17) */
    msr cpsr, r0
    ldr sp, =0x5FDFE000
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xDB       /* UND (0x1B) */
    msr cpsr, r0
    ldr sp, =0x5FDFD000
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD3       /* обратно в SVC */
    msr cpsr, r0
    /* VFP/NEON для CPU1: ядро выходит из сброса с CPACR=0 и FPEXC.EN=0.
     * С -O2 -mfpu=neon tft_core_main начинается с vpush {d8-d9} — без
     * этого разрешения первая же инструкция даёт UNDEF, а VBAR CPU1 ещё
     * указывает на SRAM-трамплин (0x00000000) — бесконечный ревход.
     * Включаем так же, как core0 в _start. */
    mrc p15, 0, r0, c1, c0, 2
    orr r0, r0, #(0xF << 20)      /* CP10, CP11 full access */
    mcr p15, 0, r0, c1, c0, 2
    isb
    mov r0, #0x40000000
    vmsr fpexc, r0                /* FPEXC.EN = 1 */
    isb
    mov r2, #0x24
    mov r3, #0x0E          /* st=0x0E — «сейчас прыгнем в tft_core_main» */
    str r3, [r2]
    ldr r0, =tft_core_main /* прямой прыжок через литерал (вместо bl) */
    bx  r0
    mov r2, #0x24
    mov r3, #0xEF           /* st=0xEF — вернулись (не должно) */
    str r3, [r2]
2:  b 2b

/* ---- Точка входа CPU2 (audio core) ----
 * Ф1 (r585): аудио-ядро. Запускается h3_cpu_start(2, cpu2_entry):
 *  1) выходит из сброса с выключенными MMU/кэшами
 *  2) SVC-стек (отдельная область ниже стека CPU1)
 *  3) VFP/NEON (как cpu1_entry) — иначе Undefined на первой C-инструкции
 *  4) прыжок в cpu2_audio_entry (audio_core.c) — вечный цикл долива I2S.
 * MMU выключен: кольцо/почта в .coherent доступны по физическим адресам.
 * VBAR ставим на таблицу векторов core0 (исключения дадут P:/D: в UART).
 */
.section .text
.global cpu2_entry
cpu2_entry:
    /* «Почта» в SRAM A1: h3_secondary_start (h3_smp.c) ждёт CPU1_MAGIC
     * в 0x20 ПОСЛЕ release — записываем тот же magic 0x13579BDF. */
    mov r2, #0x20
    ldr r3, =0x13579BDF
    str r3, [r2]
    /* Статус аудио-ядра — СВОЯ ячейка 0x90 (SRAM A1, uncached), чтобы не
     * пересекаться со статусом CPU1 (0x24, TFT_STAT). Значение 0xA2 —
     * «CPU2 в трамплине, иду в cpu2_audio_entry». Диагностика: core0 может
     * читать 0x90 как признак успешного подъёма аудио-ядра. */
    mov r2, #0x90
    mov r3, #0xA2
    str r3, [r2]
    ldr r0, =_vectors
    mcr p15, 0, r0, c12, c0, 0
    dsb
    isb
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD3       /* SVC, IRQ/FIQ off */
    msr cpsr, r0
    ldr sp, =0x5FC01000     /* стек CPU2 audio: 4 МБ ниже стека/банков CPU1 */
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xDF       /* FIQ */
    msr cpsr, r0
    ldr sp, =0x5FC00000
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD2       /* IRQ */
    msr cpsr, r0
    ldr sp, =0x5FBFF000
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD7       /* ABT */
    msr cpsr, r0
    ldr sp, =0x5FBFE000
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xDB       /* UND */
    msr cpsr, r0
    ldr sp, =0x5FBFD000
    mrs r0, cpsr
    bic r0, r0, #0x1F
    orr r0, r0, #0xD3       /* обратно в SVC */
    msr cpsr, r0
    /* VFP/NEON для CPU2 — как cpu1_entry (CPACR + FPEXC.EN) */
    mrc p15, 0, r0, c1, c0, 2
    orr r0, r0, #(0xF << 20)
    mcr p15, 0, r0, c1, c0, 2
    isb
    mov r0, #0x40000000
    vmsr fpexc, r0
    isb
    ldr r0, =cpu2_audio_entry
    bx  r0
2:  b 2b
.end