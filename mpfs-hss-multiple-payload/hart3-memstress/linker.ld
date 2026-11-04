OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ddr_hart3 (rwx) : ORIGIN = 0x82000000, LENGTH = 16M
}

SECTIONS
{
    .text : {
        *(.text.entry)
        *(.text .text.*)
    } > ddr_hart3

    .rodata : {
        *(.rodata .rodata.*)
    } > ddr_hart3

    .data : {
        __global_pointer$ = . + 0x800;
        *(.data .data.*)
        *(.sdata .sdata.*)
    } > ddr_hart3

    .bss : {
        . = ALIGN(8);
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > ddr_hart3

    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x10000; /* 64 KB Stack */
    __stack_top = .;
}