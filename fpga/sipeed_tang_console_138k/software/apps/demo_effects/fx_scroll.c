//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      fx_scroll.c (Tang Console 138K Belenos effects demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// 5: SINE SCROLLER
//
// A message scrolls from right to left, every pixel column of it displaced
// by a moving sine wave, over "copper bars": every screen row has its own
// palette entry, so the bars that sweep up and down are pure palette
// animation, 248 LUT entries rewritten per frame, no pixel touched (the
// entries cover the middle 248 rows; the few rows above and below, out of
// the bars' reach, share the nearest row's entry).
//
// Double-buffered: in the hidden frame the GPU refills the band the text can
// cover (one span per row, the row's own colour), then draws the text column
// by column, each column at its own height: first a dark shadow one pixel
// right and two down, then the text over it. The font sits in the VRAM after
// the two frames; a text column is one EXEC_EXPAND of a 1 x FONT_H rectangle
// from it, its 2-bit levels mapped to the text ramp (or to black).
// The bars run alone for two seconds, then the message enters from the right
// edge; the effect lasts until the whole message has scrolled past.
// The font is IBM Plex Sans Bold, pre-rendered to 2-bit columns (font_data.h,
// tools/font2c.py).
//----------------------------------------------------------------------------
#include "fx.h"

#define FONT_DATA_TABLES
#include "font_data.h"

#define PAL_TEXT         0u                    // text levels 1..3 at 1..3
#define PAL_SHADOW       4u                    // shadow levels 1..3 at 4..6, all black
#define PAL_ROW          8u                    // row ROW_Y0 + i at PAL_ROW + i
#define ROW_NR           ((SCR_H < 256u - PAL_ROW) ? SCR_H : 256u - PAL_ROW)
#define ROW_Y0           ((int) (SCR_H - ROW_NR) / 2)
#define BAR_AMPL         100                   // bar sweep amplitude
#define BAR_HALF         14                    // bar half height
#define BASE_Y           ((int) SCR_H * 15 / 32)  // middle of the wave
#define AMPL             32                    // wave amplitude
#define FONT_PA          (VRAM_SPARE * 4u)     // the font, as 2-bit pixel addresses
#define COL_PX           (FONT_COL_BYTES * 4u) // 2-bit pixels per font column
#define BAND_Y0          (BASE_Y - AMPL - (int) FONT_H / 2 - 1)
#define BAND_Y1          (BASE_Y + AMPL + (int) FONT_H / 2 + 4)
#define SPEED            3u                    // pixels per frame
#define PAUSE            120u                  // frames of bars alone before the text

static const char message[] =
    "HELLO FROM THE ARVERN RISC-V CORE AND THE BELENOS GRAPHICS CONTROLLER, "
    "LIVE ON THE TANG CONSOLE 138K ...  STARS, PLASMA, A SHADED TORUS, FIRE AND THIS "
    "SCROLLER: 33 MHZ, NO OPERATING SYSTEM, LESS THAN 32 KB OF CODE ...  "
    "GREETINGS !!!     ";

typedef struct {
    uint16_t glyph_of[sizeof(message)];        // font glyph of each message character
} scroll_mem_t;
FX_MEM_CHECK(scroll_mem_t);
static scroll_mem_t *mem;

_Static_assert(BAR_AMPL + BAR_HALF <= (int) ROW_NR / 2, "copper bars outside the rows with an entry");

// Palette entry of screen row y
static unsigned row_pal(int y)
{
    int i = y - ROW_Y0;
    return PAL_ROW + (unsigned) ((i < 0) ? 0 : (i >= (int) ROW_NR) ? (int) ROW_NR - 1 : i);
}

static unsigned glyph_index(char c)
{
    static const char chars[] = FONT_CHARS;
    for (unsigned i = 0; i < sizeof(chars) - 1u; i++)
        if (chars[i] == c)
            return i;
    return 0;                                  // space
}

static void bars_palette(unsigned t)
{
    static const uint32_t hue[4] = { 0xe04020u, 0x30c060u, 0x3060e0u, 0xe0b030u };
    for (int y = ROW_Y0; y < ROW_Y0 + (int) ROW_NR; y++) {
        uint32_t c = rgb_lerp(0x0a0620u, 0x000000u, (unsigned) y, SCR_H);
        for (unsigned k = 0; k < 4u; k++) {
            int yk = (int) (SCR_H / 2u) + (fx_sin(t * (2u + k) + k * 64u) * BAR_AMPL >> 14);
            int d  = y - yk;
            if (d < 0)
                d = -d;
            if (d < BAR_HALF)
                c = rgb_lerp(c, hue[k], (unsigned) (BAR_HALF - d), BAR_HALF);
        }
        pal[row_pal(y)] = c;
    }
    pal[PAL_TEXT + 1u] = 0x605040u;
    pal[PAL_TEXT + 2u] = 0xc0b090u;
    pal[PAL_TEXT + 3u] = 0xffffffu;
    pal[PAL_SHADOW]      = 0x000000u;
    pal[PAL_SHADOW + 1u] = 0x000000u;
    pal[PAL_SHADOW + 2u] = 0x000000u;
}

