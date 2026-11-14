//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      fx.h  (DE0-Nano-SoC Belenos effects demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Shared helpers of the effects: fixed-point sine, random numbers, and the
// list of effects.
//----------------------------------------------------------------------------
#ifndef FX_H
#define FX_H

#include <stdint.h>
#include "demo_kit.h"
#include "logo_data.h"

// Sine and cosine of a 256-step angle, Q14 (16384 = 1.0)
// One-time set-up code: compiled for size
#define COLD             __attribute__((optimize("Os"), noinline))

extern int16_t sin_q14[256];
void fx_sin_init(void);
static inline int32_t fx_sin(unsigned a) { return sin_q14[a & 255u]; }
static inline int32_t fx_cos(unsigned a) { return sin_q14[(a + 64u) & 255u]; }

// xorshift32
uint32_t fx_rand(void);

// Working memory shared by the effects (one runs at a time): each effect lays
// its own structure over it, through a pointer it takes from fx_mem() when it
// starts, and accesses it only through that structure
#define FX_SCRATCH_SIZE  19712u
void *fx_mem(void);
#define FX_MEM_CHECK(type) _Static_assert(sizeof(type) <= FX_SCRATCH_SIZE, #type " too large")

// Hard cut to black: black palette, both frames cleared, F0 shown. Every
// effect starts with it, then loads its own palette.
void fx_clear(void);

// The aRVern logo (fx_logo.c): drawn at full size from its runs at 'base'
// with line pitch 'pitch', colour c as colour_or | c; its colours lit to
// 'lit' / 256 at pal[pal_base + c]
void fx_logo_draw(uint32_t base, unsigned pitch, uint8_t colour_or);
void fx_logo_light(uint8_t pal_base, unsigned lit);
uint32_t fx_logo_colour(unsigned c);           // RGB of logo colour c (1 .. LOGO_PAL_NR-1)

// Scaled drawing: the runs indexed by source row once (in working memory),
// then the logo 'w' pixels wide centred on (cx, cy) in 'frame', nearest
// neighbour, one GPU fill per run and group of screen rows from one source
// row, clipped to the screen; 'box' returns the rectangle covered
typedef struct {
    uint16_t start[LOGO_H + 1u];               // runs of source row y: [start[y], start[y+1])
    uint8_t  run[LOGO_RUNS][3];                // x, length, colour
} fx_logo_rows_t;
typedef struct { int x0, y0, x1, y1; } fx_box_t;
void fx_logo_index(fx_logo_rows_t *rows);
void fx_logo_scaled(uint32_t frame, int cx, int cy, unsigned w, const fx_logo_rows_t *rows,
                    uint8_t colour_or, fx_box_t *box);

// The effects, in running order
void fx_stars(void);        // 1: flight through a starfield
void fx_plasma(void);       // 2: plasma, animated by the palette and the frame pointer
void fx_torus(void);        // 3: flat-shaded spinning torus, double-buffered
void fx_fire(void);         // 4: the aRVern volcano erupting
void fx_scroll(void);       // 5: sine scroller over palette-animated copper bars

#endif // FX_H
