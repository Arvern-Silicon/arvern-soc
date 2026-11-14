#
# startup.S - minimal C runtime for the FPGA LED demo (bare-metal RISC-V)
#
#   1. set up the stack pointer
#   2. copy the initialized .data section from its ROM load image into SRAM
#   3. zero the .bss section
#   4. call main()
#
# No global pointer (gp) is set up: the linker script does not define
# __global_pointer$, so global accesses use absolute %hi/%lo addressing
# (same convention as the arvern core benchmark projects).
#

.section .init, "ax"
.globl _start
_start:
    la   sp, _stack_end              # 1. stack pointer (top of SRAM stack region)

    # 2. copy initialized data from its ROM load image into SRAM
    la   a0, __romdatastart          #    source      (ROM)
    la   a1, __datastart             #    destination (SRAM)
    la   a2, __romdatacopysize       #    byte count
    beqz a2, 2f
1:  lw   t0, 0(a0)
    sw   t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    addi a2, a2, -4
    bgtz a2, 1b

    # 3. zero the .bss section
2:  la   a0, __bssstart
    la   a1, __bssend
    sub  a2, a1, a0
    beqz a2, 3f
4:  sw   x0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, -4
    bnez a2, 4b

3:  call main                        # 4. enter C
    j    .                           # hang if main ever returns
