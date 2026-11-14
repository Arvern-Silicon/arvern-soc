//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      mod.h  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Tracker modules: ProTracker MOD (31 samples, 4 channels; the 6- and
// 8-channel and "xxCH" variants), loaded into memory areas and played by
// the tracker mixer (trk.h).
//----------------------------------------------------------------------------
#ifndef MOD_H
#define MOD_H

#include <stdint.h>
#include "trk.h"

#define MOD_CH_MAX     8u

// Load a module of size bytes into the areas (tried in order for each
// part) and start it in the mixer. 0 when it can be played.
int mod_load(trk_read_t rd, uint32_t size, const trk_area_t *areas, unsigned n_areas, trk_info_t *info);

#endif
