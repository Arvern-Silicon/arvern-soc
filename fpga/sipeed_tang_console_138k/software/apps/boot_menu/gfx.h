//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      gfx.h  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Drawing and palette of the boot menu: CPU drawing in the 8 bpp frame F0,
// and a palette composed once a frame (the animated entries, then the fade)
// into the LUT bank the refresh is not showing.
//
// The background is a vertical gradient with a field of stars drifting
// towards the viewer, redrawn once a frame with the palette; a jump to
// hyperspace (gfx_warp) streaks them. The menu sits on glass panels: each
// panel's area, and the title band's, is registered in a slot, and no star
// is drawn or erased inside a registered area.
//
// LUT entries:
//   0..15     the menu's colours (C_*)
//   16..47    the background gradient, one entry per 8.5 rows
//   48..50    the glass panels: fill, edges, top highlight
//   51..62    the stars, dim to bright
//   64..107   the logo (64 + its colour index)
//   112..115  the wordmark's coverage ramp on the title band, 116..123
//             two more ramps and 124 the link (the boot animation)
//   128..239  the preview image of the selection (APP.IMG)
// C_BGA is not an entry: drawn as a background, it stands for the gradient
// entry of each row.
//----------------------------------------------------------------------------
#ifndef GFX_H
#define GFX_H

#include <stdbool.h>
#include <stdint.h>
#include "demo_kit.h"

enum {
    C_BG, C_TEXT, C_DIM, C_BAR, C_BAND, C_ACCENT, C_ERROR, C_PROGRESS,
    C_LOAD_DONE, C_LOAD_TODO, C_LOAD_TEXT, C_LOAD_ERR,  // kept through the launch fade
    C_GOOD, C_WARN, C_ICON, C_FRAME
};
#define C_BGA        0xFFu                  // the background
#define C_KEEP       0xFEu                  // text background: the pixels left as they are

#define PAL_GRAD     16u
#define GRAD_NR      32u
#define C_PANEL      48u
#define C_EDGE       49u
#define C_HI         50u
#define PAL_STAR     51u
#define STAR_SHADES  12u
#define PAL_LOGO     64u
#define PAL_WORD     112u
#define PAL_RAMP2    116u
#define PAL_RAMP3    120u
#define PAL_LINK     124u
#define PAL_IMG      128u
#define PAL_IMG_NR   112u

extern uint8_t *vram;                       // F0; demo_kit's pal[] is the palette before animation and fade

void     gfx_pal_init(void);                // the menu's palette into pal[], before demo_init
void     gfx_init(void);                    // after demo_init
// Once a frame has passed since the last update (never waits): the palette
// animated and faded into the hidden LUT bank, shown from the next frame on.
// Returns true when it updated. The simulation build does not animate and
// uploads only a changed or fading palette.
bool     gfx_frame(void);
void     gfx_animate(bool on);              // the stars and the selection glow
uint32_t gfx_frame_nr(void);                // palette updates so far
void     gfx_pal_changed(void);             // pal[] changed: upload it at the next frame
// Fade towards 'level' (0 black .. 256 full) over 'frames' updates; with
// 'keep' the C_LOAD_* entries stay as they are
void     gfx_fade(unsigned level, unsigned frames, bool keep);
bool     gfx_fading(void);

static inline uint8_t bg_at(unsigned y) { return (uint8_t) (PAL_GRAD + y * GRAD_NR / SCR_H); }

// Areas kept free of stars
enum { GL_HEAD, GL_LIST, GL_PREV, GL_DESC, GL_HELP, GL_BIG, GL_NR };
void     gfx_glass(unsigned slot, unsigned x, unsigned y, unsigned w, unsigned h);   // a panel drawn there
void     gfx_keep_out(unsigned slot, unsigned x, unsigned y, unsigned w, unsigned h);
void     gfx_glass_off(unsigned slot);      // its area back to the background
void     gfx_stars(bool on);                // off: erased
void     gfx_warp(bool on);                 // the jump to hyperspace, or back to a drift
void     gfx_sync(void);                    // the GPU's stars drawn (before CPU stores into vram)

void     rect(unsigned x, unsigned y, unsigned w, unsigned h, uint8_t c);
unsigned text(unsigned x, unsigned y, const char *s, uint8_t fg, uint8_t bg, unsigned n);
void     draw_logo(unsigned x, unsigned y);
void     draw_bits2(const uint8_t *bits, unsigned w, unsigned h, unsigned stride, unsigned cols,
                    unsigned x, unsigned y, uint8_t ramp);
void     draw_wordmark(unsigned x, unsigned y, uint8_t ramp, unsigned cols);
void     draw_blobs(unsigned x, unsigned y, unsigned w, unsigned h, uint32_t seed);   // a picture for no picture
// The same in steps, so that the menu keeps polling its keys meanwhile: its
// palette entries first .. first+n-1 (PAL_IMG_NR in all), then its rows
// ya .. yb-1
void     draw_blobs_pal(uint32_t seed, unsigned first, unsigned n);
void     draw_blobs_rows(unsigned x, unsigned y, unsigned w, uint32_t seed, unsigned ya, unsigned yb);

#endif // GFX_H
