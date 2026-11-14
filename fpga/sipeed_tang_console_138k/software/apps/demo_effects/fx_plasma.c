//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      fx_plasma.c (Tang Console 138K Belenos effects demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// 2: PLASMA
//
// The classic plasma: every pixel of a 480 x 544 pattern (both frames) gets
// a colour index from a sum of four sines (in x, in y, along the diagonal and
// in rings around a point). The CPU computes it four pixels per 32-bit write
// through the VRAM window: the visible half first, the lower half a slice per
// frame while the animation already runs. Then nothing is drawn: every frame
// rebuilds the 256-entry palette from three drifting sine waves and shifts it
// (colour cycling), and the refresh frame pointer bobs up and down over the
// taller pattern.
//----------------------------------------------------------------------------
#include "fx.h"

#define PL_H             (2u * SCR_H)          // pattern height: both frames
#define RING_X           (3u * SCR_W / 8u)     // centre of the rings
#define RING_Y           (5u * PL_H / 8u)

typedef struct {
    int16_t col_term[SCR_W];                   // sin(x) part
    int16_t sq[SCR_W];                         // (x - RING_X)^2 / 64, for the rings
} plasma_mem_t;
FX_MEM_CHECK(plasma_mem_t);
static plasma_mem_t *mem;
#define col_term         (mem->col_term)
#define sq               (mem->sq)

static uint8_t plasma_at(unsigned x, unsigned y, int row_term)
{
    int dy  = (int) y - (int) RING_Y;
    int r2  = sq[x] + (dy * dy >> 6);
    int v   = col_term[x] + row_term + fx_sin((x + y) * 2u) + fx_sin((unsigned) r2 >> 1);
    return (uint8_t) ((v + 65536) >> 9);       // -65536..65535 to 0..255
}

static void plasma_init(void)
{
    for (unsigned x = 0; x < SCR_W; x++) {
        int dx = (int) x - (int) RING_X;
        col_term[x] = (int16_t) fx_sin(x * 3u);
        sq[x]       = (int16_t) (dx * dx >> 6);
    }
}

static void plasma_draw(unsigned y0, unsigned y1)
{
    volatile uint32_t *vram = blns_vram32(&blns);

    for (unsigned y = y0; y < y1; y++) {
        int row_term = fx_sin(y * 4u);
        volatile uint32_t *row = vram + y * (SCR_W / 4u);
        for (unsigned x = 0; x < SCR_W; x += 4u)
            row[x / 4u] =  (uint32_t) plasma_at(x,      y, row_term)        |
                          ((uint32_t) plasma_at(x + 1u, y, row_term) << 8)  |
                          ((uint32_t) plasma_at(x + 2u, y, row_term) << 16) |
                          ((uint32_t) plasma_at(x + 3u, y, row_term) << 24);
    }
}

// Palette: three sine waves (red, green, blue) over the 256 indices, with
// phases and frequencies that drift with time t
static void plasma_palette(unsigned t)
{
    unsigned fr = 1u + ((t >> 7) & 1u);        // frequency doubles now and then
    for (unsigned i = 0; i < 256u; i++) {
        unsigned a = i * fr + t * 2u;
        int r = 128 + (fx_sin(a)             * 127 >> 14);
        int g = 128 + (fx_sin(a + 85u + t)   * 127 >> 14);
        int b = 128 + (fx_sin(a + 170u - t)  * 127 >> 14);
        pal[i] = ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
    }
}

void fx_plasma(void)
{
    mem = fx_mem();
    const unsigned frames = FRAMES(600u);

    fx_clear();
    plasma_init();
    plasma_draw(0, SCR_H);

    for (unsigned f = 0; f < frames; f++) {
        unsigned t = f;
#ifdef SIMULATION
        t *= 16u;
#endif
        // lower half: 16 lines a frame, ahead of the bobbing (which starts
        // at the top and moves down only once it is complete)
        unsigned done = SCR_H + 16u * f;
        if (done < PL_H)
            plasma_draw(done, done + 16u);
        // bob over the pattern: 0..SCR_H lines
        unsigned line = (done < PL_H) ? 0u
                      : (unsigned) ((int) (SCR_H / 2u) - (fx_cos(t / 2u) * (int) (SCR_H / 2u) >> 14));
        plasma_palette(t);
        // fade in over the first frames
        if (f < FRAMES(60u))
            for (unsigned i = 0; i < 256u; i++)
                pal[i] = rgb_lerp(0, pal[i], f, FRAMES(60u));
        blns_set_frame(&blns, 0, line * SCR_W);
        pal_show();                            // waits for the frame
        if (scene_check())
            break;
    }
    blns_set_frame(&blns, 0, F0);
}
