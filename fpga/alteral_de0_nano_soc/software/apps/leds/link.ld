ENTRY(_start)

MEMORY
{
    ROM  (rx)  : ORIGIN = 0x20000000, LENGTH = 32K
    SRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 32K
}

SECTIONS
{
    /* Code and read-only data go to ROM */
    .text : {
        . = ALIGN(4);
        KEEP(*(.init))
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
    } > ROM

    /* Initialized data: runtime location in SRAM, load image in ROM */
    .data : {
        . = ALIGN(4);
        _data_start = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        _data_end = .;
    } > SRAM AT > ROM

    /* Uninitialized data (zeroed by startup.S) in SRAM */
    .bss (NOLOAD) : {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > SRAM

    /* Stack region (initial SP = top of this region) */
    .stack (NOLOAD) : {
        . = ALIGN(4);
        . += 4K;
        . = ALIGN(4);
        _stack_end = .;
    } > SRAM

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }

    /* Startup symbols (consumed by startup.S) */
    __romdatastart    = LOADADDR(.data);   /* init values in ROM            */
    __datastart       = ADDR(.data);       /* runtime location in SRAM      */
    __romdatacopysize = SIZEOF(.data);     /* bytes to copy ROM -> SRAM     */
    __bssstart        = _bss_start;
    __bssend          = _bss_end;
}
