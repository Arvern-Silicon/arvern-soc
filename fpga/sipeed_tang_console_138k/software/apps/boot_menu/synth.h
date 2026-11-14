//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      synth.h  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The menu's chiptune synthesizer: four voices (a 25 % pulse lead, a
// triangle bass, a noise drum, a square for the sound effects), integer only,
// and the boot chime: eight sine voices and a filtered noise of their own.
// synth_render() runs in the audio interrupt; the main loop only posts
// requests (synth_music, synth_sfx), which the renderer takes at its next
// control tick.
//----------------------------------------------------------------------------
#ifndef SYNTH_H
#define SYNTH_H

#include <stdbool.h>
#include <stdint.h>
#include "tang_board.h"

// Rendering rate: hclk / (2 x MDIV x RATIO), 22.04 kHz; the I2S stream runs
// at twice that, 44.08 kHz (an HDMI audio rate), each sample sent twice
#define SYNTH_MDIV   2u
#define SYNTH_RATIO  384u
#define SYNTH_FS     (BOARD_HCLK_HZ / (2u * SYNTH_MDIV * SYNTH_RATIO))

typedef enum { SFX_MOVE, SFX_RUN, SFX_ERROR, SFX_PLUG, SFX_UNPLUG } sfx_t;

// n stereo words ([15:0] left, [31:16] right, the same sample)
void synth_render(uint32_t *out, unsigned n);

void synth_music(bool on);                  // the tune, from its start
void synth_chime(bool on);                  // the boot chime from its start, or faded out
void synth_sfx(sfx_t s);                    // replaces the effect playing
bool synth_sfx_busy(void);
void synth_volume(unsigned v);              // 0 (silent) .. SYNTH_VOL_MAX, everything
#define SYNTH_VOL_MAX  8u

#endif // SYNTH_H
