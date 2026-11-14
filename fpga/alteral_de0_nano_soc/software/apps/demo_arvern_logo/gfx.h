//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      gfx.h  (DE0-Nano-SoC aRVern logo demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Graphics layer of the logo demo, on top of the shared demo_kit: the theme
// palettes and the drawing of the gradient, the logo and the text.
//
// Palette (both LUT banks hold the same layout):
//   0          black (also the transparent colour of the sprite copies)
//   1..43      logo colours, 20..43 being the circuit traces (logo_data.h)
//   48..51     white text ramp (coverage levels 1..3 at 49..51)
//   52..55     orange text ramp
//   64..127    background gradient, top to bottom
//   128..255   index ^ 0x80: the colours the XOR raster operation produces
//----------------------------------------------------------------------------
#ifndef GFX_H
#define GFX_H

#include "demo_kit.h"

// Palette layout
#define PAL_BLACK        0u
#define PAL_WHITE        48u
#define PAL_ORANGE       52u
#define PAL_GRAD         64u
#define PAL_GRAD_NR      64u
#define PAL_XOR          0x80u

// Set-up: the theme palettes, then demo_init()
void     gfx_init(void);

// Load the working palette with a theme (0 light, 1 dark); pal_show() displays it
void     pal_theme(unsigned theme);
// The two theme palettes (0 light, 1 dark), complete
extern uint32_t theme_pal[2][256];

// Drawing
void     draw_gradient(uint32_t frame);
void     draw_gradient_rect(uint32_t frame, unsigned x, unsigned y, unsigned w, unsigned h);
void     draw_runs_layer(const uint8_t *runs, unsigned first, unsigned end, uint8_t colour,
                         uint32_t base, unsigned pitch);
void     draw_logo_layers(uint32_t base, unsigned pitch, unsigned first, unsigned end);
void     draw_sprite(uint32_t base, unsigned pitch, uint8_t colour_or);

// Text bitmaps (wordmark and text lines): the first 'cols' columns, one GPU
// EXPAND from a VRAM copy, coverage levels 1..3 in colours ramp+1..ramp+3
typedef enum {
    TXT_WORDMARK, TXT_SUB,
    TXT_A0, TXT_A1, TXT_A2, TXT_A3, TXT_A4,     // aRVern core credits
    TXT_B0, TXT_B1, TXT_B2, TXT_B3, TXT_B4,     // Belenos credits
    TXT_GIT,
    TXT_NR
} txt_id_t;

void     draw_text(txt_id_t id, uint32_t base, uint8_t ramp, unsigned cols);
unsigned text_w(txt_id_t id);
unsigned text_h(txt_id_t id);

#endif // GFX_H
