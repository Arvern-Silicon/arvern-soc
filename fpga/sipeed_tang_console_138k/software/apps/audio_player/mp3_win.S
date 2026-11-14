//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      mp3_win.S  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// void mp3_win2(const int32_t *vl, const int32_t *vr, const int32_t *d, int16_t *pcm)
//
// The synthesis window of both channels of a time slot (mp3.c's synth for
// two channels, the same results): for j < 32, pcm[2j] (left) and
// pcm[2j + 1] (right) = clip16((sum + 64) >> 7) of the sum over t < 8 of
// mulh(v[128 t + j], d[64 t + j]) + mulh(v[128 t + 96 + j], d[64 t + 32 + j]).
// Each coefficient is loaded once for both channels; the three loads of a
// tap come before their products (no load-use stall). The bases sit in the
// middle of the taps, so every offset fits a load's 12 bits.
//----------------------------------------------------------------------------
    .section .text.mp3_win2, "ax"
    .globl  mp3_win2
    .type   mp3_win2, @function
    .p2align 2
mp3_win2:
    addi    sp, sp, -4
    sw      s0, 0(sp)
    addi    a0, a0, 1984            /* the bases in the middle of the taps: offsets within +-2047 */
    addi    a1, a1, 1984
    addi    a2, a2, 1024
    addi    a4, a2, 128             /* d end: 32 words */
1:
    li      a5, 0                   /* left sum  */
    li      t0, 0                   /* right sum */
    .irp    t, 0, 1, 2, 3, 4, 5, 6, 7       /* three loads, then their two products: no load-use stalls */
    lw      t1, (256 * \t - 1024)(a2)
    lw      t2, (512 * \t - 1984)(a0)
    lw      s0, (512 * \t - 1984)(a1)
    mulh    t2, t2, t1
    mulh    s0, s0, t1
    lw      t1, (256 * \t + 128 - 1024)(a2)
    add     a5, a5, t2
    lw      t2, (512 * \t + 384 - 1984)(a0)
    add     t0, t0, s0
    lw      s0, (512 * \t + 384 - 1984)(a1)
    mulh    t2, t2, t1
    mulh    s0, s0, t1
    add     a5, a5, t2
    add     t0, t0, s0
    .endr
    li      t1, 32767
    li      t2, -32768
    addi    a5, a5, 64
    srai    a5, a5, 7
    min     a5, a5, t1
    max     a5, a5, t2
    sh      a5, 0(a3)
    addi    t0, t0, 64
    srai    t0, t0, 7
    min     t0, t0, t1
    max     t0, t0, t2
    sh      t0, 2(a3)
    addi    a0, a0, 4
    addi    a1, a1, 4
    addi    a2, a2, 4
    addi    a3, a3, 4
    bne     a2, a4, 1b
    lw      s0, 0(sp)
    addi    sp, sp, 4
    ret
    .size   mp3_win2, . - mp3_win2
