//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      screen.h  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The player's screen (480 x 272, 8 bpp, on Belenos through demo_kit): a
// title panel, the spectrum's bars over a glass panel, and a status panel
// with the progress, the time, the CPU load, the sound output and its
// volume, then a help line.
//----------------------------------------------------------------------------
#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>
#include "spectrum.h"

// Belenos, the palette and the panels; the display on
void screen_init(void);

// A new track: its title, artist (may be empty), number (from 1) of n, file
// format ("WAV", ...), sample rate and channels
void screen_track(const char *title, const char *artist, unsigned idx, unsigned n,
                  const char *format, uint32_t rate, unsigned channels);

// A message in place of the bars (an error, the card being read)
void screen_message(const char *line1, const char *line2);

// The sound output's name (4 characters) and the volume (0 to 8), in the
// status panel
void screen_sound(const char *name, unsigned volume);

// The help line at the bottom
void screen_help(const char *line);

// The next frame, drawn just after a refresh started (wait_frames): the bars
// at these levels (from spectrum_run), the progress, the time and the CPU
// load (percent), or "Paused"
void screen_frame(const uint8_t *level, uint32_t elapsed_s, uint32_t total_s, unsigned load, int paused);

#endif
