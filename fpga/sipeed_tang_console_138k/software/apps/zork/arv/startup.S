/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      startup.S (zork)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * C runtime of the Zork port (bare-metal RV32E):
 *   0. double-trap boot sequence (arm mnstatus.NMIE, then clear mstatush.MDT)
 *   1. the trap vector (arv_trap_entry), the stack pointer
 *   2. .data copied from its ROM image into the SRAM, .bss zeroed
 *   3. main()
 * arv_trap_entry saves the registers in a frame on the stack and calls
 * arv_trap(frame, mcause, mepc, mtval) (trap.c), which returns the PC to
 * resume at.
 *----------------------------------------------------------------------------*/

.section .init, "ax"
.globl _start
_start:
    csrsi 0x744, 8                   /* mnstatus.NMIE = 1                   */
    csrw  mstatush, x0               /* mstatus.MDT   = 0                   */
    la    t0, arv_trap_entry
    csrw  mtvec, t0
    la    sp, _stack_end

    la    a0, __romdatastart
    la    a1, __datastart
    la    a2, __romdatacopysize
    beqz  a2, 2f
1:  lw    t0, 0(a0)
    sw    t0, 0(a1)
    addi  a0, a0, 4
    addi  a1, a1, 4
    addi  a2, a2, -4
    bgtz  a2, 1b

2:  la    a0, __bssstart
    la    a1, __bssend
    sub   a2, a1, a0
    beqz  a2, 3f
4:  sw    x0, 0(a0)
    addi  a0, a0, 4
    addi  a2, a2, -4
    bnez  a2, 4b

3:  call  main
    j     .

    .text
    .p2align 2
    .globl arv_trap_entry
arv_trap_entry:
    addi  sp, sp, -64
    sw    x1,  4(sp)
    sw    x3, 12(sp)
    sw    x4, 16(sp)
    sw    x5, 20(sp)
    sw    x6, 24(sp)
    sw    x7, 28(sp)
    sw    x8, 32(sp)
    sw    x9, 36(sp)
    sw    x10, 40(sp)
    sw    x11, 44(sp)
    sw    x12, 48(sp)
    sw    x13, 52(sp)
    sw    x14, 56(sp)
    sw    x15, 60(sp)
    addi  t0, sp, 64
    sw    t0,  8(sp)                 /* x2: the sp at the trap              */
    sw    x0,  0(sp)
    mv    a0, sp
    csrr  a1, mcause
    csrr  a2, mepc
    csrr  a3, mtval
    call  arv_trap
    csrw  mepc, a0
    lw    x1,  4(sp)
    lw    x3, 12(sp)
    lw    x4, 16(sp)
    lw    x5, 20(sp)
    lw    x6, 24(sp)
    lw    x7, 28(sp)
    lw    x8, 32(sp)
    lw    x9, 36(sp)
    lw    x10, 40(sp)
    lw    x11, 44(sp)
    lw    x12, 48(sp)
    lw    x13, 52(sp)
    lw    x14, 56(sp)
    lw    x15, 60(sp)
    addi  sp, sp, 64
    mret
