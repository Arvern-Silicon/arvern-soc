//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      flac.h  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// FLAC decoder: mono or stereo, 8 to 24 bits, blocks of up to FLAC_BLOCK_MAX
// samples (every rate up to 48 kHz in the format's subset). The stream is
// pulled through a bit reader from the caller's read function, so a frame
// never has to fit in memory; a frame's samples go to the player's ring as
// 16-bit stereo frames.
//----------------------------------------------------------------------------
#ifndef FLAC_H
#define FLAC_H

#include <stdint.h>
#include "codec.h"

#define FLAC_BLOCK_MAX  4608u

// The stream's next bytes into dst (at most n): the bytes read, 0 at its end
typedef unsigned (*flac_read_t)(uint8_t *dst, unsigned n);
// Skip n bytes of the stream: 0 when done
typedef int      (*flac_skip_t)(uint32_t n);

typedef struct {
    uint32_t rate;
    unsigned channels, bps;
    unsigned block_max;
    uint32_t frames;                        // in the stream, 0 when unknown
} flac_info_t;

// The stream's header and metadata: the stream info, and TITLE / ARTIST
// from the Vorbis comment (into title / artist, at most text_max - 1
// characters; left as they are without one). buf (size bytes) is the bit
// reader's buffer from now on. 0 when the stream can be played.
int flac_open(flac_read_t rd, flac_skip_t skip, uint8_t *buf, unsigned size,
              flac_info_t *info, char *title, char *artist, unsigned text_max);

// The decoder's sample buffers, free for other use between FLAC tracks
void *flac_buffers(uint32_t *size);

// The next frame into the ring (which has room for block_max frames): the
// frames written, 0 at the stream's end, < 0 on an error
int flac_frame(sink_t *s);

#endif
