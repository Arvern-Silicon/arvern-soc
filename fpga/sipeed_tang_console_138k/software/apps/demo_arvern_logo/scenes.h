//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      scenes.h  (Tang Console 138K aRVern logo demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
#ifndef SCENES_H
#define SCENES_H


void scene_paint(void);     // 1: gradient, logo layer by layer, wordmark, subtitle
void scene_power(void);     // 2: current pulses along the circuit traces (LUT animation)
void scene_themes(void);    // 3: fades and light / dark crossfade (double-buffered LUT)
void scene_spin(void);      // 4: mirrored and upside-down scan orders
void scene_sprites(void);   // 5: bouncing logos, transparent and XOR GPU copies
void scene_credits(void);   // 6: credits page scrolled in by moving the frame pointer

#endif // SCENES_H
