//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K Belenos smoke test)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// tst_blns -- Belenos graphic controller smoke test for the Tang Console 138K.
//
//   1. reads BLNS_ID / BLNS_CFG through the register port (0x10050000),
//   2. writes and reads back VRAM through the window (0x82000000), word and
//      byte accesses,
//   3. sets up the built-in DPI driver for the 480 x 272 panel (the frame
//      shown 1:1, centred) and starts it,
//      then in the HDMI build reads the monitor's EDID over the DDC and turns
//      on HDMI signalling with an AVI InfoFrame (in the RGB panel build,
//      checks that there is neither),
//   4. draws eight colour bars at 16 bpp and sends one manual refresh,
//      checking the REFR_DONE interrupt on the core's platform IRQ 0 (MIP[16]),
//   5. on the board, then keeps the period timer running (the DPI driver
//      takes a frame every panel frame anyway).
//   9. after 4 (LED code 9), checks the 1.0 hardware: FRAME_NUM moved over
//      the refresh, a GPU fill reaches its fence tag, blns_lut_present flips
//      LUT_CFG.REFR_BANK.
//  10-15. checks the 1.0 GPU features, each drawn after the frame shown and
//      read back from the VRAM or the registers: 10 lines (solid, the end
//      pixel left out, dashed with gaps skipped or drawn), 11 the hardware
//      cursor (shape block by rows and columns, colours, hotspot, position,
//      show; shown over the frame for the stimulus, LED 0x0C, then hidden),
//      12 the mask plane and the palette remap (8 bpp, then 16 bpp again),
//      13 the immediate source (copy, expansion) and blns_gpu_discard (a cut
//      command dropped), 14 a command list called twice, 15 FIFO room
//      (blns_gpu_wait_space, the FIFO_LOW threshold and its event); then the
//      frame shown is checked again.
//
// Result on the LEDs: 0xA5 = pass, 0xE0 | step = failure at that step (the
// failed sub-check in blns_check, for a debugger).
// Built with -DSIMULATION the frame is 8 x 2 pixels and the delays are short,
// so the board-level testbench can check every pixel the panel model receives.
// Uses the Belenos driver library (arvern-belenos/sw).
//----------------------------------------------------------------------------

#include <stdint.h>
#include "belenos.h"
#include "ahb_aclint.h"
#include "tang_board.h"

//----------------------------------------------------------
// Board
//----------------------------------------------------------
#define LED_CTRL      (*(volatile uint32_t *) 0x10040000u)

#define BLNS_REG_BASE  0x10050000u
#define BLNS_VRAM_BASE 0x82000000u
#define HCLK_HZ        BOARD_HCLK_HZ

#define MIP_BLNS       (1u << BOARD_IRQ_BELENOS)

#ifdef SIMULATION
#define DISP_W        8u
#define DISP_H        2u
#else
#define DISP_W        320u                     // shown doubled on the 800 x 480 panel
#define DISP_H        240u
#define REFR_FPS      25u
#endif

#define read_csr(reg)       ({ uint32_t __v; __asm__ volatile ("csrr %0, " #reg : "=r"(__v)); __v; })
#define clear_csr(reg, val) ({ __asm__ volatile ("csrc " #reg ", %0" :: "r"(val) : "memory"); })

// RGB565 colour bars: white, yellow, cyan, green, magenta, red, blue, black
static const uint16_t bars[8] = {
    0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000
};

// Pixel colour: the bar of column x; the band of row y flips the three blue
// LSBs so that every row is distinct (invisible on the panel).
static uint16_t pixel(uint32_t x, uint32_t y)
{
    return bars[(x * 8u) / DISP_W] ^ (uint16_t)((y * 8u) / DISP_H);
}

static void fail(uint32_t step)
{
    LED_CTRL = 0xE0u | step;
    for (;;) ;
}

static aclint_t aclint;

// A quarter of a DDC bit (100 kHz on the board; as fast as the bus allows
// in the simulation build)
static void ddc_quarter(void)
{
#if (BOARD_TIME_HZ / 400000u) > 0u
    const uint32_t t0 = aclint_mtime_lo(&aclint);
    while (aclint_mtime_lo(&aclint) - t0 < BOARD_TIME_HZ / 400000u)
        ;
#endif
}

