/* Absolute minimum UART test — pure assembly, no C, no libc.
 * Just writes 'A' to UART0 THR in an infinite loop.
 * If this doesn't produce output, either:
 *   1. Code isn't executing
 *   2. UART addresses are wrong
 *   3. XMODEM transfer is broken
 */

        .section .init
        .globl _start
        .type _start,@function

_start:
        /* Set stack pointer to safe value (top of SRAM) */
        lui     sp, 0x240        /* sp = 0x00240000 (top of 256KB SRAM) */

        /* UART0 base = 0x10000100 */
        lui     t0, 0x10000      /* t0 = 0x10000000 */
        addi    t0, t0, 0x100    /* t0 = 0x10000100 (UART0 base) */

        /* First re-init UART: 8N1, no interrupts, FIFOs on */
        /* LCR = 0x0C offset → addr 0x1000010C */
        li      t2, 0x03         /* 8-bit, no parity, 1 stop = 0x03 */
        sw      t2, 0x0C(t0)    /* Write to UART0_LCR */

        /* IER = 0x04 offset → disable all interrupts */
        sw      zero, 0x04(t0)  /* UART0_IER = 0 */

        /* FCR = 0x08 offset → enable + clear FIFOs */
        li      t2, 0x07         /* Enable FIFO + clear Rx/Tx FIFO */
        sw      t2, 0x08(t0)    /* UART0_FCR */

        /* Now loop: write characters */
        li      a0, 0x48         /* 'H' */
        li      a1, 0x49         /* 'I' */
        li      a2, 0x0A         /* '\n' */

send_H:
        lw      t3, 0x14(t0)     /* Read UART0_LSR */
        andi    t3, t3, 0x20     /* Check THRE (bit 5) */
        beqz    t3, send_H       /* Wait if Tx not empty */
        sw      a0, 0x00(t0)     /* Write 'H' to THR */

send_I:
        lw      t3, 0x14(t0)
        andi    t3, t3, 0x20
        beqz    t3, send_I
        sw      a1, 0x00(t0)     /* Write 'I' */

send_NL:
        lw      t3, 0x14(t0)
        andi    t3, t3, 0x20
        beqz    t3, send_NL
        sw      a2, 0x00(t0)     /* Write '\n' */

        /* Simple delay */
        li      t4, 5000000
delay:
        addi    t4, t4, -1
        bnez    t4, delay

        j       send_H           /* Loop forever */
