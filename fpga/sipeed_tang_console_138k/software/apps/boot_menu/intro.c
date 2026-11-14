//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      intro.c  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The boot animation, after the first two scenes of ../demo_arvern_logo: the
// logo painted one colour layer at a time over a gradient with its circuit
// traces dark, the wordmark typed in, the subtitle, then pulses of light
// running along the traces up to the RV mark while they power up and the
// project's address fades in; a fade to black ends it. The synthesizer's
// boot chime plays along. About six seconds; any key or button skips it.
//----------------------------------------------------------------------------
#include <string.h>
#include "tang_board.h"
#include "gfx.h"
#include "synth.h"
#include "intro.h"
#include "logo_data.h"

// Phase lengths in frames. The simulation build shows the finished title
// screen for one frame, then fades out in one.
#ifdef SIMULATION
#define SHORT            1
#define PH(n)            1u
#else
#define SHORT            0
#define PH(n)            (n)
#endif

// Phases in frames (60 a second), the paint starting with the fade-in: the
// power-up meets the chime's bloom (synth.c), 2.33 s in
#define T_PAINT          75u
#define T_TYPE           65u
#define T_POWER          120u
#define T_HOLD           110u

#define LOGO_X           ((SCR_W - LOGO_W) / 2u)
#define LOGO_Y           ((SCR_H - (LOGO_H + 10u + WORDMARK_H + 12u + TXT_SUB_H + 14u + LINK_H)) / 2u)
#define WORD_X           ((SCR_W - WORDMARK_W) / 2u)
#define WORD_Y           (LOGO_Y + LOGO_H + 10u)
#define SUB_X            ((SCR_W - TXT_SUB_W) / 2u)
#define SUB_Y            (WORD_Y + WORDMARK_H + 12u)
#define LINK             "github.com/Arvern-Silicon"
#define LINK_H           16u                // the menu font
#define LINK_X           (((SCR_W - 8u * (sizeof(LINK) - 1u)) / 2u) & ~3u)
#define LINK_Y           (SUB_Y + TXT_SUB_H + 14u)

#define GRAD_TOP         0x1A3A78u
#define GRAD_BOTTOM      0x03060Fu
#define TEXT_BG          0x0A1630u
#define TRACE_DIM        0x0E2236u
#define TRACE_BASE_RGB   0x4286BDu
#define TRACE_PEAK       0xD8F6FFu
#define LINK_RGB         0x8FB4D8u
#define PULSE_LEN        6u                 // traces lit behind a pulse front
#define PULSE_GAP        12u                // trace positions between pulses

static bool (*skip)(void);
static bool skipped;

// One frame: the palette updated; false once skipped
static bool frame(void)
{
    wait_frames(1u);
    gfx_pal_changed();
    (void) gfx_frame();
    if (!skipped && skip())
        skipped = true;
    return !skipped;
}

static void traces_set(unsigned front, uint32_t base_rgb)
{
    for (unsigned k = 0; k < LOGO_TRACE_NR; k++) {
        unsigned d = (front + PULSE_GAP * 4u - k) % PULSE_GAP;
        pal[PAL_LOGO + LOGO_TRACE_BASE + k] = (d < PULSE_LEN) ? rgb_lerp(TRACE_PEAK, base_rgb, d, PULSE_LEN)
                                                              : base_rgb;
    }
}

static void draw_layers(unsigned first, unsigned end)
{
    gfx_sync();
    for (unsigned l = first; l < end && l < LOGO_LAYERS; l++)
        for (unsigned k = logo_layer_start[l]; k < logo_layer_start[l + 1u]; k++) {
            const uint8_t *r = &logo_runs[3u * k];
            memset(vram + (LOGO_Y + r[0]) * SCR_W + LOGO_X + r[1], PAL_LOGO + logo_layer_idx[l], r[2]);
        }
}

void intro(bool (*skip_fn)(void))
{
    skip = skip_fn;
    skipped = false;
    gfx_animate(false);
    if (!SHORT)                             // the simulation build: no chime
        synth_chime(true);

    // The gradient in the background entries, the traces dark
    for (unsigned i = 0; i < GRAD_NR; i++)
        pal[PAL_GRAD + i] = rgb_lerp(GRAD_TOP, GRAD_BOTTOM, i, GRAD_NR - 1u);
    for (unsigned l = 1; l <= 3u; l++) {
        pal[PAL_RAMP2 + l] = rgb_lerp(TEXT_BG, 0xF2F2F2u, l, 3u);
        pal[PAL_RAMP3 + l] = rgb_lerp(TEXT_BG, 0xE2702Au, l, 3u);
    }
    traces_set(PULSE_GAP, TRACE_DIM);
    const uint32_t link_bg = pal[PAL_GRAD + (LINK_Y + LINK_H / 2u) * GRAD_NR / SCR_H];
    pal[PAL_LINK] = link_bg;
    gfx_sync();
    for (unsigned y = 0; y < SCR_H; y++)
        memset(vram + y * SCR_W, PAL_GRAD + y * GRAD_NR / SCR_H, SCR_W);
    (void) text(LINK_X, LINK_Y, LINK, PAL_LINK, C_KEEP, sizeof(LINK) - 1u);
    gfx_fade(256u, PH(15u), false);
    if (SHORT) {
        draw_layers(0, LOGO_LAYERS);
        draw_wordmark(WORD_X, WORD_Y, PAL_RAMP2, WORDMARK_W);
        draw_bits2(txt_sub_bits, TXT_SUB_W, TXT_SUB_H, TXT_SUB_STRIDE, TXT_SUB_W, SUB_X, SUB_Y, PAL_RAMP3);
        traces_set(0u, TRACE_BASE_RGB);
        pal[PAL_LINK] = LINK_RGB;
        (void) frame();
        goto out;
    }

    // Paint, then type: the power-up starts with the chime's bloom
    for (unsigned f = 0; f < PH(T_PAINT); f++) {
        draw_layers(f * LOGO_LAYERS / PH(T_PAINT), (f + 1u) * LOGO_LAYERS / PH(T_PAINT));
        if (!frame())
            goto out;
    }
    for (unsigned f = 1; f <= PH(T_TYPE); f++) {
        draw_wordmark(WORD_X, WORD_Y, PAL_RAMP2, f * WORDMARK_W / PH(T_TYPE));
        if (!frame())
            goto out;
    }
    draw_bits2(txt_sub_bits, TXT_SUB_W, TXT_SUB_H, TXT_SUB_STRIDE, TXT_SUB_W, SUB_X, SUB_Y, PAL_RAMP3);

    // Power-up: pulses run inward while the traces' base level rises
    const unsigned steps = PH(T_POWER);
    for (unsigned f = 0; f < steps; f++) {
        traces_set(f / 2u, rgb_lerp(TRACE_DIM, TRACE_BASE_RGB, f, steps));
        pal[PAL_LINK] = rgb_lerp(link_bg, LINK_RGB, f, steps);
        if (!frame())
            goto out;
    }
    traces_set(0u, TRACE_BASE_RGB);
    pal[PAL_LINK] = LINK_RGB;
    for (unsigned f = 0; f < PH(T_HOLD); f++)
        if (!frame())
            goto out;
out:
    if (skipped)
        synth_chime(false);
    gfx_fade(0u, PH(20u), false);
    for (unsigned f = 0; gfx_fading() && f < 4u * PH(20u); f++)
        (void) frame();
    gfx_animate(true);
}
