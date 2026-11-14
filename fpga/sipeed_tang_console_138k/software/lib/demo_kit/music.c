//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      music.c  (Tang Console 138K demo kit)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The demos' synthesizer (see music.h).
//
// Voices: phase accumulators (Q32 cycles per sample) and an envelope (Q16)
// updated every MUS_TICK samples, the control tick that also steps the
// sequencer. The echo is one delay line (int16) fed by every voice's send and
// fed back on itself.
//----------------------------------------------------------------------------
#include <string.h>
#include "music.h"

#define ECHO_LEN     4096u                  // samples: 186 ms
#define ECHO_FB      110                    // feedback, of 256
#define ECHO_MIX     150                    // in the output, of 256

enum { E_OFF, E_ATT, E_DEC, E_REL };

typedef struct {
    uint32_t phase, phase2, inc, inc0;      // phase2: the detuned second saw
    uint32_t vol, sus, att;                 // Q16
    uint32_t gate;                          // ticks until the release
    int32_t  amp;
    uint8_t  st;
    uint32_t vib;                           // vibrato phase
} voice_t;

typedef struct {
    const mus_ev_t *ev;
    unsigned        i, left;                // event, steps left in it
} track_t;

static voice_t                 v[MUS_CH];
static track_t                 trk[MUS_CH];
static const mus_tune_t       *cur;
static const mus_tune_t *volatile req;
static volatile bool           req_new;
static unsigned                step_t, step_n, steps;
static unsigned                fade = 256u;  // master, of 256
static volatile unsigned       gain = 6u;
static uint32_t                lfsr = 1u, sub;
static int16_t                 echo[ECHO_LEN];
static unsigned                echo_i;

// Phase increments of MIDI notes 120..131 at MUS_FS; an octave lower is a shift right
#define INC(f256)    ((uint32_t) (((uint64_t) (f256) << 24) / MUS_FS))   // f256: Hz x 256
static const uint32_t top_inc[12] = {
    INC(2143237u), INC(2270680u), INC(2405702u), INC(2548752u), INC(2700309u), INC(2860878u),
    INC(3030994u), INC(3211227u), INC(3402176u), INC(3604480u), INC(3818814u), INC(4045892u),
};

static uint32_t note_inc(unsigned note)
{
    return top_inc[note % 12u] >> (10u - note / 12u);
}

static void note_on(unsigned c, unsigned note, unsigned len)
{
    const mus_inst_t *in = &cur->inst[c];
    voice_t *p = &v[c];
    uint32_t inc = note_inc(note);

    if (in->detune && in->wave != W_SAW)
        inc += (uint32_t) (((int64_t) inc * in->detune) >> 8);
    p->inc0 = inc;
    p->inc  = (in->wave == W_KICK) ? inc << 3 : inc;    // the kick starts three octaves up
    if (in->wave == W_KICK || in->wave == W_NOISE)
        p->phase = 0u;
    p->sus  = (uint32_t) in->sustain * 257u;
    p->att  = in->attack ? 0xFFFFu / in->attack : 0xFFFFu;
    p->st   = E_ATT;
    if (!in->attack)
        p->vol = 0xFFFFu;
    p->gate = (len * cur->step_ticks * 7u) / 8u;
}

static void play_tune(const mus_tune_t *t, bool keep)
{
    cur = t;
    if (!keep)
        memset(v, 0, sizeof(v));
    memset(trk, 0, sizeof(trk));
    step_t = step_n = steps = 0u;
    if (!t)
        return;
    for (unsigned c = 0; c < t->nch; c++) {
        unsigned n = 0;
        trk[c].ev = t->ch[c];
        for (const mus_ev_t *e = t->ch[c]; e->len; e++)
            n += e->len;
        if (n > steps)
            steps = n;
    }
}

// Into the tune at step 'off' of its first bar: the notes that started
// before it are not played
static void seek(unsigned off)
{
    step_n = off;
    for (unsigned c = 0; c < cur->nch; c++) {
        track_t *t = &trk[c];
        unsigned s = 0;
        while (t->ev[t->i].len && s + t->ev[t->i].len <= off)
            s += t->ev[t->i++].len;
        if (t->ev[t->i].len && s < off)
            t->left = s + t->ev[t->i++].len - off;
    }
}

static void step(void)
{
    if (step_n == steps) {                  // the loop: every channel from its start
        step_n = 0u;
        for (unsigned c = 0; c < cur->nch; c++) {
            trk[c].i = 0u;
            trk[c].left = 0u;
        }
    }
    for (unsigned c = 0; c < cur->nch; c++) {
        track_t *t = &trk[c];
        if (t->left) {
            t->left--;
            continue;
        }
        const mus_ev_t *e = &t->ev[t->i];
        if (!e->len)
            continue;                       // this channel waits for the loop
        if (e->note)
            note_on(c, e->note, e->len);
        else
            v[c].st = (v[c].st == E_OFF) ? E_OFF : E_REL;
        t->left = e->len - 1u;
        t->i++;
    }
    step_n++;
}

