/* maria_asm.S - hand-written ARM Thumb for the hottest MARIA primitive.
 *
 * maria_StoreCell() is called up to ~55k times per frame for heavy 7800
 * games (Donkey Kong); the out-of-line C call plus per-pixel branch cost
 * real time on the RP2040 (Cortex-M0+, 133MHz). These two functions keep
 * the exact semantics of the C version (see maria_StoreCell in Maria.cpp):
 *
 *   maria_store_cell1_asm(data):
 *     if (horizontal < 160) { if (data) lineRAM[h]=palette|data;
 *                             else if (kmode) lineRAM[h]=0; }
 *     horizontal++;
 *
 *   maria_store_cell2_asm(high, low):
 *     if (horizontal < 160) { if (high||low) lineRAM[h]=palette&16|high|low;
 *                             else if (kmode) lineRAM[h]=0; }
 *     horizontal++;
 *
 * Thumb-1 only (Cortex-M0+): no cbz/cbnz/wide immediates. Called only on ARM
 * builds; the host C fallback in Maria.cpp keeps host tests identical.
 */

    .syntax unified
    .thumb
    .cpu cortex-m0plus

    .text

/* --- maria_store_cell1_asm(unsigned char data) --- */
    .align 2
    .thumb_func
    .global maria_store_cell1_asm
    .type   maria_store_cell1_asm, %function
maria_store_cell1_asm:
    ldr     r3, =maria_horizontal      /* r3 = &maria_horizontal */
    ldrb    r2, [r3]                   /* r2 = horizontal */
    cmp     r2, #160                   /* h < 160? */
    bhs     2f
    cmp     r0, #0
    bne     1f                         /* data != 0 -> store palette|data */
    /* data == 0: if kmode -> lineRAM[h] = 0 */
    ldr     r1, =maria_kmode
    ldrb    r1, [r1]
    cmp     r1, #0
    beq     2f
    ldr     r1, =maria_lineRAM
    movs    r0, #0
    strb    r0, [r1, r2]
    b       2f
1:
    /* lineRAM[h] = palette | data */
    ldr     r1, =maria_palette
    ldrb    r1, [r1]
    orrs    r0, r1                     /* r0 = palette | data */
    ldr     r1, =maria_lineRAM
    strb    r0, [r1, r2]
2:
    adds    r2, #1                     /* horizontal++ */
    strb    r2, [r3]
    bx      lr

    .size   maria_store_cell1_asm, .-maria_store_cell1_asm

/* --- maria_store_cell2_asm(unsigned char high, unsigned char low) --- */
    .align 2
    .thumb_func
    .global maria_store_cell2_asm
    .type   maria_store_cell2_asm, %function
maria_store_cell2_asm:
    /* r0 = high, r1 = low */
    ldr     r3, =maria_horizontal
    ldrb    r2, [r3]                   /* r2 = horizontal */
    cmp     r2, #160
    bhs     5f
    orrs    r0, r1                     /* r0 = high|low */
    cmp     r0, #0
    bne     4f                         /* if (high||low) store */
    /* high==0 && low==0: if kmode -> lineRAM[h]=0 */
    ldr     r1, =maria_kmode
    ldrb    r1, [r1]
    cmp     r1, #0
    beq     5f
    ldr     r1, =maria_lineRAM
    movs    r0, #0
    strb    r0, [r1, r2]
    b       5f
4:
    /* lineRAM[h] = (palette & 16) | high | low
     * r0 = high|low, r2 = horizontal, r3 = &horizontal */
    push    {r4}                       /* r4 is callee-saved */
    ldr     r4, =maria_palette
    ldrb    r4, [r4]                   /* r4 = palette */
    movs    r1, #16
    ands    r4, r1                     /* r4 = palette & 16 */
    orrs    r0, r4                     /* r0 = (palette&16)|high|low */
    ldr     r1, =maria_lineRAM
    strb    r0, [r1, r2]               /* lineRAM[h] = r0 */
    pop     {r4}
    b       5f
5:
    adds    r2, #1                      /* horizontal++ */
    strb    r2, [r3]
    bx      lr

    .size   maria_store_cell2_asm, .-maria_store_cell2_asm

/* Literal pool for the `ldr rX, =symbol` pseudo-ops above. */
    .ltorg
    .end
