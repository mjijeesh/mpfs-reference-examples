.section .text.entry, "ax"
.global _start
.type _start, @function

_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* Setup private stack pointer */
    la sp, __stack_top

    /* Zero out BSS section */
    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:
    fence rw, rw
    fence.i

    tail main

3:
    wfi
    j 3b