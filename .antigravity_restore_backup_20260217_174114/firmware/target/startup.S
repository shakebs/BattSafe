// Minimal bare-metal startup for THEJAS32 — no libc dependency
// Based on VSD BSP start.S but simplified to avoid syscall traps

        .section .init
        .globl _start
        .type _start,@function

_start:
        la gp, _gp
        la sp, _sp

        /* Load data section (LMA → VMA copy) */
        la a0, _data_lma
        la a1, _data
        la a2, _edata
        bgeu a1, a2, 2f
1:
        lw t0, (a0)
        sw t0, (a1)
        addi a0, a0, 4
        addi a1, a1, 4
        bltu a1, a2, 1b
2:

        /* Clear bss section */
        la a0, __bss_start
        la a1, _end
        bgeu a0, a1, 2f
1:
        sw zero, (a0)
        addi a0, a0, 4
        bltu a0, a1, 1b
2:

        /* Jump straight to main — no atexit/libc_init_array */
        li a0, 0
        li a1, 0
        call main

        /* If main returns, spin forever */
1:      j 1b

/* Provide _init and _fini as no-ops (required by -lc) */
        .globl _init
        .globl _fini
        .type _init,@function
        .type _fini,@function
_init:
_fini:
        ret
