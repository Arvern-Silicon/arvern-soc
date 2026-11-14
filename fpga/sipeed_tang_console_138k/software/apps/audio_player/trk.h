//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      trk.h  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The tracker modules' common parts (mod.c, s3m.c, xm.c): the loading into
// memory areas, and the mixer.
//
// Loading: a module is loaded whole into memory areas the caller gives
// (patterns and each sample may land in different ones).
//
// Mixer: voices playing signed 8-bit samples at a 16.16 step, linearly
// interpolated, into 16-bit stereo frames at TRK_RATE. A song gives its
// voices and a tick function; the mixer renders each tick's frames.
//
// A voice has a volume and a panning p, 0 .. 16 (left .. right): the side
// gains 16 - p and p (16 and 16 in a mono song). For each block of frames
// the voices at the same panning (all, in a mono song) share a group
// (TRK_GROUPS at most): they
// add their sample times their volume into the group's sums, and each
// frame's group sums are panned once. A voice beyond the groups adds into
// both sides with its two gains. A sample keeps one more byte after its end
// or its loop for the interpolation (0, or the loop's first byte).
//----------------------------------------------------------------------------
#ifndef TRK_H
#define TRK_H

#include <stdint.h>
#include "codec.h"

#define TRK_RATE    44100u                  // mixing rate
#define TRK_GROUPS  8u
#define TRK_PAN_MAX 16u
#define TRK_AREAS   4u

// The module file's next bytes into dst (at most n): the bytes read
typedef unsigned (*trk_read_t)(uint8_t *dst, unsigned n);

// To the module file's byte at offset: 0 when done
typedef int (*trk_seek_t)(uint32_t offset);

// A memory area for the module: base and size in bytes
typedef struct {
    uint8_t *base;
    uint32_t size;
} trk_area_t;

typedef struct {
    unsigned channels;
    uint32_t frames;                        // the song's length at TRK_RATE (played once)
    char     title[29];
} trk_info_t;

typedef struct {
    const int8_t *d;                        // the sample
    uint32_t      pos, end, loop_len, step; // end: of the sample or its loop; step 16.16
    uint16_t      frac;
    int32_t       gv;                       // the volume (0 .. 127)
    uint8_t       pan;                      // 0 .. TRK_PAN_MAX
    uint8_t       on;
} trk_voice_t;

typedef struct {
    trk_voice_t *v;
    unsigned     n_voices;
    unsigned     mono;                      // every voice on both sides
    unsigned     shift;                     // the sums to 16 bits
    uint32_t   (*tick)(void);               // the next tick: its frames, 0 at the song's end
} trk_song_t;

// The memory areas for a module's parts (at most TRK_AREAS), then n bytes
// from the first area that has room (NULL: none has)
void     trk_areas(const trk_area_t *areas, unsigned n);
uint8_t *trk_take(uint32_t n);

// Start a song (its voices off)
void trk_start(const trk_song_t *song);

// The next n frames into the ring (which has room for them): the frames
// written, fewer at the song's end, 0 after it
unsigned trk_render(sink_t *s, unsigned n);

#endif