static void tick(void)
{
    // A new tune: at the same tempo it takes over on the next beat, at the
    // same place in its bar (the notes ringing go on); else the one playing
    // fades out first
    if (req_new) {
        const mus_tune_t *t = req;
        if (cur && t && t->step_ticks == cur->step_ticks) {
            if (step_t == 0u && (step_n % 4u) == 0u) {
                const unsigned off = step_n % 16u;
                req_new = false;
                play_tune(t, true);
                seek(off);
            }
        } else if (cur && fade > 0u) {
            fade = (fade > 24u) ? fade - 24u : 0u;
        } else {
            req_new = false;
            play_tune(t, false);
            fade = 256u;
        }
    }
    if (!cur)
        return;
    if (step_t-- == 0u) {
        step_t = cur->step_ticks - 1u;
        step();
    }
    for (unsigned c = 0; c < cur->nch; c++) {
        const mus_inst_t *in = &cur->inst[c];
        voice_t *p = &v[c];
        if (p->st != E_OFF && p->st != E_REL && p->gate && --p->gate == 0u)
            p->st = E_REL;
        switch (p->st) {
        case E_ATT:
            p->vol += p->att;
            if (p->vol >= 0xFFFFu) {
                p->vol = 0xFFFFu;
                p->st  = E_DEC;
            }
            break;
        case E_DEC:
            p->vol -= (p->vol - p->sus) >> in->decay;
            break;
        case E_REL:
            p->vol -= p->vol >> in->release;
            if (p->vol < 64u) {
                p->vol = 0u;
                p->st  = E_OFF;
            }
            break;
        default:
            break;
        }
        if (in->wave == W_KICK && p->inc > p->inc0)
            p->inc -= (p->inc - p->inc0) >> 3;
        if (in->vibrato) {                  // about 5 Hz
            p->vib += 0x0068DB8Bu;
            const int32_t x = (int32_t) p->vib >> 17;
            const int32_t s = (x * (16384 - ((x < 0) ? -x : x))) >> 12;     // Q14
            p->inc = p->inc0 + (uint32_t) ((((int64_t) p->inc0 >> (12u - in->vibrato)) * s) >> 14);
        }
        p->amp = (int32_t) ((((p->vol * (uint32_t) in->level) >> 16) * gain * fade) >> 11);
    }
}

static inline int32_t sine(uint32_t phase)  // Q14
{
    const int32_t x = (int32_t) phase >> 17;
    return (x * (16384 - ((x < 0) ? -x : x))) >> 12;
}

void mus_render(uint32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        int32_t s = 0, send = 0;
        if (cur) {
            for (unsigned c = 0; c < cur->nch; c++) {
                voice_t *p = &v[c];
                if (p->amp == 0)
                    continue;
                const mus_inst_t *in = &cur->inst[c];
                int32_t y;                  // Q14
                p->phase += p->inc;
                switch (in->wave) {
                case W_PULSE12: y = (p->phase < 0x20000000u) ? 16384 : -16384; break;
                case W_PULSE25: y = (p->phase < 0x40000000u) ? 16384 : -16384; break;
                case W_PULSE50: y = (p->phase < 0x80000000u) ? 16384 : -16384; break;
                case W_TRI: {
                    uint32_t t = p->phase >> 16;
                    if (t & 0x8000u)
                        t = 0xFFFFu - t;
                    y = (int32_t) t - 16384;
                    break;
                }
                case W_SAW:
                    y = ((int32_t) (p->phase >> 17) - 16384);
                    if (in->detune) {
                        p->phase2 += (p->inc << 1) + (uint32_t) (((int64_t) p->inc * in->detune) >> 7);
                        y = (y + ((int32_t) (p->phase2 >> 17) - 16384) / 2) * 2 / 3;
                    }
                    break;
                case W_NOISE: {
                    uint32_t ph = p->phase;
                    if (ph < p->inc)        // a new bit per period of the note
                        lfsr = (lfsr >> 1) ^ ((uint32_t) -(int32_t) (lfsr & 1u) & 0xB400u);
                    y = (lfsr & 1u) ? 16384 : -16384;
                    break;
                }
                default:                    // W_SINE, W_KICK
                    y = sine(p->phase);
                    break;
                }
                const int32_t o = (y * p->amp) >> 14;
                s += o;
                if (in->echo)
                    send += (o * in->echo) >> 8;
            }
        }
        int32_t e = echo[echo_i];
        int32_t w = send + ((e * ECHO_FB) >> 8);
        echo[echo_i] = (int16_t) ((w > 32767) ? 32767 : (w < -32767) ? -32767 : w);
        echo_i = (echo_i + 1u) & (ECHO_LEN - 1u);
        s += (e * ECHO_MIX) >> 8;
        if (s > 32767)  s = 32767;
        if (s < -32767) s = -32767;
        out[i] = (uint32_t) (uint16_t) s * 0x00010001u;
        if (++sub == MUS_TICK) {
            sub = 0u;
            tick();
        }
    }
}

void mus_play(const mus_tune_t *t)
{
    if (t == cur && !req_new)
        return;                             // already playing: it goes on
    req = t;
    req_new = true;
}
void mus_volume(unsigned g)        { gain = (g > 8u) ? 8u : g; }
bool mus_playing(void)             { return cur != 0; }
