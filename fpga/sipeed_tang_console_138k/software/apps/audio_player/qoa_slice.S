//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      qoa_slice.S  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// void qoa_slice20(int32_t lms[8], int16_t *s)
// void qoa_slice20_mono(int32_t lms[8], int16_t *s)
//
// One whole QOA slice of one channel, in place, in the player's ring: s
// points at the channel's half of 20 stereo frames (s[0], s[2], .. s[38]),
// which hold the dequantized residuals and get the samples; the mono form
// writes each sample to both halves. lms is the channel's history h[0..3]
// (oldest first) and weights w[0..3].
//
// Written for RV32E (also RV32I): the whole state in the 12 registers a
// leaf can use, so the loop never touches the stack but for its end:
//   h0..h3  a2 a3 a4 a5     the history, renamed by each step (no moves
//                           but the new sample's)
//   w0..w3  t0 t1 t2 s0     the weights
//   P       a1              the slice, 4 frames a loop
//   R B C   s1 a0 ra        residual then step, prediction then sample,
//                           scratch
// A step: the prediction (4 multiplies), the sample clamped to 16 bits (a
// shift pair finds an overflow; the clamp itself is out of line), then
// each weight moves by the step d = residual >> 4 with its history's sign:
// w -= s; w += s ^ d (s = h >> 31: 0 or -1), which needs no second scratch.
//----------------------------------------------------------------------------

    .section .text.qoa_slice20, "ax"

#define P   a1
#define W0  t0
#define W1  t1
#define W2  t2
#define W3  s0
#define R   s1
#define B   a0
#define C   ra

.macro WEIGHT w, h
    srai  C, \h, 31
    sub   \w, \w, C
    xor   C, C, R
    add   \w, \w, C
.endm

.macro STEP h0, h1, h2, h3, off, mono
    lh    R, \off(P)
    mul   B, W0, \h0
    mul   C, W1, \h1
    add   B, B, C
    mul   C, W2, \h2
    add   B, B, C
    mul   C, W3, \h3
    add   B, B, C
    srai  B, B, 13
    add   B, B, R
    slli  C, B, 16
    srai  C, C, 16
    bne   C, B, .Lsat\@
.Lsat_done\@:
    sh    B, \off(P)
    .if \mono
    sh    B, \off+2(P)
    .endif
    srai  R, R, 4
    WEIGHT W0, \h0
    WEIGHT W1, \h1
    WEIGHT W2, \h2
    WEIGHT W3, \h3
    mv    \h0, B
    .subsection 1                       // the clamp, out of the loop
.Lsat\@:
    srai  B, B, 31                      // 0 or -1
    lui   C, 8
    addi  C, C, -1                      // 0x7FFF
    xor   B, B, C                       // 32767 or -32768
    j     .Lsat_done\@
    .subsection 0
.endm

.macro SLICE name, mono
    .globl   \name
    .type    \name, @function
    .p2align 2
\name:
    addi  sp, sp, -20
    sw    ra, 16(sp)
    sw    s0, 12(sp)
    sw    s1, 8(sp)
    sw    a0, 4(sp)
    addi  C, P, 80
    sw    C, 0(sp)                      // the slice's end
    lw    a2, 0(a0)
    lw    a3, 4(a0)
    lw    a4, 8(a0)
    lw    a5, 12(a0)
    lw    W0, 16(a0)
    lw    W1, 20(a0)
    lw    W2, 24(a0)
    lw    W3, 28(a0)
1:
    STEP  a2, a3, a4, a5, 0, \mono
    STEP  a3, a4, a5, a2, 4, \mono
    STEP  a4, a5, a2, a3, 8, \mono
    STEP  a5, a2, a3, a4, 12, \mono
    addi  P, P, 16
    lw    C, 0(sp)
    bne   P, C, 1b
    lw    a0, 4(sp)                     // 20 steps: the names back in order
    sw    a2, 0(a0)
    sw    a3, 4(a0)
    sw    a4, 8(a0)
    sw    a5, 12(a0)
    sw    W0, 16(a0)
    sw    W1, 20(a0)
    sw    W2, 24(a0)
    sw    W3, 28(a0)
    lw    s1, 8(sp)
    lw    s0, 12(sp)
    lw    ra, 16(sp)
    addi  sp, sp, 20
    ret
    .size    \name, . - \name
.endm

    SLICE qoa_slice20, 0
    SLICE qoa_slice20_mono, 1
