//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      s3m.h  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Tracker modules: Scream Tracker 3 S3M (up to S3M_CH_MAX sample channels),
// loaded into memory areas and played by the tracker mixer (trk.h). The
// samples are kept at 8 bits (a 16-bit sample's upper byte).
//----------------------------------------------------------------------------
#ifndef S3M_H
#define S3M_H

#include <stdint.h>
#include "trk.h"

#define S3M_CH_MAX     16u

// Load a module of size bytes into the areas (tried in order for each
// part; the file's parts are read where they lie, through seek) and start
// it in the mixer. 0 when it can be played.
int s3m_load(trk_read_t rd, trk_seek_t seek, uint32_t size, const trk_area_t *areas, unsigned n_areas,
             trk_info_t *info);

#endif
