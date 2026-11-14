//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      music.h  (Tang Console 138K demo kit)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The demos' music: a small synthesizer playing tunes written as patterns,
// integer only, with no hardware in it (music_i2s.c streams it to the board's
// sound output; a host build renders it to a file).
//
// A tune is up to MUS_CH channels, each an instrument and a list of notes
// (MIDI note, 0 a rest; a length in steps), all looping together at the
// length of the longest. Instruments: pulse (three duties), triangle, saw,
// sine, noise and a kick drum (a sine falling in pitch); an attack, a decay
// to a sustain level, a release when the note ends, optional vibrato and
// detune, and a send to a shared echo. mus_render() makes the samples (in
// the audio interrupt); mus_play() asks for another tune, taken at the
// renderer's next control tick: a tune at the same tempo takes over on the
// next beat (4 steps), at the same place in its first bar (16 steps), the
// notes ringing carried on; another one after the one playing has faded out.
//----------------------------------------------------------------------------
#ifndef MUSIC_H
#define MUSIC_H

#include <stdbool.h>
#include <stdint.h>

#define MUS_CH        6u
#define MUS_TICK      64u                   // samples per control tick

enum { W_PULSE12, W_PULSE25, W_PULSE50, W_TRI, W_SAW, W_SINE, W_NOISE, W_KICK };

typedef struct {
    uint8_t  wave;
    uint8_t  attack;                        // ticks to full volume
    uint8_t  decay;                         // towards the sustain: vol -= (vol - sus) >> decay per tick
    uint8_t  sustain;                       // of 255
    uint8_t  release;                       // once the note ends: vol -= vol >> release per tick
    uint8_t  vibrato;                       // depth: inc >> (12 - vibrato) at about 5 Hz, 0 none
    int8_t   detune;                        // 1/256ths of the note (a second voice an octave up for W_SAW)
    uint8_t  echo;                          // send to the echo, of 255
    int16_t  level;                         // peak amplitude
} mus_inst_t;

typedef struct { uint8_t note, len; } mus_ev_t;       // len 0 ends the list

typedef struct {
    uint16_t          step_ticks;           // control ticks per step (a step: a sixteenth, usually)
    uint8_t           nch;
    const mus_inst_t *inst;                 // per channel
    const mus_ev_t   *ch[MUS_CH];           // per channel
} mus_tune_t;

// The rendering rate: MUS_FS samples a second (set by the build)
#ifndef MUS_FS
#define MUS_FS        22040u
#endif

// n stereo words ([15:0] left, [31:16] right, the same sample)
void mus_render(uint32_t *out, unsigned n);
void mus_play(const mus_tune_t *t);         // 0: silence (the tune playing fades out)
void mus_volume(unsigned v);                // 0 .. 8
bool mus_playing(void);

#endif // MUSIC_H
