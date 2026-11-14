//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      audio.h  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The menu's sound: the I2S master (apb_i2s) streams the synthesizer
// (synth.c) to the sound output the board I/O selects (HDMI, the onboard
// speaker or a Pmod I2S2), refilled by its TX_LOW interrupt (PLIC source 17).
//
// audio_init() takes over mtvec and the machine external interrupt; without
// an I2S master the menu runs silent. audio_stop() hands the board back as a
// program expects it: the I2S engine stopped with its FIFOs empty, its
// interrupt enables and threshold at their reset values, the sound output as
// selected, the PLIC source disabled at priority 0, mie cleared and mtvec
// restored to its value at boot; interrupts stay off.
//----------------------------------------------------------------------------
#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include "synth.h"

// Debugger view (magic 'AUDO')
typedef struct {
    uint32_t magic;
    uint32_t on;                            // 1 while the stream runs
    uint32_t samples;                       // stereo samples written
    uint32_t underruns;                     // frames sent as zeros for lack of data
    uint32_t irqs;
} audio_status_t;

extern volatile audio_status_t audio;

bool audio_init(void);
void audio_stop(void);

#endif // AUDIO_H
