/* The boot_menu simulation's test programs: one section in the program memory */
ENTRY(_start)
MEMORY { ROM (rx) : ORIGIN = 0x20000000, LENGTH = 160K }
SECTIONS { .text : { *(.text*) } > ROM }
