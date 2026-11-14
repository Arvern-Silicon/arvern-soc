//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      codec.h  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The decoders: a file's bytes to stereo frames in the player's ring
// ([15:0] left, [31:16] right; mono on both). Each decodes whole units only
// (a PCM frame, an ADPCM block, a QOA frame) and returns the bytes it used;
// the caller keeps the rest for the next call.
//
//   PCM        8 or 16 bits, mono or stereo (WAV format 1)
//   IMA ADPCM  4 bits, mono or stereo, in blocks (WAV format 0x11): each
//              block starts with each channel's sample and step index
//   QOA        "Quite OK Audio", 3.2 bits a sample: frames of up to 256
//              slices of 20 samples a channel, a sign-sign LMS predictor
//----------------------------------------------------------------------------
#ifndef CODEC_H
#define CODEC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { CODEC_PCM, CODEC_ADPCM, CODEC_QOA, CODEC_FLAC, CODEC_MOD, CODEC_MP3 } codec_fmt_t;   // FLAC: flac.c, MOD: a tracker module (trk.c), MP3: mp3.c

typedef struct {
    codec_fmt_t fmt;
    unsigned    channels;                   // 1 or 2
    unsigned    bits;                       // PCM: 8 or 16
    unsigned    block;                      // ADPCM: bytes a block
} codec_t;

// Each frame is published as soon as it is decoded: the reader (the
// player's interrupt) plays it while the rest of the read is decoded
typedef struct {
    volatile uint32_t *ring;
    uint32_t           mask;                // ring size - 1 (a power of two)
    volatile uint32_t *w;                   // next frame written (free-running)
} sink_t;

extern codec_t codec;

// Decode the whole units in p[0..n); last: the end of the data, where a
// short ADPCM block is decoded too. Returns the bytes used.
unsigned codec_run(const uint8_t *p, unsigned n, bool last, sink_t *s);

// The most frames n bytes can give (the ring room a read needs)
unsigned codec_frames_max(unsigned n);

// Frames in an ADPCM block of n bytes
unsigned codec_adpcm_frames(unsigned n);

// The largest unit, in bytes (the read buffer holds one more than a read)
#define CODEC_UNIT_MAX  (8u + 2u * 16u + 256u * 2u * 8u)

#endif