//----------------------------------------------------------
// 10-15) the 1.0 classic GPU features and the cursor, each drawn into the
// VRAM after the frame shown and read back (address, then x/y mode at
// 16 bpp; the remap at 8 bpp). Scratch layout from SPARE (bytes):
//   +0x000 cursor shape block (128 words)   +0x200 remap table (256 bytes)
//   +0x300 mask plane bits                  +0x400 command list (128 words)
//   +0x600 lines, 32 x 16 pixels            +0xA00 the other checks
//   +0x1000 a large fill that keeps the GPU busy (BUSY_PX pixels)
//----------------------------------------------------------
#define SPARE         (((DISP_W * DISP_H * 2u) + 511u) & ~511u)
#define S_CUR         (SPARE + 0x000u)
#define S_TBL         (SPARE + 0x200u)
#define S_MSK         (SPARE + 0x300u)
#define S_LST         (SPARE + 0x400u)
#define S_LIN         (SPARE + 0x600u)
#define S_MSC         (SPARE + 0xA00u)
#define S_BUSY        (SPARE + 0x1000u)
#define BUSY_ROOM     ((BOARD_VRAM_WORDS * 4u - S_BUSY) / 2u / 64u * 64u)
#define BUSY_PX       (BUSY_ROOM < 16384u ? BUSY_ROOM : 16384u)   // DE0 board: 3072
#define FILL          0x0F0Fu                  // what was there before a check draws
_Static_assert(S_BUSY + 2u * BUSY_PX <= BOARD_VRAM_WORDS * 4u, "the checks' scratch is past the VRAM");

volatile uint32_t blns_check;                  // the sub-check that failed (a debugger reads it)

static void check(bool ok, uint32_t step, uint32_t sub)
{
    if (!ok) {
        blns_check = sub;
        fail(step);
    }
}

// The cursor shown on the panel (board's), then held while the stimulus
// checks the panel
static void cursor_present(blns_t *b);
static void cursor_hold(blns_t *b);

static volatile uint16_t *v16(blns_t *b, uint32_t off) { return (volatile uint16_t *) (blns_vram8(b) + off); }
static volatile uint8_t  *v8(blns_t *b, uint32_t off)  { return blns_vram8(b) + off; }

static blns_surf_t surf16(blns_t *b, uint32_t off, uint16_t pitch)
{
    const blns_surf_t s = { blns_vram_pa(b, v16(b, off)), pitch, BLNS_SWAP_NONE };
    return s;
}

// 10) lines in x/y mode: solid (horizontal, vertical, diagonal, end pixel
//     left out), dashed (gaps skipped, then drawn in the gap colour)
static void check_lines(blns_t *b)
{
    volatile uint16_t *p = v16(b, S_LIN);
    const blns_line_t nolast = { .no_last = true };
    const blns_line_t dash   = { .phase_reset = true };
    const blns_line_t dash_o = { .phase_reset = true, .opaque = true, .bg = 0x2222u };
    for (uint32_t i = 0; i < 32u * 16u; i++)
        p[i] = FILL;
    check(b->caps.gpu_line, 10, 1);
    check(blns_gpu_xy_begin(b, blns_vram_pa(b, p), 32u, 0x1111u) == BLNS_OK, 10, 2);
    check(blns_gpu_line_px(b, 0, 0, 9, 0, NULL) == BLNS_OK, 10, 3);
    check(blns_gpu_line_px(b, 0, 2, 0, 5, NULL) == BLNS_OK, 10, 3);
    check(blns_gpu_line_px(b, 2, 8, 6, 12, NULL) == BLNS_OK, 10, 3);
    check(blns_gpu_line_px(b, 20, 0, 25, 0, &nolast) == BLNS_OK, 10, 3);
    check(blns_gpu_line_pattern(b, 0x3u, 4u) == BLNS_OK, 10, 4);    // 2 on, 2 off
    check(blns_gpu_line_px(b, 0, 14, 15, 14, &dash) == BLNS_OK, 10, 4);
    check(blns_gpu_line_px(b, 0, 15, 15, 15, &dash_o) == BLNS_OK, 10, 4);
    check(blns_gpu_line_solid(b) == BLNS_OK, 10, 4);
    check(blns_gpu_wait(b) == BLNS_OK, 10, 5);
    for (unsigned y = 0; y < 16u; y++)
        for (unsigned x = 0; x < 32u; x++) {
            uint16_t e = FILL;
            if ((y == 0u && (x <= 9u || (x >= 20u && x <= 24u))) || (x == 0u && y >= 2u && y <= 5u) ||
                (y >= 8u && y <= 12u && x == y - 6u))
                e = 0x1111u;
            if ((y == 14u || y == 15u) && x <= 15u)
                e = ((x & 3u) < 2u) ? 0x1111u : (y == 15u) ? 0x2222u : FILL;
            check(p[y * 32u + x] == e, 10, 6);
        }
}

