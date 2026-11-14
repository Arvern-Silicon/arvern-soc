//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      scale.h  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The output's frame: RGB565 pixels, w x h (the screen), in the VRAM on the
// board, in memory on a host.
//----------------------------------------------------------------------------
#ifndef SCALE_H
#define SCALE_H

#include <stdint.h>

#define OUT_W_MAX   480u

#if defined(__riscv)
typedef volatile uint16_t fb_t;
#else
typedef uint16_t fb_t;
#endif

void out_target(fb_t *frame, unsigned w, unsigned h);
// Called before every write into the frame (the board's GPU may still be
// drawing into it); NULL: none
void out_sync(void (*sync)(void));
// Where the picture lands on the screen
void out_geometry(unsigned *x, unsigned *y, unsigned *w, unsigned *h);

#endif
