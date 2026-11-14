//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      ray.h       (DE0-Nano-SoC Belenos raycaster demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The raycaster: map, player, rendering of one frame.
//----------------------------------------------------------------------------
#ifndef RAY_H
#define RAY_H

#include <stdint.h>
#include "demo_kit.h"

// Angles: 1024 steps per turn. Fixed point: positions in 16.16 map units.
#define ANG_NR           1024u
#define ANG_MASK         (ANG_NR - 1u)
#define ONE              65536

typedef struct {
    int32_t  x, y;                             // position, 16.16
    unsigned a;                                // heading, 0..1023
} player_t;

extern int16_t sin_q14[ANG_NR];               // filled by ray_init()
static inline int32_t rsin(unsigned a) { return sin_q14[a & ANG_MASK]; }
static inline int32_t rcos(unsigned a) { return sin_q14[(a + ANG_NR / 4u) & ANG_MASK]; }

void     ray_init(void);
// After demo_init(): the textures move into the VRAM between the two frames
void     ray_upload(void);
// Palette animation for time t (glowing circuits); pal_show() displays it
void     ray_animate(unsigned t);
// Draw the view from p into the frame at 'frame', the horizon 'bob' lines
// below the middle; returns the wall distance straight ahead (16.16) and,
// through left / right, those a quarter screen aside
int32_t  ray_frame(uint32_t frame, const player_t *p, int bob, int32_t *left, int32_t *right);
// Floating logos over the frame just drawn (same view), at time t
void     ray_sprites(uint32_t frame, const player_t *p, int bob, unsigned t);
// Minimap in the top-right corner
void     ray_minimap(uint32_t frame, const player_t *p);
// 1 when the map cell under (x, y) (16.16) is a wall
int      ray_wall(int32_t x, int32_t y);

#endif // RAY_H
