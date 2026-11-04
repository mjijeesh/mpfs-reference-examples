OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ddr_hart2 (rwx) : ORIGIN = 0x81000000, LENGTH = 16M
}

SECTIONS
{
    .text : {
        *(.text.entry)
        *(.text .text.*)
    } > ddr_hart2

    .rodata : {
        *(.rodata .rodata.*)
    } > ddr_hart2

    .data : {
        __global_pointer$ = . + 0x800;
        *(.data .data.*)
        *(.sdata .sdata.*)
    } > ddr_hart2

    .bss : {
        . = ALIGN(8);
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > ddr_hart2

    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x10000; /* 64 KB Stack */
    __stack_top = .;
}