// 11) the cursor: the shape block (by rows and by columns), the colours,
//     the hotspot, the position and show read back; then shown over the
//     frame (cursor_present: the panel model checks it while LED = 0x0C)
//     and hidden again. Shape pixels (0..5, 0..1) land on frame pixels
//     (2..7, 0..1): codes 1 2 3 0 1 2 / 3 0 0 3 2 1
static void check_cursor(blns_t *b)
{
    static const uint8_t code[2][6] = { { 1, 2, 3, 0, 1, 2 }, { 3, 0, 0, 3, 2, 1 } };
    static uint32_t rows[64];
    volatile uint32_t *w = (volatile uint32_t *) v8(b, S_CUR);
    const blns_wa_t blk = S_CUR / 4u;
    check(b->caps.cursor, 11, 1);
    for (unsigned j = 0; j < 2u; j++)
        for (unsigned i = 0; i < 6u; i++)
            rows[2u * j] |= (uint32_t) code[j][i] << (2u * i);
    check(blns_cursor_shape(b, blk, rows) == BLNS_OK, 11, 2);
    for (unsigned k = 0; k < 64u; k++) {
        check(w[k] == rows[k], 11, 3);                                  // by rows
        const unsigned i = k / 2u;                                      // by columns
        const uint32_t c = (k & 1u) || i >= 6u ? 0u : (uint32_t) code[0][i] | ((uint32_t) code[1][i] << 2);
        check(w[64u + k] == c, 11, 3);
    }
    check(b->regs->CURSOR_ADDR == blk, 11, 4);
    check(blns_cursor_colours(b, 0xF80000u, 0x00FC00u, 0x0000F8u) == BLNS_OK, 11, 5);
    check(b->regs->CURSOR_COL[0] == 0xF80000u && b->regs->CURSOR_COL[1] == 0x00FC00u &&
          b->regs->CURSOR_COL[2] == 0x0000F8u, 11, 5);
    check(blns_cursor_hotspot(b, 31u, 1u) == BLNS_OK, 11, 6);
    check((b->regs->CURSOR_CTRL & (BLNS_CURSOR_CTRL_HOT_X_Msk | BLNS_CURSOR_CTRL_HOT_Y_Msk)) ==
          ((31u << BLNS_CURSOR_CTRL_HOT_X_Pos) | (1u << BLNS_CURSOR_CTRL_HOT_Y_Pos)), 11, 6);
    check(blns_cursor_hotspot(b, 32u, 0u) == BLNS_ERR_PARAM, 11, 6);
    check(blns_cursor_hotspot(b, 0u, 0u) == BLNS_OK, 11, 6);
    check(blns_cursor_move(b, -5, -7) == BLNS_OK && b->regs->CURSOR_POS == 0xFFF9FFFBu, 11, 7);
    check(blns_cursor_move(b, 2, 0) == BLNS_OK && b->regs->CURSOR_POS == 2u, 11, 7);
    check(!(b->regs->CURSOR_CTRL & BLNS_CURSOR_CTRL_EN_Msk), 11, 8);
    check(blns_cursor_show(b, true) == BLNS_OK && (b->regs->CURSOR_CTRL & BLNS_CURSOR_CTRL_EN_Msk), 11, 8);
    cursor_present(b);
    LED_CTRL = 0x0C;                                                    // the cursor on the panel
    cursor_hold(b);
    check(blns_cursor_show(b, false) == BLNS_OK && !(b->regs->CURSOR_CTRL & BLNS_CURSOR_CTRL_EN_Msk), 11, 9);
    cursor_present(b);
    LED_CTRL = 0x0D;
}

