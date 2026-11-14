//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      fx_fire_asm.S  (DE0-Nano-SoC Belenos effects demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// One line of the burning ground (fx_fire.c), four pixels a word, updated in
// place (RV32E, Zba/Zbb). For each pixel x: d[x] = tab[d[x] + 2 * (b[x-1] +
// b[x] + b[x+1])].
//
//   void fx_fire_line(uint32_t *d, const uint32_t *b, uint32_t *end,
//                     unsigned left, const uint8_t *tab);
//
// d .. end: the words to compute; b: the line under them (b[end] is read);
// left: the pixel left of the first word (0 at the line start). A word whose
// pixels and neighbours are all 0 stays 0 and is not written.
//
// a0 d   a1 b   a2 end   a3 left pixel   a4 tab   a5 b word
// t0 next b word   t1 d word (shifted down)   t2 result
// s1 sum of three   s0 table address   ra scratch
//----------------------------------------------------------------------------
        .section .text.fx_fire_line,"ax",@progbits
        .globl  fx_fire_line
        .type   fx_fire_line, @function
        .p2align 2
fx_fire_line:
        bgeu    a0, a2, 3f
        addi    sp, sp, -12
        sw      s0, 0(sp)
        sw      s1, 4(sp)
        sw      ra, 8(sp)
        lw      a5, 0(a1)
1:      lw      t0, 4(a1)
        lw      t1, 0(a0)
        or      s1, a3, a5
        or      s1, s1, t1
        zext.b  ra, t0
        or      s1, s1, ra
        beqz    s1, 2f
        // pixel 0: left, b0, b1
        zext.b  ra, a5
        add     s1, a3, ra
        srli    ra, a5, 8
        zext.b  ra, ra
        add     s1, s1, ra
        zext.b  s0, t1
        add     s0, s0, a4
        sh1add  s0, s1, s0
        lbu     t2, 0(s0)
        // pixel 1: b0, b1, b2
        sub     s1, s1, a3
        srli    ra, a5, 16
        zext.b  ra, ra
        add     s1, s1, ra
        srli    t1, t1, 8
        zext.b  s0, t1
        add     s0, s0, a4
        sh1add  s0, s1, s0
        lbu     s0, 0(s0)
        slli    s0, s0, 8
        or      t2, t2, s0
        // pixel 2: b1, b2, b3
        zext.b  ra, a5
        sub     s1, s1, ra
        srli    ra, a5, 24
        add     s1, s1, ra
        srli    t1, t1, 8
        zext.b  s0, t1
        add     s0, s0, a4
        sh1add  s0, s1, s0
        lbu     s0, 0(s0)
        slli    s0, s0, 16
        or      t2, t2, s0
        // pixel 3: b2, b3, next b0
        srli    ra, a5, 8
        zext.b  ra, ra
        sub     s1, s1, ra
        zext.b  ra, t0
        add     s1, s1, ra
        srli    t1, t1, 8
        add     s0, t1, a4
        sh1add  s0, s1, s0
        lbu     s0, 0(s0)
        slli    s0, s0, 24
        or      t2, t2, s0
        sw      t2, 0(a0)
2:      srli    a3, a5, 24
        mv      a5, t0
        addi    a0, a0, 4
        addi    a1, a1, 4
        bltu    a0, a2, 1b
        lw      s0, 0(sp)
        lw      s1, 4(sp)
        lw      ra, 8(sp)
        addi    sp, sp, 12
3:      ret
        .size   fx_fire_line, .-fx_fire_line
