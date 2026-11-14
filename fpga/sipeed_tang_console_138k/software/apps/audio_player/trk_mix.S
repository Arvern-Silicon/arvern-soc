//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      trk_mix.S  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// int trk_mix(trk_mix_t *v, int32_t *acc, int32_t *end)
//
// One voice into a group's sums (trk.c's mix_voice for a voice at a group's
// place, the same results): for each frame, the sample at the position,
// linearly interpolated with the next, times the voice's volume, added to
// acc[]; then the position steps on. At the sample's end a looped voice
// goes back by its loop length, another stops: returns 0 then, else 1.
//
// trk_mix_t: the next sample, the end, the loop length (0: none), the
// position's fraction and the step's fraction (each in the upper 16 bits,
// so that a carry out of their sum steps the sample on), the step's whole
// samples, the volume. The interpolation is ((s1 - s0) << 8) times the
// fraction, high word (mulhsu): (s1 - s0) x frac >> 8, as in C.
//
// Written for RV32E (also RV32I), the state in the 12 registers a leaf can
// use:
//   P PE FR SF SI G    a3 a4 a5 t0 t1 t2    sample, end, fractions, step, volume
//   M ME               a1 a2                the sums, their end
//   A B T U            s0 s1 a0 ra          scratch
//----------------------------------------------------------------------------

    .section .text.trk_mix, "ax"
    .globl   trk_mix
    .type    trk_mix, @function
    .p2align 2

#define P   a3
#define PE  a4
#define FR  a5
#define SF  t0
#define SI  t1
#define G   t2
#define M   a1
#define ME  a2
#define A   s0
#define B   s1
#define T   a0
#define U   ra

trk_mix:
    addi  sp, sp, -16
    sw    ra, 12(sp)
    sw    s0, 8(sp)
    sw    s1, 4(sp)
    sw    a0, 0(sp)
    lw    P, 0(a0)
    lw    PE, 4(a0)
    lw    FR, 12(a0)
    lw    SF, 16(a0)
    lw    SI, 20(a0)
    lw    G, 24(a0)
    bgeu  M, ME, .Lsave

.Lframe:
    lb    A, 0(P)
    lb    B, 1(P)
    sub   B, B, A
    slli  B, B, 8
    mulhsu B, B, FR                     // (s1 - s0) x frac >> 8
    slli  A, A, 8
    add   A, A, B
    mul   A, A, G
    lw    U, 0(M)
    add   U, U, A
    sw    U, 0(M)
    add   FR, FR, SF
    sltu  T, FR, SF                     // the fraction's carry
    add   P, P, T
    add   P, P, SI
    addi  M, M, 4
    bgeu  P, PE, .Lend
    bltu  M, ME, .Lframe
    j     .Lsave

.Lend:                                  // past the end: the loop, or the voice stops
    lw    T, 0(sp)
    lw    T, 8(T)
    beqz  T, .Lstop
1:  sub   P, P, T
    bgeu  P, PE, 1b
    bltu  M, ME, .Lframe

.Lsave:
    lw    T, 0(sp)
    sw    P, 0(T)
    sw    FR, 12(T)
    li    a0, 1
    j     .Lret
.Lstop:
    lw    T, 0(sp)
    sw    P, 0(T)
    sw    FR, 12(T)
    li    a0, 0
.Lret:
    lw    s1, 4(sp)
    lw    s0, 8(sp)
    lw    ra, 12(sp)
    addi  sp, sp, 16
    ret

    .size    trk_mix, . - trk_mix