// 12) mask plane (16 bpp: the source where the mask bit is 1, or 0 when
//     inverted), palette remap (8 bpp: a copy through the table, the table
//     applied to a rectangle of the destination)
static void check_remap_mask(blns_t *b)
{
    volatile uint16_t *s  = v16(b, S_MSC);
    volatile uint16_t *d  = v16(b, S_MSC + 0x40u);
    volatile uint16_t *di = v16(b, S_MSC + 0x80u);
    volatile uint8_t  *m  = v8(b, S_MSK);
    const uint16_t     mask = 0xC3A5u;
    const blns_blit_t  mp = { .rop = BLNS_ROP_S, .mplane = true };
    check(b->caps.gpu_mplane && b->caps.gpu_remap, 12, 1);
    m[0] = (uint8_t) mask;
    m[1] = (uint8_t) (mask >> 8);
    for (unsigned i = 0; i < 17u; i++) {
        s[i] = (uint16_t) (0x0100u + i);
        d[i] = di[i] = 0xEEEEu;
    }
    blns_surf_t ss = surf16(b, S_MSC, 16u), sd = surf16(b, S_MSC + 0x40u, 16u), sdi = surf16(b, S_MSC + 0x80u, 16u);
    check(blns_gpu_mask_plane(b, blns_vram_ba(b, m), false) == BLNS_OK, 12, 2);
    check(blns_gpu_blit(b, &ss, &sd, 16u, 1u, &mp) == BLNS_OK, 12, 2);
    check(blns_gpu_mask_plane(b, blns_vram_ba(b, m), true) == BLNS_OK, 12, 2);
    check(blns_gpu_blit(b, &ss, &sdi, 16u, 1u, &mp) == BLNS_OK, 12, 2);
    check(blns_gpu_wait(b) == BLNS_OK, 12, 2);
    for (unsigned i = 0; i < 16u; i++) {
        const bool on = (mask >> i) & 1u;
        check(d[i] == (on ? s[i] : 0xEEEEu) && di[i] == (on ? 0xEEEEu : s[i]), 12, 3);
    }
    check(d[16] == 0xEEEEu && di[16] == 0xEEEEu, 12, 3);

    // the remap at 8 bpp (no effect at 16 bpp); the frame shown is 16 bpp
    // again before anything else (its pixels checked)
    volatile uint8_t *t = v8(b, S_TBL), *src = v8(b, S_MSC + 0x100u), *d1 = v8(b, S_MSC + 0x120u),
                     *d2 = v8(b, S_MSC + 0x140u);
    const blns_blit_t rs = { .rop = BLNS_ROP_S, .remap = BLNS_REMAP_SRC };
    for (unsigned i = 0; i < 256u; i++)
        t[i] = (uint8_t) (i * 7u + 3u);
    for (unsigned i = 0; i < 17u; i++) {
        src[i] = (uint8_t) (i * 13u + 1u);
        d1[i]  = 0xEEu;
        d2[i]  = (i < 16u) ? (uint8_t) (i * 5u) : 0x77u;
    }
    blns_set_mode(b, BLNS_BPP_8);
    const blns_surf_t s8 = { blns_vram_pa(b, src), 16u, BLNS_SWAP_NONE };
    const blns_surf_t d81 = { blns_vram_pa(b, d1), 16u, BLNS_SWAP_NONE };
    const blns_surf_t d82 = { blns_vram_pa(b, d2), 16u, BLNS_SWAP_NONE };
    check(blns_gpu_remap_table(b, S_TBL / 4u) == BLNS_OK, 12, 4);
    check(blns_gpu_blit(b, &s8, &d81, 16u, 1u, &rs) == BLNS_OK, 12, 4);
    check(blns_gpu_remap_rect(b, &d82, 16u, 1u) == BLNS_OK, 12, 4);
    check(blns_gpu_wait(b) == BLNS_OK, 12, 4);
    blns_set_mode(b, BLNS_BPP_16);
    for (unsigned i = 0; i < 16u; i++)
        check(d1[i] == t[src[i]] && d2[i] == t[(i * 5u) & 0xFFu], 12, 5);
    check(d1[16] == 0xEEu && d2[16] == 0x77u, 12, 5);
    check(blns_gpu_remap_rect(b, &d82, 16u, 1u) == BLNS_ERR_PARAM, 12, 6);   // refused at 16 bpp
}

