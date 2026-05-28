/*
 * SiliconV Boot Stub — ARM64
 *
 * Minimal code that runs at EL1 and prints "Hello SiliconV" to PL011 UART.
 * This is the first guest code — validates UART, GIC, and boot flow.
 *
 * Entry: x0 = DTB address (ignored for now)
 * UART: PL011 at 0x10000000
 */

    .section .text
    .global _start

_start:
    /* Save DTB pointer */
    mov     x19, x0

    /* Disable interrupts */
    msr     daifset, #0xf

    /* -------------------------------------------------------
     * Print banner via PL011 UART at 0x10000000
     * PL011 register offsets (32-bit access):
     *   0x000 = DR (Data Register)
     *   0x018 = FR (Flag Register) — bit 5 = TXFF (TX FIFO Full)
     *   0x030 = CR (Control Register) — bit 0 = UARTEN
     *   0x024 = IBRD (Integer Baud Rate)
     *   0x028 = FBRD (Fractional Baud Rate)
     *   0x02C = LCR_H (Line Control)
     *   0x034 = IMSC (Interrupt Mask)
     *   0x038 = RIS (Raw Interrupt Status)
     *   0x044 = ICR (Interrupt Clear)
     * ------------------------------------------------------- */

    mov     x1, #0x10000000         /* UART base */

    /* Disable UART */
    mov     w2, #0x0
    str     w2, [x1, #0x030]       /* CR = 0 */

    /* Wait for TX FIFO to drain */
    bl      uart_wait_tx

    /* Set baud rate: 115200 @ 24MHz clock
     * IBRD = 24000000 / (16 * 115200) = 13.02
     * IBRD = 13, FBRD = round(0.02 * 64) = 1
     */
    mov     w2, #13
    str     w2, [x1, #0x024]       /* IBRD */
    mov     w2, #1
    str     w2, [x1, #0x028]       /* FBRD */

    /* 8N1, FIFO enabled */
    mov     w2, #0x70              /* 8 bits, FIFO enable */
    str     w2, [x1, #0x02C]       /* LCR_H */

    /* Mask all interrupts */
    mov     w2, #0x0
    str     w2, [x1, #0x038]       /* ICR (clear) */
    str     w2, [x1, #0x034]       /* IMSC (mask all) */

    /* Enable UART, TX, RX */
    mov     w2, #0x301             /* UARTEN | TXE | RXE */
    str     w2, [x1, #0x030]       /* CR */

    /* Print the banner */
    adr     x2, banner
    bl      uart_puts

    /* Print DTB address */
    adr     x2, dtb_msg
    bl      uart_puts
    mov     x0, x19
    bl      uart_puthex

    /* Print newline */
    mov     w0, #'\n'
    bl      uart_putc

    /* Halt — spin forever */
halt:
    wfi
    b       halt

/* -------------------------------------------------------
 * uart_putc — write character in w0
 * ------------------------------------------------------- */
uart_putc:
    mov     x1, #0x10000000
1:  ldr     w3, [x1, #0x018]       /* FR */
    tbnz    w3, #5, 1b             /* Wait while TXFF */
    str     w0, [x1, #0x000]       /* DR */
    ret

/* -------------------------------------------------------
 * uart_wait_tx — wait for TX FIFO empty
 * ------------------------------------------------------- */
uart_wait_tx:
    mov     x1, #0x10000000
1:  ldr     w3, [x1, #0x018]       /* FR */
    tbnz    w3, #3, 1b             /* Wait while BUSY */
    ret

/* -------------------------------------------------------
 * uart_puts — write null-terminated string at x2
 * ------------------------------------------------------- */
uart_puts:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
1:  ldrb    w0, [x2], #1
    cbz     w0, 2f
    bl      uart_putc
    b       1b
2:  ldp     x29, x30, [sp], #16
    ret

/* -------------------------------------------------------
 * uart_puthex — print 64-bit value in x0 as hex
 * ------------------------------------------------------- */
uart_puthex:
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    mov     x29, sp
    mov     x19, x0
    mov     x20, #60               /* Bit shift counter */

    /* Print "0x" */
    mov     w0, #'0'
    bl      uart_putc
    mov     w0, #'x'
    bl      uart_putc

1:  lsr     x0, x19, x20
    and     w0, w0, #0xf
    cmp     w0, #10
    b.lt    2f
    add     w0, w0, #'a' - 10
    b       3f
2:  add     w0, w0, #'0'
3:  bl      uart_putc
    subs    x20, x20, #4
    b.ge    1b

    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret

/* -------------------------------------------------------
 * Data
 * ------------------------------------------------------- */
    .section .rodata
banner:
    .asciz  "SiliconV v0.1 — Hello, World!\n"
dtb_msg:
    .asciz  "DTB at: "
