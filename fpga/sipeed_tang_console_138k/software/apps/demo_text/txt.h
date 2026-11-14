//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      txt.h  (Tang Console 138K text demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Text engine of the text demo. The fonts (text_data.h) are copied into the
// VRAM area of frame 1; a string is one EXEC_EXPAND per character, the GPU
// drawing each glyph while the CPU pushes the next one (three FIFO words).
// With the immediate source (caps.gpu_imm) the fonts whose cells are whole
// words (mono: 4, big: 88) and the title are pushed from ROM behind their
// EXEC_EXPAND instead: only the sans font (400-bit cells, not word-aligned)
// stays in the VRAM.
//
// A string goes from a start pixel in a direction: the destination swaps of
// the glyph (TXT_* below) and the step to the next character go together.
//----------------------------------------------------------------------------
#ifndef TXT_H
#define TXT_H

#include <stdbool.h>
#include <stdint.h>
#include "demo_kit.h"

typedef struct {
    uint32_t    src;            // source pixel address of glyph 0, at the font depth
    uint8_t     w, h;           // cell size
    uint8_t     depth;          // BLNS_EXP_1BPP / 2BPP / 4BPP
    const char *chars;          // characters in glyph order (NULL: ASCII from 32)
    const uint32_t *rom;        // the cells in ROM, whole words each (NULL: VRAM only)
} font_t;

extern font_t font_mono, font_sans, font_big;

// Orientation of a string: glyph swaps and direction
typedef enum {
    TXT_RIGHT,                  // normal, left to right
    TXT_DOWN,                   // rotated 90 degrees clockwise, top to bottom
    TXT_LEFT,                   // upside down, right to left
    TXT_UP                      // rotated 90 degrees anticlockwise, bottom to top
} txt_dir_t;

// Fonts (and the 2-bit title) into the VRAM of frame 1, those read from ROM
// left out: call it again after a scene that drew into frame 1
void     txt_upload(void);
// The 2-bit title expanded once into an 8 bpp image at VRAM_SPARE (TITLE_W
// pitch, value 0 outside the ink, ramp at 'colour')
void     txt_title_image(uint8_t colour);

// A string at pixel address 'dst' (its first glyph's first visited pixel).
// Value v of a glyph is written as colour + v - 1; 0 is skipped, or written
// as 'bg' with opaque. Returns the number of characters.
unsigned txt_draw(const font_t *f, uint32_t dst, const char *s, uint8_t colour,
                  uint8_t bg, bool opaque, txt_dir_t dir);
// Pixel width of a string drawn left to right
unsigned txt_width(const font_t *f, const char *s);

static inline uint32_t txt_at(uint32_t frame, unsigned x, unsigned y)
{
    return frame + y * SCR_W + x;
}

// Sine, 256 steps per turn, Q14
extern int16_t sin_q14[256];
void    sin_init(void);
static inline int32_t isin(unsigned a) { return sin_q14[a & 255u]; }
static inline int32_t icos(unsigned a) { return sin_q14[(a + 64u) & 255u]; }

#endif // TXT_H