// 13) immediate source (a 16 bpp copy and an opaque 1 bpp expansion pushed
//     behind their command) and discard (a cut command dropped, the work
//     before it run, the stream in step after it)
static void check_imm_discard(blns_t *b)
{
    volatile uint16_t *p = v16(b, S_MSC + 0x200u);                    // 8 x 4, pitch 8
    volatile uint16_t *q = v16(b, S_MSC + 0x280u);                    // 8 x 2, pitch 8
    uint32_t px[3], bits = 0x3CA5u, w[7];
    const blns_blit_t  plain = { .rop = BLNS_ROP_S };
    const blns_expand_t e = { .width = 8u, .height = 2u, .src_pitch = 0u, .depth = BLNS_EXP_1BPP,
                              .opaque = true, .mask = false, .swap = BLNS_SWAP_NONE, .rop = BLNS_ROP_S };
    check(b->caps.gpu_imm, 13, 1);
    for (unsigned i = 0; i < 32u; i++)
        p[i] = FILL;
    for (unsigned i = 0; i < 16u; i++)
        q[i] = FILL;
    for (unsigned k = 0; k < 3u; k++)
        px[k] = (0x1000u + 2u * k * 0x111u) | ((0x1000u + (2u * k + 1u) * 0x111u) << 16);
    blns_surf_t d = surf16(b, S_MSC + 0x200u, 8u);
    check(blns_gpu_blit_imm(b, &d, 3u, 2u, &plain, px) == BLNS_OK, 13, 2);
    const blns_pa_t de = blns_vram_pa(b, p + 16);
    check(blns_gpu_expand_begin(b, &e, de, 8u, 0x1357u, 0x2468u) == BLNS_OK, 13, 3);
    check(blns_gpu_expand_imm(b, de, &bits) == BLNS_OK, 13, 3);
    check(blns_gpu_wait(b) == BLNS_OK, 13, 3);
    for (unsigned y = 0; y < 4u; y++)
        for (unsigned x = 0; x < 8u; x++) {
            const uint16_t v = p[y * 8u + x];
            if (y < 2u)
                check(v == ((x < 3u) ? (uint16_t) (0x1000u + (y * 3u + x) * 0x111u) : FILL), 13, 4);
            else
                check(v == (((bits >> ((y - 2u) * 8u + x)) & 1u) ? 0x1357u : 0x2468u), 13, 4);
        }

    // a fill whose last command (SET_FILL) has no operand: discarded; the
    // fill before it runs (q[0..3]), and the fills after it (q[8..10],
    // q[12..15]) draw as pushed (without the discard, the cut SET_FILL would
    // take the next fill's first word, its SET_SIZE, as its colour: that
    // fill would then draw 4 pixels, not 3)
    w[0] = BLNS_GPU_OP_SET_DST | 8u;
    w[1] = blns_vram_pa(b, q);
    w[2] = BLNS_GPU_OP_SET_SIZE | 4u | (1u << 14);
    w[3] = BLNS_GPU_OP_SET_FILL;
    w[4] = 0xAAAAu;
    w[5] = BLNS_GPU_OP_EXEC_FILL;
    w[6] = BLNS_GPU_OP_SET_FILL;                                        // its operand never comes
    check(blns_gpu_push(b, w, 7u) == BLNS_OK, 13, 5);
    check(blns_gpu_discard(b) == BLNS_OK, 13, 5);
    blns_surf_t f = surf16(b, S_MSC + 0x280u + 16u, 8u);
    uint32_t tag;
    check(blns_gpu_fill(b, &f, 3u, 1u, 0xBBBBu, BLNS_ROP_S) == BLNS_OK, 13, 6);
    check(blns_gpu_discard(b) == BLNS_OK, 13, 6);                     // nothing cut: no effect
    f = surf16(b, S_MSC + 0x280u + 24u, 8u);
    check(blns_gpu_fill(b, &f, 4u, 1u, 0xCCCCu, BLNS_ROP_S) == BLNS_OK, 13, 6);
    check(blns_gpu_fence(b, false, &tag) == BLNS_OK && blns_gpu_wait_fence(b, tag) == BLNS_OK, 13, 7);
    check(blns_gpu_wait(b) == BLNS_OK && !(b->regs->GPU.STAT & BLNS_GPU_STAT_ERR_Msk), 13, 7);
    for (unsigned i = 0; i < 16u; i++)
        check(q[i] == ((i < 4u) ? 0xAAAAu : (i < 8u || i == 11u) ? FILL : (i < 11u) ? 0xBBBBu : 0xCCCCu), 13, 8);
}

