//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      music_i2s.h  (Tang Console 138K demo kit)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The demos' music on the board's sound output (music_i2s.c).
//----------------------------------------------------------------------------
#ifndef MUSIC_I2S_H
#define MUSIC_I2S_H

#include <stdbool.h>

bool music_init(void);                      // false without an I2S master: silent

#endif // MUSIC_I2S_H
