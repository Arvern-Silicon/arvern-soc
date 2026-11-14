//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      intro.h  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The boot animation (intro.c). It draws over the whole frame and changes
// pal[] (background, trace and ramp entries): the caller redraws and
// rebuilds the palette after it. skip() is polled once a frame.
//----------------------------------------------------------------------------
#ifndef INTRO_H
#define INTRO_H

#include <stdbool.h>

void intro(bool (*skip)(void));

#endif // INTRO_H
