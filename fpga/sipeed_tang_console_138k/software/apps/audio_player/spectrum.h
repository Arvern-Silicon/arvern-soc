//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      spectrum.h  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The spectrum of the output stream: a 512-point fixed-point FFT (Hann
// window) grouped into log-spaced bands from 40 Hz to 16 kHz, each a level
// over 66 dB.
//----------------------------------------------------------------------------
#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stdint.h>

#define SPEC_N      512u                    // samples per transform
#define SPEC_BANDS  32u

// The band edges for a stream at fs Hz
void spectrum_init(uint32_t fs);

// x: SPEC_N samples, the oldest first; level: SPEC_BANDS values, 0 (-66 dB
// or below) to 255 (full scale)
void spectrum_run(const int16_t *x, uint8_t *level);

#endif
