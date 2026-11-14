//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      spectrum_fft.S  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// void fft256(int32_t z[512], const int16_t tw[512])
//
// The butterflies of spectrum.c's 256-point FFT (the same results): z holds
// 256 complex samples (real, imaginary) in bit-reversed order, tw the
// twiddles W_512^k (cos, -sin) in Q15. Eight stages; in a stage of
// butterflies 2 x half wide, sample u of each group meets sample u + half
// with twiddle k x 256 / half: t = v x w rounded to Q15, then
// v = (u - t) / 2 and u = (u + t) / 2, rounded. A rounded shift by 15 is
// ((x >> 14) + 1) >> 1, which is (x + 2^14) >> 15 exactly.
//
// Each stage is its own loop (the STAGE macro), so that the distance to
// sample u + half and the twiddle step are offsets in the instructions: the
// state fits the 12 registers an RV32E leaf can use.
//   Z TW U GE TP END   a0 a1 a2 a3 a4 a5   the samples, the twiddles, sample
//                                          u, its group's end, the twiddle,
//                                          the samples' end
//   WR WI BR BI P1 P2  t0 t1 t2 s0 s1 ra   twiddle, sample v, products
//----------------------------------------------------------------------------

    .section .text.fft256, "ax"
    .globl   fft256
    .type    fft256, @function
    .p2align 2

#define Z   a0
#define TW  a1
#define U   a2
#define GE  a3
#define TP  a4
#define END a5
#define WR  t0
#define WI  t1
#define BR  t2
#define BI  s0
#define P1  s1
#define P2  ra

// x rounded from Q30 to Q15, in place
.macro ROUND15 r
    srai  \r, \r, 14
    addi  \r, \r, 1
    srai  \r, \r, 1
.endm

// (a op b + 1) >> 1 into d, stored at off(U)
.macro HALVE d, a, op, b, off
    \op   \d, \a, \b
    addi  \d, \d, 1
    srai  \d, \d, 1
    sw    \d, \off(U)
.endm

.macro STAGE half, twstep
    mv    U, Z
1:  addi  GE, U, \half * 8
    mv    TP, TW
2:  lh    WR, 0(TP)
    lh    WI, 2(TP)
    lw    BR, \half * 8(U)
    lw    BI, \half * 8 + 4(U)
    mul   P1, BR, WR
    mul   P2, BI, WI
    sub   P1, P1, P2
    ROUND15 P1                          // t real
    mul   P2, BR, WI
    mul   BR, BI, WR
    add   P2, P2, BR
    ROUND15 P2                          // t imaginary
    lw    BR, 0(U)
    lw    BI, 4(U)
    HALVE WR, BR, sub, P1, \half * 8
    HALVE WI, BI, sub, P2, \half * 8 + 4
    HALVE BR, BR, add, P1, 0
    HALVE BI, BI, add, P2, 4
    addi  U, U, 8
    addi  TP, TP, \twstep
    bltu  U, GE, 2b
    addi  U, U, \half * 8               // past the group's v half
    bltu  U, END, 1b
.endm

fft256:
    addi  sp, sp, -12
    sw    ra, 8(sp)
    sw    s0, 4(sp)
    sw    s1, 0(sp)
    li    END, 2048
    add   END, END, Z
    STAGE 1,   1024
    STAGE 2,   512
    STAGE 4,   256
    STAGE 8,   128
    STAGE 16,  64
    STAGE 32,  32
    STAGE 64,  16
    STAGE 128, 8
    lw    s1, 0(sp)
    lw    s0, 4(sp)
    lw    ra, 8(sp)
    addi  sp, sp, 12
    ret

    .size    fft256, . - fft256
