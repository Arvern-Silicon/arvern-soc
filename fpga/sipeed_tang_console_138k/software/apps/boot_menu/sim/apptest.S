#
# apptest.S - test program of the boot_menu simulation, put on the simulated
# card as an APP.BIN. It sums the 32-bit words of its own image (in the
# program memory, from 0x20000000 up to app_sum) and compares the result with
# app_sum, which sim/gen_card.py fills in: the LED register shows APP_ID when
# they match, 0xEE when they do not. APP_PAD bytes of padding size the image:
# past the menu's loader block, a launch moves all three parts (read in place
# below the block, staged over it, read in place above it).
#

.section .text
.option norvc                        # 4-byte words throughout: app_sum ends the image
.globl _start
_start:
    csrsi 0x744, 8                   # mnstatus.NMIE = 1
    csrw  mstatush, x0               # mstatus.MDT   = 0
    li    a0, 0x20000000
    la    a1, app_sum
    li    a2, 0
1:  lw    t0, 0(a0)
    add   a2, a2, t0
    addi  a0, a0, 4
    bltu  a0, a1, 1b
    lw    t0, 0(a1)
    li    t1, 0x10040000             # LED register
    li    t2, APP_ID
    beq   a2, t0, 2f
    li    t2, 0xEE
2:  sw    t2, 0(t1)
    j     .

    .p2align 2
pad:
    .rept APP_PAD / 4
    .word (0x9E3779B9 * (. - pad) + 0x7F4A7C15) & 0xFFFFFFFF
    .endr
app_sum:
    .word 0
