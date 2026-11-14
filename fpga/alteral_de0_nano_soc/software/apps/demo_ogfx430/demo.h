//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      demo.h  (DE0-Nano-SoC graphic demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
#ifndef DEMO_H
#define DEMO_H
#include "gfx_controller.h"

void demo_16bpp (void);
void demo_8bpp  (void);
void demo_4bpp  (void);
void demo_2bpp  (void);
void demo_1bpp  (void);

void draw_block(uint32_t addr, uint16_t width, uint16_t length, uint16_t color, uint16_t swap_configuration, uint16_t use_gpu);

#endif
