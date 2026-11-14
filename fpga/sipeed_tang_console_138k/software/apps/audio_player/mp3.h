//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      mp3.h  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// MP3 decoder: MPEG-1 Layer III, 32 / 44.1 / 48 kHz, mono or stereo (joint
// stereo: mid/side and intensity), in fixed point. The stream is pulled
// through the caller's read function; a frame's 1152 samples go to the
// player's ring as 16-bit stereo frames.
//----------------------------------------------------------------------------
#ifndef MP3_H
#define MP3_H

#include <stdint.h>
#include "codec.h"

#define MP3_FRAME       1152u               // samples a frame
#define MP3_MEM         (32u * 1024u + 1024u)   // the decoder's working memory

// The stream's next bytes into dst (at most n): the bytes read, 0 at its end
typedef unsigned (*mp3_read_t)(uint8_t *dst, unsigned n);
// Skip n bytes of the stream: 0 when done
typedef int      (*mp3_skip_t)(uint32_t n);

typedef struct {
    uint32_t rate;
    unsigned channels;
    uint32_t bitrate;                       // the first frame's, bit/s
    uint32_t frames;                        // in the stream (estimated for CBR), 0 when unknown
} mp3_info_t;

// The stream's ID3v2 tag (TIT2 / TPE1 into title / artist, at most
// text_max - 1 characters; left as they are without one), then its first
// frame. mem (MP3_MEM bytes, 4-byte aligned) is the decoder's from now on;
// size is the stream's (for the duration of a constant-bitrate stream).
// 0 when the stream can be played.
int mp3_open(mp3_read_t rd, mp3_skip_t skip, uint32_t size, void *mem, mp3_info_t *info,
             char *title, char *artist, unsigned text_max);

// The next frame into the ring (which has room for MP3_FRAME frames): the
// frames written, 0 at the stream's end, < 0 on an error
int mp3_frame(sink_t *s);

#endif
