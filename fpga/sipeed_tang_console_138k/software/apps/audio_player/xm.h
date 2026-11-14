//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      xm.h  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Tracker modules: FastTracker 2 XM (up to XM_CH_MAX channels), loaded into
// memory areas and played by the tracker mixer (trk.h). The samples are
// kept at 8 bits (a 16-bit sample's upper byte).
//----------------------------------------------------------------------------
#ifndef XM_H
#define XM_H

#include <stdint.h>
#include "trk.h"

#define XM_CH_MAX      32u

// Load a module of size bytes into the areas (tried in order for each
// part) and start it in the mixer. 0 when it can be played.
int xm_load(trk_read_t rd, uint32_t size, const trk_area_t *areas, unsigned n_areas, trk_info_t *info);

#endif