// 14) a command list (a fill and a copy) recorded, called, then called
//     again over pixels the CPU reset: the same drawing each time, the
//     list's fence reached
static void check_list(blns_t *b)
{
    volatile uint16_t *p = v16(b, S_MSC + 0x300u);                    // 8 x 2, pitch 8
    volatile uint16_t *src = v16(b, S_MSC + 0x200u);                  // the immediate copy above
    blns_gpu_list_t l = { 0 };
    uint32_t tag;
    l.addr     = S_LST / 4u;
    l.capacity = 128u;
    const blns_surf_t f = surf16(b, S_MSC + 0x300u, 8u);
    const blns_surf_t s = surf16(b, S_MSC + 0x200u, 8u);
    const blns_surf_t d = surf16(b, S_MSC + 0x300u + 16u + 4u, 8u);  // row 1, from x = 2
    check(blns_gpu_list_begin(b, &l, 0u) == BLNS_OK, 14, 1);
    check(blns_gpu_fill(b, &f, 4u, 1u, 0x5A5Au, BLNS_ROP_S) == BLNS_OK, 14, 1);
    check(blns_gpu_copy(b, &s, &d, 3u, 1u, BLNS_ROP_S) == BLNS_OK, 14, 1);
    check(blns_gpu_list_end(b) == BLNS_OK && l.count > 0u && l.count <= l.capacity, 14, 1);
    for (unsigned n = 0; n < 2u; n++) {
        for (unsigned i = 0; i < 16u; i++)
            p[i] = FILL;
        check(blns_gpu_list_call(b, &l, false) == BLNS_OK, 14, 2);
        check(blns_gpu_list_wait(b, &l) == BLNS_OK && !blns_gpu_list_busy(b, &l), 14, 3);
        for (unsigned i = 0; i < 16u; i++)
            check(p[i] == ((i < 4u) ? 0x5A5Au : (i >= 10u && i < 13u) ? src[i - 10u] : FILL), 14, 4);
    }
    check(blns_gpu_fence(b, false, &tag) == BLNS_OK && blns_gpu_wait_fence(b, tag) == BLNS_OK, 14, 5);
}

// 15) FIFO room: wait_space on an empty FIFO, the FIFO_LOW threshold and
//     its event (an edge when FREE rises above it); with the GPU busy on a
//     large read-modify-write fill and the FIFO filled behind it, no room
//     and no event until wait_space has seen the room come back
static void check_fifo(blns_t *b)
{
    const unsigned depth = b->caps.gpu_fifo_depth;
    const unsigned thr   = depth / 2u - 1u;
    volatile uint16_t *big = v16(b, S_BUSY);
    volatile uint16_t *q   = v16(b, S_MSC + 0x400u);
    unsigned n = 0;
    check(depth >= 8u, 15, 1);
    check(blns_gpu_wait(b) == BLNS_OK && blns_gpu_wait_space(b, depth) == BLNS_OK, 15, 1);
    check(blns_gpu_space(b) == depth, 15, 1);
    check(blns_gpu_wait_space(b, depth + 1u) == BLNS_ERR_PARAM, 15, 1);
    blns_irq_clear(b, BLNS_IRQ_GPU_FIFO_LOW_Msk);
    blns_gpu_fifo_low_set(b, thr);                                      // FREE above it: the event at once
    check(b->regs->GPU.FIFO_LOW == thr, 15, 2);
    check((blns_irq_status(b) & BLNS_IRQ_GPU_FIFO_LOW_Msk) && blns_gpu_fifo_low(b), 15, 2);

    for (unsigned i = 0; i < BUSY_PX; i++)
        big[i] = 0x1234u;
    for (unsigned i = 0; i < 8u; i++)
        q[i] = FILL;
    const blns_surf_t sb = surf16(b, S_BUSY, 64u);
    check(blns_gpu_fill(b, &sb, 64u, BUSY_PX / 64u, 0x1234u, BLNS_ROP_XOR) == BLNS_OK, 15, 3);
    while (blns_gpu_space(b) > thr && n < 8u) {
        const blns_surf_t s1 = surf16(b, S_MSC + 0x400u + 2u * n, 8u);
        check(blns_gpu_fill(b, &s1, 1u, 1u, 0x3000u + n, BLNS_ROP_S) == BLNS_OK, 15, 3);
        n++;
    }
    check(blns_gpu_space(b) <= thr && !blns_gpu_fifo_low(b), 15, 4);
    blns_irq_clear(b, BLNS_IRQ_GPU_FIFO_LOW_Msk);
    check(!(blns_irq_status(b) & BLNS_IRQ_GPU_FIFO_LOW_Msk), 15, 4);   // FREE has not risen yet
    check(blns_gpu_wait_space(b, depth) == BLNS_OK && blns_gpu_space(b) == depth, 15, 5);
    check(blns_irq_status(b) & BLNS_IRQ_GPU_FIFO_LOW_Msk, 15, 6);       // it rose above thr
    check(b->regs->GPU.FIFO_LOW == thr, 15, 6);                         // written back
    check(blns_gpu_wait(b) == BLNS_OK, 15, 7);
    check(big[0] == 0u && big[BUSY_PX - 1u] == 0u && big[BUSY_PX / 2u] == 0u, 15, 7);
    for (unsigned i = 0; i < 8u; i++)
        check(q[i] == ((i < n) ? 0x3000u + i : FILL), 15, 7);
    blns_gpu_fifo_low_set(b, depth / 2u);                               // the reset value again
    blns_irq_clear(b, BLNS_IRQ_GPU_FIFO_LOW_Msk);
}

