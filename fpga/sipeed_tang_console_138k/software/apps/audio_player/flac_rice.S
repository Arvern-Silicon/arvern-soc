//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      flac_rice.S  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// int32_t *flac_rice(flac_bits_t *b, int32_t *x, int32_t *end, unsigned k)
//
// Rice-coded residuals with parameter k (0 to 24) into x[], up to end (the
// fast path of flac.c's residual): each a unary quotient (zeros before a 1),
// then k low bits, the result zigzag coded (even = positive). Stops early,
// before a sample, when the bytes left are fewer than 8 (two refills take up
// to 7) or when the cache is all zeros (a quotient longer than the cache);
// returns where it stopped. b is the bit reader: the cache (bits most
// significant first), the free bits (24 - valid bits: the cache is refilled
// a byte at a time while it is not negative), the next byte, and the last
// byte the routine may start a sample at.
//
// Written for RV32E (also RV32I), its whole state in the 12 registers a
// leaf can use:
//   C F P L    a2 a3 a4 a5    cache, free bits, next byte, limit
//   X E        a1 t0          the residuals, their end
//   K KS KL    t1 t2 s0       k, 31 - k, 24 - k
//   Z V T      s1 a0 ra       quotient, value, scratch
// The k low bits are (C >> 1) >> (31 - k), so k = 0 needs no shift by 32.
//----------------------------------------------------------------------------

    .section .text.flac_rice, "ax"
    .globl   flac_rice
    .type    flac_rice, @function
    .p2align 2

#define C   a2
#define F   a3
#define P   a4
#define L   a5
#define X   a1
#define E   t0
#define K   t1
#define KS  t2
#define KL  s0
#define Z   s1
#define V   a0
#define T   ra

flac_rice:
    addi  sp, sp, -16
    sw    ra, 12(sp)
    sw    s0, 8(sp)
    sw    s1, 4(sp)
    sw    a0, 0(sp)
    mv    E, a2
    mv    K, a3
    lw    C, 0(a0)
    lw    F, 4(a0)
    lw    P, 8(a0)
    lw    L, 12(a0)
    li    KS, 31
    sub   KS, KS, K
    li    KL, 24
    sub   KL, KL, K

.Lsample:
    bgeu  X, E, .Ldone
    bgtu  P, L, .Ldone
.Lfill:                                 // to 25 or more valid bits
    bltz  F, .Lfilled
    lbu   T, 0(P)
    sll   T, T, F
    or    C, C, T
    addi  P, P, 1
    addi  F, F, -8
    j     .Lfill
.Lfilled:
    beqz  C, .Ldone                     // a quotient longer than the cache
    clz   Z, C
    sll   C, C, Z
    slli  C, C, 1                       // the quotient and its 1
    add   F, F, Z
    addi  F, F, 1
    blt   KL, F, .Lmore                 // fewer than k bits left (rare)
.Lbits:
    srli  V, C, 1
    srl   V, V, KS                      // the k low bits
    sll   Z, Z, K
    or    V, V, Z
    sll   C, C, K
    add   F, F, K
    andi  T, V, 1                       // zigzag: v >> 1, negated when odd
    srli  V, V, 1
    neg   T, T
    xor   V, V, T
    sw    V, 0(X)
    addi  X, X, 4
    j     .Lsample

.Lmore:
    bltz  F, .Lbits
    lbu   T, 0(P)
    sll   T, T, F
    or    C, C, T
    addi  P, P, 1
    addi  F, F, -8
    j     .Lmore

.Ldone:
    lw    T, 0(sp)
    sw    C, 0(T)
    sw    F, 4(T)
    sw    P, 8(T)
    mv    a0, X
    lw    s1, 4(sp)
    lw    s0, 8(sp)
    lw    ra, 12(sp)
    addi  sp, sp, 16
    ret

    .size    flac_rice, . - flac_rice
