//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      gfx.c  (DE0-Nano-SoC aRVern logo demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Graphics layer of the logo demo (see gfx.h).
//----------------------------------------------------------------------------
#define LOGO_DATA_TABLES
#include "de0_board.h"
#include "gfx.h"
#include "logo_data.h"

uint32_t theme_pal[2][256];

//==========================================================
// PALETTE
//==========================================================

// Gradient end colours (light theme: night blue, dark theme: charcoal) and
// text colours
static const uint32_t grad_top[2]    = { 0x1a3a78u, 0x34343cu };
static const uint32_t grad_bottom[2] = { 0x03060fu, 0x060607u };
#define TEXT_BG          0x0a1630u
#define TEXT_WHITE       0xf2f2f2u
#define TEXT_ORANGE      0xe2702au

void pal_theme(unsigned theme)
{
    for (unsigned i = 0; i < 256u; i++)
        pal[i] = theme_pal[theme][i];
}

static void build_theme(unsigned theme)
{
    const uint32_t *logo = theme ? logo_pal_dark : logo_pal_light;
    uint32_t       *p    = theme_pal[theme];

    for (unsigned i = 0; i < 256u; i++)
        p[i] = 0;
    for (unsigned i = 1; i < LOGO_PAL_NR; i++)
        p[i] = logo[i];
    for (unsigned l = 1; l <= 3u; l++) {
        p[PAL_WHITE  + l] = rgb_lerp(TEXT_BG, TEXT_WHITE,  l, 3);
        p[PAL_ORANGE + l] = rgb_lerp(TEXT_BG, TEXT_ORANGE, l, 3);
    }
    for (unsigned i = 0; i < PAL_GRAD_NR; i++)
        p[PAL_GRAD + i] = rgb_lerp(grad_top[theme], grad_bottom[theme], i, PAL_GRAD_NR - 1u);
    // XOR images: the gradient turns into a bright cyan ramp, everything else
    // into its complement
    for (unsigned i = 0; i < 128u; i++)
        p[i | PAL_XOR] = (i >= PAL_GRAD) ? rgb_lerp(0x40e0ffu, 0x1060a0u, i - PAL_GRAD, PAL_GRAD_NR - 1u)
                                           : (~p[i] & 0xFFFFFFu);
}

//==========================================================
// SET-UP
//==========================================================

static void text_upload(void);

void gfx_init(void)
{
    build_theme(0);
    build_theme(1);
    pal_theme(0);
    demo_init();
    text_upload();
}

//==========================================================
// DRAWING
//==========================================================

static unsigned grad_band(unsigned y)
{
    return y * PAL_GRAD_NR / SCR_H;
}

void draw_gradient_rect(uint32_t frame, unsigned x, unsigned y, unsigned w, unsigned h)
{
    unsigned y0 = y;

    while (y0 < y + h) {
        unsigned band = grad_band(y0);
        unsigned y1   = y0 + 1u;
        while (y1 < y + h && grad_band(y1) == band)
            y1++;
        fill(frame + y0 * SCR_W + x, w, y1 - y0, (uint8_t) (PAL_GRAD + band));
        y0 = y1;
    }
}

void draw_gradient(uint32_t frame)
{
    draw_gradient_rect(frame, 0, 0, SCR_W, SCR_H);
}

// One EXEC_SPAN word per run: the colour is set once per layer, each run is a
// delta from the previous one
void draw_runs_layer(const uint8_t *runs, unsigned first, unsigned end, uint8_t colour,
                     uint32_t base, unsigned pitch)
{
    if (first < end)
        blns_gpu_span_begin(&blns, base + runs[3u * first] * pitch + runs[3u * first + 1u],
                            (uint16_t) pitch, colour);
    for (unsigned k = first; k < end; k++) {
        const uint8_t *r = &runs[3u * k];
        blns_gpu_span(&blns, base + r[0] * pitch + r[1], r[2]);
    }
}

void draw_logo_layers(uint32_t base, unsigned pitch, unsigned first, unsigned end)
{
    for (unsigned l = first; l < end && l < LOGO_LAYERS; l++)
        draw_runs_layer(logo_runs, logo_layer_start[l], logo_layer_start[l + 1u],
                        logo_layer_idx[l], base, pitch);
}

