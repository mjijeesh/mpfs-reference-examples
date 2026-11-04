OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ddr_hart4 (rwx) : ORIGIN = 0x83000000, LENGTH = 15M
}

SECTIONS
{
    .text : {
        *(.text.entry)
        *(.text .text.*)
    } > ddr_hart4

    .rodata : {
        *(.rodata .rodata.*)
    } > ddr_hart4

    .data : {
        __global_pointer$ = . + 0x800;
        *(.data .data.*)
        *(.sdata .sdata.*)
    } > ddr_hart4

    .bss : {
        . = ALIGN(8);
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > ddr_hart4

    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x10000; /* 64 KB Stack */
    __stack_top = .;
}