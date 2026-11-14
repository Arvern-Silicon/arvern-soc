/*
 * Linker script for the boot menu (Tang Console 138K)
 *
 *   ROM     0x20000000  the menu: startup, interface, sound, graphics, USB,
 *                       and the .data load image
 *   LOADER  0x20023F00  the loader block (loader.h): the loader and all it
 *                       calls (FatFs, SD-card layer, SPI driver, C and
 *                       compiler support libraries), with their read-only data
 *   STUB    0x20027F00  the launch routine (stub.S): the last 256 bytes of the
 *                       program memory, which an app image never reaches
 *   SRAM    0x80000000  .data, .bss, the stack, then the staging area: the
 *                       part of an app image that overlaps the loader block
 *
 * An app image replaces the menu: its bytes below and above the loader block
 * are read straight into place, the ones over it are staged.
 */

ENTRY(_start)

MEMORY
{
    ROM    (rx) : ORIGIN = 0x20000000, LENGTH = 160K - 256 - 16K
    LOADER (rx) : ORIGIN = 0x20023F00, LENGTH = 16K
    STUB   (rx) : ORIGIN = 0x20027F00, LENGTH = 256
    SRAM   (rw) : ORIGIN = 0x80000000, LENGTH = 128K
}

SECTIONS
{
    /* First: these input sections are taken before the menu's .text */
    .loader : {
        _loader_start = .;
        */loader.o(.text* .rodata* .srodata*)
        */ff.o(.text* .rodata* .srodata*)
        */sd_diskio.o(.text* .rodata* .srodata*)
        */apb_spi_sd.o(.text* .rodata* .srodata*)
        */apb_spi.o(.text* .rodata* .srodata*)
        *libc.a:*(.text* .rodata* .srodata*)
        *libc_nano.a:*(.text* .rodata* .srodata*)
        *libgcc.a:*(.text* .rodata* .srodata*)
        . = ALIGN(4);
        _loader_end = .;
    } > LOADER

    .text : {
        . = ALIGN(4);
        KEEP(*(.init))
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
    } > ROM

    .data : {
        . = ALIGN(4);
        _data_start = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        _data_end = .;
    } > SRAM AT > ROM

    .stub : {
        KEEP(*(.stub))
    } > STUB

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > SRAM

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

    __romdatastart    = LOADADDR(.data);
    __datastart       = ADDR(.data);
    __romdatacopysize = SIZEOF(.data);
    __bssstart        = _bss_start;
    __bssend          = _bss_end;

    _menu_rom_end = LOADADDR(.data) + SIZEOF(.data);
    _stage_start  = _stack_end;
    _stage_end    = ORIGIN(SRAM) + LENGTH(SRAM);
    _app_max      = ORIGIN(STUB) - ORIGIN(ROM);
    ASSERT(_menu_rom_end <= _loader_start, "the menu overlaps the loader block")
    /* The staged part: the loader block widened to sector boundaries, plus
       the zeroed tail word */
    _loader_staged = ((_loader_end - ORIGIN(ROM) + 511) & ~511) - ((_loader_start - ORIGIN(ROM)) & ~511);
    ASSERT(_loader_staged + 4 <= _stage_end - _stage_start,
           "the loader block is larger than the staging area")
    /* The SD link's buffer and mailbox (sdlink.h) at the top of the staging
       area: what a launch stages stays below them */
    ASSERT(_stage_start + _loader_staged + 4 <= 0x80010000,
           "the staged part reaches the SD link's buffer")
}