// One row in the row's colour: the session colour, then a span one line
// further down (the first one in place): three FIFO words a row
static void rows(uint32_t frame, int y0, int y1)
{
    blns_gpu_span_begin(&blns, frame + (uint32_t) y0 * SCR_W, 0, row_pal(y0));
    for (int y = y0; y < y1; y++) {
        blns_gpu_colour_set(&blns, row_pal(y));
        blns_gpu_span(&blns, frame + (uint32_t) y * SCR_W, SCR_W);
    }
}

// The text from screen column 'x0' on, starting at message character ci,
// column cc in it; in the shadow pass every ink pixel is written one right
// and two down, black
static void draw_text(uint32_t frame, unsigned x0, unsigned ci, unsigned cc, unsigned t, bool shadow)
{
    static const blns_expand_t col_exp = { .width = 1, .height = FONT_H, .src_pitch = 0,
                                           .depth = BLNS_EXP_2BPP, .opaque = false,
                                           .swap = BLNS_SWAP_NONE };
    const uint32_t base = frame + (shadow ? 2u * SCR_W + 1u : 0u);

    blns_gpu_expand_begin(&blns, &col_exp, base, 0, shadow ? PAL_SHADOW : PAL_TEXT + 1u, 0);
    for (unsigned x = x0; x < SCR_W - 1u; x++) {
        unsigned g = mem->glyph_of[ci];
        if (cc >= (unsigned) (font_start[g + 1u] - font_start[g])) {
            cc = 0;
            if (++ci >= sizeof(message) - 1u)
                ci = 0;
            g = mem->glyph_of[ci];
        }
        int y0 = BASE_Y - (int) FONT_H / 2 + (fx_sin(x + t * 3u) * AMPL >> 14);
        blns_gpu_expand(&blns, FONT_PA + (font_start[g] + cc) * COL_PX,
                        base + (uint32_t) y0 * SCR_W + x);
        cc++;
    }
}

void fx_scroll(void)
{
    mem = fx_mem();
    uint32_t back = F1;
    unsigned ci = 0, cc = 0, cols = 0;

    for (unsigned i = 0; i < sizeof(message) - 1u; i++) {
        unsigned g = glyph_index(message[i]);
        mem->glyph_of[i] = (uint16_t) g;
        cols += (unsigned) (font_start[g + 1u] - font_start[g]);
    }
    // pause, entry from the right edge, the whole message (the simulation
    // shows a part)
    const unsigned frames = FRAMES(PAUSE + (SCR_W + cols) / SPEED);
    unsigned       lead   = SCR_W;             // blank screen columns before the text
    fx_clear();
    volatile uint8_t *font_vram = blns_vram8(&blns) + VRAM_SPARE;
    for (unsigned i = 0; i < sizeof(font_cols); i++)
        font_vram[i] = font_cols[i];
    rows(F0, 0, (int) SCR_H);
    rows(F1, 0, (int) SCR_H);
    blns_gpu_wait(&blns);

    for (unsigned f = 0; f < frames; f++) {
        unsigned t = f;
#ifdef SIMULATION
        t *= 4u;
#endif
        rows(back, BAND_Y0, BAND_Y1);
        if (lead < SCR_W - 1u) {
            draw_text(back, lead, ci, cc, t, true);
            draw_text(back, lead, ci, cc, t, false);
        }
        blns_gpu_wait(&blns);
        blns_set_frame(&blns, 0, back);
        back = (back == F0) ? F1 : F0;
        bars_palette(t);
        pal_show();                            // waits for the frame
        if (scene_check())
            break;
        // advance the scroll: after the pause, first the text enters
        if (f < FRAMES(PAUSE))
            continue;
        if (lead > 0u) {
            lead = (lead > SPEED) ? lead - SPEED : 0u;
            continue;
        }
        for (unsigned s = 0; s < SPEED
#ifdef SIMULATION
             * 6u
#endif
             ; s++) {
            unsigned g = mem->glyph_of[ci];
            if (++cc >= (unsigned) (font_start[g + 1u] - font_start[g])) {
                cc = 0;
                if (++ci >= sizeof(message) - 1u)
                    ci = 0;
            }
        }
    }
    blns_set_frame(&blns, 0, F0);
}