// 10-15, then the frame shown checked again (the remap's 8 bpp interlude,
// the cursor)
static void check_gpu_features(blns_t *b, volatile uint32_t *vram32)
{
    check_lines(b);
    check_cursor(b);
    check_remap_mask(b);
    check_imm_discard(b);
    check_list(b);
    check_fifo(b);
    for (uint32_t y = 0; y < DISP_H; y++)
        for (uint32_t x = 0; x < DISP_W; x += 2)
            check(vram32[blns_pixel_addr(b, 0, x, y) / 2] == (((uint32_t) pixel(x + 1, y) << 16) | pixel(x, y)), 15, 8);
}

// The cursor is taken at a frame start: two frames after it was shown (or
// hidden) the panel shows it (or not); held four more frames
static void cursor_present(blns_t *b) { check(blns_wait_frames(b, 2u) == BLNS_OK, 11, 10); }
static void cursor_hold(blns_t *b)    { check(blns_wait_frames(b, 4u) == BLNS_OK, 11, 10); }

int main(void)
{
    static blns_t blns;
    LED_CTRL = 0x01;

    //------------------------------------------------------
    // 1) identification
    //------------------------------------------------------
    const aclint_config_t tcfg = { .base = BOARD_ACLINT_BASE, .num_harts = 1u,
                                   .sswi = true, .mtime_hz = BOARD_TIME_HZ };
    aclint_init(&aclint, &tcfg);
    const blns_config_t cfg = { .reg_base = BLNS_REG_BASE, .vram_base = BLNS_VRAM_BASE, .hclk_hz = HCLK_HZ };
    if (blns_init(&blns, &cfg) != BLNS_OK)      fail(1);
    if (blns.version != 0x00010000u)            fail(1);   // BLNS_ID 0.1.0
    if (blns.caps.vram_words != BOARD_VRAM_WORDS) fail(2);
    if (blns.caps.dif_type != (BOARD_HDMI() ? BLNS_DIF_HDMI : BLNS_DIF_DPI)) fail(2);   // built-in DPI driver (HDMI build: with TMDS)

    //------------------------------------------------------
    // 2) VRAM window: words, then a byte lane
    //------------------------------------------------------
    volatile uint32_t *vram32 = blns_vram32(&blns);
    volatile uint8_t  *vram8  = blns_vram8(&blns);
    for (uint32_t i = 0; i < 4; i++)
        vram32[i] = 0xA5000000u ^ (i * 0x01010101u);
    for (uint32_t i = 0; i < 4; i++)
        if (vram32[i] != (0xA5000000u ^ (i * 0x01010101u))) fail(3);
    vram8[5] = 0x5A;
    if (vram32[1] != ((0xA5000000u ^ 0x01010101u) & 0xFFFF00FFu) + 0x00005A00u) fail(4);

    //------------------------------------------------------
    // 3) DPI panel set-up
    //------------------------------------------------------
    const blns_dpi_timing_t lcd  = BOARD_LCD_TIMING;
    const blns_dpi_timing_t hdmi = BOARD_HDMI_TIMING;
    if (blns_dpi_config(&blns, BOARD_HDMI() ? &hdmi : &lcd) != BLNS_OK) fail(2);
    blns_dpi_enable(&blns, true);
    LED_CTRL = 0x02;

    //------------------------------------------------------
    // 3b) HDMI build: the monitor's EDID over the DDC (the bench's test
    //     EDID, ddc_edid_test.vh); RGB panel build: no DDC register
    //------------------------------------------------------
    static blns_edid_t edid;
    if (BOARD_HDMI()) {
        if (blns_dpi_edid_read(&blns, ddc_quarter, &edid) != BLNS_OK)      fail(8);
        if (edid.blocks != 2u || edid.width_mm != 880u || edid.height_mm != 367u ||
            edid.aspect_x100 != 240u || edid.pref_w != 3840u || edid.pref_h != 1600u ||
            edid.pref_pclk_khz != 395000u)                                  fail(8);
        uint8_t b;
        if (blns_dpi_ddc_read(&blns, ddc_quarter, 0x51u, 0u, &b, 1u) != BLNS_ERR_BUS) fail(8);
        // HDMI signalling with an AVI InfoFrame (BOARD_HDMI_VIC)
        const blns_avi_t avi = { .vic     = BOARD_HDMI_VIC,
                                 .picture = BOARD_HDMI_PICTURE,
                                 .active  = BLNS_AVI_ACTIVE_SAME,
                                 .quant   = BLNS_AVI_QUANT_DEFAULT };
        if (blns_dpi_hdmi(&blns, &avi) != BLNS_OK)                          fail(8);
    } else {
        if (blns_dpi_edid_read(&blns, ddc_quarter, &edid) != BLNS_ERR_ABSENT) fail(8);
        if (blns_dpi_hdmi(&blns, 0) != BLNS_ERR_ABSENT)                      fail(8);
    }

    //------------------------------------------------------
    // 4) colour bars, one manual refresh, interrupt
    //------------------------------------------------------
    blns_set_mode(&blns, BLNS_BPP_16);
    blns_set_size(&blns, DISP_W, DISP_H);
    blns_set_frame(&blns, 0, 0);
    for (uint32_t y = 0; y < DISP_H; y++)
        for (uint32_t x = 0; x < DISP_W; x += 2)
            vram32[blns_pixel_addr(&blns, 0, x, y) / 2] = ((uint32_t)pixel(x + 1, y) << 16) | pixel(x, y);

    blns_irq_clear(&blns, BLNS_IRQ_W1C_Msk);
    blns_irq_enable(&blns, BLNS_IRQ_REFR_DONE_Msk);
    const uint16_t fn0 = blns_frame_num(&blns);
    blns_refresh_once(&blns);
    while (blns_refresh_running(&blns)) ;
    LED_CTRL = 0x03;

    if (!(blns_irq_status(&blns) & BLNS_IRQ_REFR_DONE_Msk)) fail(5);
    if (!(read_csr(mip) & MIP_BLNS))            fail(6);
    blns_irq_clear(&blns, BLNS_IRQ_REFR_DONE_Msk);
    if (blns_irq_status(&blns) & BLNS_IRQ_REFR_DONE_Msk)    fail(7);   // read-back orders the W1C
    clear_csr(mip, MIP_BLNS);
    if (read_csr(mip) & MIP_BLNS)               fail(7);
    blns_irq_enable(&blns, 0);

    //------------------------------------------------------
    // 9) the frame counter moved over that refresh; a GPU fill (outside the
    //    frame shown) reaches its fence tag; the double-buffered palette
    //    flips REFR_BANK (no entry loaded: the 16 bpp frame does not use it)
    //------------------------------------------------------
    if (blns_frame_num(&blns) == fn0)           fail(9);
    {
        const blns_surf_t off = { DISP_W * DISP_H + 64u, 8u, BLNS_SWAP_NONE };
        uint32_t tag;
        if (blns_gpu_enable(&blns, true) != BLNS_OK)                        fail(9);
        if (blns_gpu_fill(&blns, &off, 8u, 4u, 0x1234u, BLNS_ROP_S) != BLNS_OK) fail(9);
        if (blns_gpu_fence(&blns, false, &tag) != BLNS_OK)                  fail(9);
        if (blns_gpu_wait_fence(&blns, tag) != BLNS_OK || !blns_gpu_fence_done(&blns, tag)) fail(9);
        if (blns_vram16(&blns)[off.addr] != 0x1234u)                         fail(9);
        const uint32_t bank = blns.regs->LUT_CFG & BLNS_LUT_CFG_REFR_BANK_Msk;
        if (blns_lut_present(&blns, NULL, 0u) != BLNS_OK)                   fail(9);
        if ((blns.regs->LUT_CFG & BLNS_LUT_CFG_REFR_BANK_Msk) == bank)      fail(9);
    }

    check_gpu_features(&blns, vram32);

    LED_CTRL = 0xA5;

#ifndef SIMULATION
    //------------------------------------------------------
    // 5) keep the panel refreshed from the period timer
    //------------------------------------------------------
    blns_refresh_rate(&blns, REFR_FPS);
#endif

    for (;;) ;
}
