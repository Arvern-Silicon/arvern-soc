//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      txt.c  (DE0-Nano-SoC text demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Text engine of the text demo (see txt.h).
//----------------------------------------------------------------------------
#define TEXT_DATA_TABLES
#include "txt.h"
#include "text_data.h"

// VRAM layout of the fonts, in frame 1 (byte addresses)
#define VB_MONO          (F1)
#define VB_SANS          (F1 + 2048u)
#define VB_BIG           (F1 + 8192u)
#define VB_TITLE         (F1 + 24576u)

// The immediate source takes a cell as whole words: mono (4) and big (88)
// cells are; a sans cell (400 bits) starts inside a word
_Static_assert(MONO_W * MONO_H % 32u == 0u && BIG_W * BIG_H * 4u % 32u == 0u &&
               TITLE_W * TITLE_H * 2u % 32u == 0u, "cells of whole words");
font_t font_mono = { (VB_MONO  * 8u), MONO_W, MONO_H, BLNS_EXP_1BPP, 0,         (const uint32_t *) mono_bits };
font_t font_sans = { (VB_SANS  * 4u), SANS_W, SANS_H, BLNS_EXP_2BPP, 0,         0 };
font_t font_big  = { (VB_BIG   * 2u), BIG_W,  BIG_H,  BLNS_EXP_4BPP, BIG_CHARS, (const uint32_t *) big_bits };

int16_t sin_q14[256];

//==========================================================
// SET-UP
//==========================================================

// Quarter wave by the Chebyshev recurrence, Q30, then mirrored
__attribute__((optimize("Os"), noinline)) void sin_init(void)
{
    const int64_t two_cos = 2LL * 1073418433;            // 2 cos(2 pi / 256), Q30
    int64_t s0 = 0, s1 = 26350943;                     // sin(0), sin(2 pi / 256), Q30

    for (unsigned n = 0; n <= 64u; n++) {
        int16_t v = (int16_t) ((s0 + (1 << 15)) >> 16);
        sin_q14[n] = v;
        sin_q14[128u - n] = v;
        sin_q14[(128u + n) & 255u] = (int16_t) -v;
        sin_q14[(256u - n) & 255u] = (int16_t) -v;
        int64_t s2 = ((two_cos * s1) >> 30) - s0;
        s0 = s1;
        s1 = s2;
    }
}

static void copy(uint32_t vram_byte, const uint8_t *src, unsigned n)
{
    volatile uint8_t *v = blns_vram8(&blns) + vram_byte;
    for (unsigned i = 0; i < n; i++)
        v[i] = src[i];
}

void txt_upload(void)
{
    blns_gpu_wait(&blns);
    copy(VB_SANS,  sans_bits,  sizeof(sans_bits));
    if (!blns.caps.gpu_imm) {                          // else pushed from ROM
        copy(VB_MONO,  mono_bits,  sizeof(mono_bits));
        copy(VB_BIG,   big_bits,   sizeof(big_bits));
        copy(VB_TITLE, title_bits, sizeof(title_bits));
    }
}

void txt_title_image(uint8_t colour)
{
    const blns_expand_t e = { .width = TITLE_W, .height = TITLE_H, .src_pitch = 0,
                              .depth = BLNS_EXP_2BPP, .opaque = true, .swap = BLNS_SWAP_NONE };
    blns_gpu_expand_begin(&blns, &e, VRAM_SPARE, TITLE_W, colour, 0);
    if (blns.caps.gpu_imm)                             // 640 words from ROM
        blns_gpu_expand_imm(&blns, VRAM_SPARE, (const uint32_t *) title_bits);
    else
        blns_gpu_expand(&blns, VB_TITLE * 4u, VRAM_SPARE);
    blns_gpu_wait(&blns);
}

//==========================================================
// STRINGS
//==========================================================

static int glyph(const font_t *f, char c)
{
    if (!f->chars)
        return (c >= 32 && c < 127) ? c - 32 : 0;
    for (int i = 0; f->chars[i]; i++)
        if (f->chars[i] == c)
            return i;
    return 0;                                          // space
}

unsigned txt_width(const font_t *f, const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n * f->w;
}

unsigned txt_draw(const font_t *f, uint32_t dst, const char *s, uint8_t colour,
                  uint8_t bg, bool opaque, txt_dir_t dir)
{
    static const uint8_t swap[4] = { BLNS_SWAP_NONE, BLNS_SWAP_CL | BLNS_SWAP_X,
                                     BLNS_SWAP_X | BLNS_SWAP_Y, BLNS_SWAP_CL | BLNS_SWAP_Y };
    const uint32_t w = f->w, h = f->h, n = txt_width(f, s) / w;
    const uint32_t words = w * h << f->depth >> 5;     // a cell, with f->rom
    const bool     imm = f->rom && blns.caps.gpu_imm;
    int32_t        adv;
    uint32_t       at;

    if (n == 0u)
        return 0;
    // 'dst' is the top-left corner of the string's box; 'at' the first pixel
    // the walk of the first glyph visits
    switch (dir) {
    case TXT_DOWN:  at = dst + (h - 1u);                                       adv = (int32_t) (w * SCR_W);  break;
    case TXT_LEFT:  at = dst + (n - 1u) * w + (h - 1u) * SCR_W + (w - 1u);     adv = -(int32_t) w;           break;
    case TXT_UP:    at = dst + ((n - 1u) * w + (w - 1u)) * SCR_W;              adv = -(int32_t) (w * SCR_W); break;
    default:        at = dst;                                                  adv = (int32_t) w;            break;
    }
    const blns_expand_t e = { .width = (uint16_t) w, .height = (uint16_t) h, .src_pitch = 0,
                              .depth = f->depth, .opaque = opaque, .swap = swap[dir] };
    blns_gpu_expand_begin(&blns, &e, at, 0, colour, bg);
    for (unsigned i = 0; i < n; i++, at += (uint32_t) adv) {
        const uint32_t g = (uint32_t) glyph(f, s[i]);
        if (imm)                                       // the cell behind its EXEC_EXPAND
            blns_gpu_expand_imm(&blns, at, f->rom + g * words);
        else
            blns_gpu_expand(&blns, f->src + g * w * h, at);
    }
    return n;
}