void draw_sprite(uint32_t base, unsigned pitch, uint8_t colour_or)
{
    for (unsigned l = 0; l < SPRITE_LAYERS; l++)
        draw_runs_layer(sprite_runs, sprite_layer_start[l], sprite_layer_start[l + 1u],
                        (uint8_t) (sprite_layer_idx[l] | colour_or), base, pitch);
}

static const struct {
    const uint8_t *bits;
    uint16_t       w, h, stride;
} texts[TXT_NR] = {
    { wordmark_bits,  WORDMARK_W,  WORDMARK_H,  WORDMARK_STRIDE   },
    { txt_sub_bits,   TXT_SUB_W,   TXT_SUB_H,   TXT_SUB_STRIDE    },
    { txt_a0_bits,    TXT_A0_W,    TXT_A0_H,    TXT_A0_STRIDE     },
    { txt_a1_bits,    TXT_A1_W,    TXT_A1_H,    TXT_A1_STRIDE     },
    { txt_a2_bits,    TXT_A2_W,    TXT_A2_H,    TXT_A2_STRIDE     },
    { txt_a3_bits,    TXT_A3_W,    TXT_A3_H,    TXT_A3_STRIDE     },
    { txt_a4_bits,    TXT_A4_W,    TXT_A4_H,    TXT_A4_STRIDE     },
    { txt_b0_bits,    TXT_B0_W,    TXT_B0_H,    TXT_B0_STRIDE     },
    { txt_b1_bits,    TXT_B1_W,    TXT_B1_H,    TXT_B1_STRIDE     },
    { txt_b2_bits,    TXT_B2_W,    TXT_B2_H,    TXT_B2_STRIDE     },
    { txt_b3_bits,    TXT_B3_W,    TXT_B3_H,    TXT_B3_STRIDE     },
    { txt_b4_bits,    TXT_B4_W,    TXT_B4_H,    TXT_B4_STRIDE     },
    { txt_git_bits,   TXT_GIT_W,   TXT_GIT_H,   TXT_GIT_STRIDE    },
};

// The 2-bit bitmaps are copied once into the VRAM spare above the two frames
// (word-aligned, rows of 'stride' bytes): the GPU expands them from there
#define TEXT_VRAM_WORD   (2u * FRAME_PX / 4u)                // first word after F1 (8 bpp)
#define TW(n)            ((TXT_##n##_STRIDE * TXT_##n##_H + 3u) / 4u)
_Static_assert(TEXT_VRAM_WORD + (WORDMARK_STRIDE * WORDMARK_H + 3u) / 4u + TW(SUB) + TW(A0) + TW(A1) +
               TW(A2) + TW(A3) + TW(A4) + TW(B0) + TW(B1) + TW(B2) + TW(B3) + TW(B4) + TW(GIT)
               <= BOARD_VRAM_WORDS, "the text bitmaps do not fit the VRAM after the frames");
static uint32_t text_src[TXT_NR];                            // 2-bit source pixel address

static void text_upload(void)
{
    volatile uint32_t *vram = (volatile uint32_t *) BOARD_VRAM_BASE;
    uint32_t           word = TEXT_VRAM_WORD;

    for (unsigned t = 0; t < TXT_NR; t++) {
        unsigned n = (unsigned) texts[t].stride * texts[t].h;
        text_src[t] = word * 16u;
        for (unsigned i = 0; i < n; i += 4u) {
            uint32_t v = 0;
            for (unsigned b = 0; b < 4u && i + b < n; b++)
                v |= (uint32_t) texts[t].bits[i + b] << (8u * b);
            vram[word++] = v;
        }
    }
}

// A text bitmap's first 'cols' columns: one GPU EXPAND, value 0 transparent,
// coverage levels 1..3 in colours ramp+1..ramp+3
void draw_text(txt_id_t id, uint32_t base, uint8_t ramp, unsigned cols)
{
    blns_expand_t e = { 0 };

    if (cols > texts[id].w)
        cols = texts[id].w;
    if (cols == 0u)
        return;
    e.width     = (uint16_t) cols;
    e.height    = texts[id].h;
    e.src_pitch = (uint16_t) (4u * texts[id].stride);
    e.depth     = BLNS_EXP_2BPP;
    blns_gpu_expand_begin(&blns, &e, base, SCR_W, (uint32_t) ramp + 1u, 0u);
    blns_gpu_expand(&blns, text_src[id], base);
}

unsigned text_w(txt_id_t id) { return texts[id].w; }
unsigned text_h(txt_id_t id) { return texts[id].h; }
