/* Minimal pure-assembly UART hello-world for THEJAS32 */
        .section .init
        .globl _start
        .type _start,@function

_start:
        /* Stack at top of 256KB SRAM (0x200000 + 0x40000) */
        lui     sp, 0x240

        /* UART0 base = 0x10000100 */
        lui     t0, 0x10000
        addi    t0, t0, 0x100

        /* UART init: 8N1, IRQ off, FIFO on */
        li      t2, 0x03
        sw      t2, 0x0C(t0)
        sw      zero, 0x04(t0)
        li      t2, 0x07
        sw      t2, 0x08(t0)

loop_msg:
        la      s0, msg
send_next:
        lbu     a0, 0(s0)
        beqz    a0, delay

wait_thre:
        lw      t3, 0x14(t0)
        andi    t3, t3, 0x20
        beqz    t3, wait_thre

        sw      a0, 0x00(t0)
        addi    s0, s0, 1
        j       send_next

delay:
        li      t4, 8000000
wait_loop:
        addi    t4, t4, -1
        bnez    t4, wait_loop
        j       loop_msg

        .section .rodata
msg:
        .asciz "Hello world from THEJAS32!\r\n"
