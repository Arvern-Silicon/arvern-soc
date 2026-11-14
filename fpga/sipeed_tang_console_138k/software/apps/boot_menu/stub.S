#
# stub.S - launch routine of the boot menu, in the last 256 bytes of the
# program memory (section .stub, see link.ld), which no app image reaches.
#
#   boot_launch(a0 = source, a1 = byte count, a multiple of 4, a2 = destination)
#
# Copies the staged part of the app image from SRAM to its place in the
# program memory, over the loader block (the rest of the image is already in
# place), then starts the app at the reset address. Registers only: no
# stack, no call back into the menu. Interrupts must be off.
#

.section .stub, "ax"
.globl boot_launch
boot_launch:
    beqz a1, 2f
1:  lw   t1, 0(a0)
    sw   t1, 0(a2)
    addi a0, a0, 4
    addi a2, a2, 4
    addi a1, a1, -4
    bnez a1, 1b
2:  .word 0x0000100f                 # fence.i: fetch the new code
    li   t0, 0x20000000
    jr   t0
