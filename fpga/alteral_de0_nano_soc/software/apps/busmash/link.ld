/* busmash -- data-bus saturation firmware for debug SBA arbitration testing.
 *
 * SRAM (32 KiB) is carved into three disjoint regions so the hart and the
 * debugger provably never touch the same bytes. That disjointness is the whole
 * basis of the cross-talk test: any value either side finds corrupted must have
 * come from the other one.
 *
 *   0x80000000  SRAM  24K   .data/.bss/.stack -- normal program memory
 *   0x80006000  DBG    4K   the DEBUGGER's scratch window; firmware NEVER touches it
 *                           (must match STRESS_RAM in debug/arvern-stress.cfg)
 *   0x80007000  HAM    4K   the hammer region + control block; the DEBUGGER never
 *                           WRITES it (reading is fine and is the point)
 *
 * The control block address is exported as __hammer_cb rather than hardcoded, but
 * debug/arvern-stress.cfg needs it as a literal. The MAGIC word guards that
 * coupling: if these ever drift apart, the script reports "hammer firmware not
 * detected" instead of silently passing a test that is measuring nothing.
 */

ENTRY(_start)

MEMORY
{
    ROM  (rx)  : ORIGIN = 0x20000000, LENGTH = 32K
    SRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 24K
    DBG  (rw)  : ORIGIN = 0x80006000, LENGTH = 4K
    HAM  (rw)  : ORIGIN = 0x80007000, LENGTH = 4K
}

SECTIONS
{
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

    /* Hammer region: 0xF00 bytes of scratch the hart pounds, then the control
     * block the debugger polls. NOLOAD -- nothing to download, the firmware
     * seeds it at startup. */
    .hammer (NOLOAD) : {
        . = ALIGN(4);
        __hammer_start = .;
        . += 0xF00;
        __hammer_end   = .;
        __hammer_cb    = .;          /* = 0x80007F00 */
        . += 0x20;
    } > HAM

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }

    __romdatastart    = LOADADDR(.data);
    __datastart       = ADDR(.data);
    __romdatacopysize = SIZEOF(.data);
    __bssstart        = _bss_start;
    __bssend          = _bss_end;
}
