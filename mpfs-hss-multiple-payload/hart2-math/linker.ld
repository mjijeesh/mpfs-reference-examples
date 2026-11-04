OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ddr_hart (rwx) :ORIGIN = 0x80010000, LENGTH = 64K
}

SECTIONS
{
    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
    } > ddr_hart

    .rodata : {
        *(.rodata .rodata.*)
    } > ddr_hart

    .data : {
        __global_pointer$ = . + 0x800;
        *(.data .data.*)
        *(.sdata .sdata.*)
    } > ddr_hart

    .bss : {
        . = ALIGN(8);
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > ddr_hart

    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x10000; /* 64 KB Stack */
    __stack_top = .;
}