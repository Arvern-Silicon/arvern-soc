//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      synth.c  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Chiptune synthesizer of the boot menu (see synth.h).
//
// Voices: phase accumulators (Q32 cycles per sample) and an exponential
// envelope (Q16) updated every TICK samples, the control tick that also
// clocks the sequencer and the sound effects.
//
// The tune: an original eight-bar loop in A minor (Am F C G Am F G E), a
// lead melody of notes and lengths in sixteenths, a bass of octave jumps on
// the bar's root in eighths, hats and snares on a sixteen-step pattern.
//
// The boot chime, an original in E: a detuned low pad glides up a fourth
// while a band of noise sweeps open, glass tones climb into the reverb, an
// Eadd9 chord blooms over a sub as the logo's traces power up, sparkles
// follow and the reverb rings out. Its voices are sines (a parabola per
// half period), some with an octave or a twelfth added, with an attack ramp
// before the exponential decay; a state-variable filter shapes the noise;
// a Schroeder-Moorer reverb (four damped combs, two all-passes) carries
// it all. The tune starts once the chime has rung out.
//----------------------------------------------------------------------------
#include "synth.h"

#define TICK         64u                    // samples per control tick (2.9 ms)
#ifdef SIMULATION
#define STEP_TICKS   2u                     // a fast tempo: several bars in a short run
#else
#define STEP_TICKS   40u                    // one sixteenth: 118 ms (127 bpm)
#endif
#define STEPS        128u                   // eight bars of sixteen steps

// Peak amplitudes (the mix of all voices stays below 16-bit full scale)
#define LVL_LEAD     1700
#define LVL_BASS     3000
#define LVL_HAT      700
#define LVL_SNARE    1300
#define LVL_SFX      3600
#define LVL_ERROR    2600

enum { V_LEAD, V_BASS, V_NOISE, V_SFX, VOICES };

typedef struct {
    uint32_t phase, inc;
    uint32_t vol;                           // envelope, Q16
    int32_t  amp;                           // vol x level: the sample amplitude
    int32_t  level;
    uint32_t decay;                         // vol -= vol >> decay per tick
} voice_t;

static voice_t  voice[VOICES];
static uint32_t lfsr = 1u, sub;
static volatile uint32_t gain = 6u;         // master volume, 0 .. SYNTH_VOL_MAX

//==========================================================
// PITCH
//==========================================================

// Phase increments of MIDI notes 120..131; an octave lower is a shift right
#define INC(f256)    ((uint32_t) (((uint64_t) (f256) << 24) / SYNTH_FS))   // f256: Hz x 256
static const uint32_t top_inc[12] = {
    INC(2143237u), INC(2270680u), INC(2405702u), INC(2548752u), INC(2700309u), INC(2860878u),
    INC(3030994u), INC(3211227u), INC(3402176u), INC(3604480u), INC(3818814u), INC(4045892u),
};

static void note_on(unsigned v, unsigned midi, int32_t level, uint32_t decay)
{
    voice[v].phase = 0u;
    voice[v].inc   = top_inc[midi % 12u] >> (10u - midi / 12u);
    voice[v].vol   = 0xFFFFu;
    voice[v].level = level;
    voice[v].decay = decay;
}

static void noise_on(unsigned kind)
{
    voice[V_NOISE].inc   = (kind == 1u) ? 0xFFFFFFFFu : 0x60000000u;  // hat: a new bit every sample
    voice[V_NOISE].vol   = 0xFFFFu;
    voice[V_NOISE].level = (kind == 1u) ? LVL_HAT : LVL_SNARE;
    voice[V_NOISE].decay = (kind == 1u) ? 2u : 4u;
}

//==========================================================
// THE TUNE
//==========================================================

enum {
    R = 0, E2 = 40, F2 = 41, G2 = 43, A2 = 45, C3 = 48, E3 = 52,
    A4 = 69, B4 = 71, C5 = 72, D5 = 74, E5 = 76, F5 = 77, G5 = 79, Gs5 = 80, A5 = 81, B5 = 83,
    G4 = 67, C6 = 84, D6 = 86, E6 = 88, G6 = 91, B6 = 95,
};

typedef struct { uint8_t note, len; } ev_t;

static const ev_t lead[] = {
    {A4,2},{C5,2},{E5,2},{A5,4},{G5,2},{E5,4},                  // Am
    {F5,2},{E5,2},{C5,2},{A4,4},{C5,2},{F5,4},                  // F
    {E5,2},{G5,2},{C6,4},{B5,2},{G5,2},{E5,4},                  // C
    {D5,2},{G5,2},{B5,2},{D6,4},{B5,2},{G5,4},                  // G
    {A5,3},{G5,1},{E5,2},{C5,2},{E5,2},{A5,2},{B5,2},{C6,2},    // Am
    {A5,4},{G5,2},{F5,2},{E5,2},{F5,2},{A5,4},                  // F
    {G5,2},{B5,2},{D6,2},{B5,2},{G5,2},{D5,2},{G5,4},           // G
    {Gs5,4},{B5,4},{E5,4},{R,4},                                // E
};
#define LEAD_NR      (sizeof(lead) / sizeof(lead[0]))

static const uint8_t bass_root[8] = { A2, F2, C3, G2, A2, F2, G2, E2 };
static const uint8_t bass_step[8] = { 0, 12, 0, 12, 0, 12, 7, 12 };    // per eighth
static const uint8_t drums[2][16] = {                                   // 1 hat, 2 snare
    { 0, 0, 1, 0, 2, 0, 1, 0, 0, 0, 1, 0, 2, 0, 1, 1 },
    { 0, 0, 1, 0, 2, 0, 1, 0, 2, 0, 2, 0, 2, 2, 2, 2 },                 // the last bar
};

static volatile bool music_req;
static bool          music_on;
static unsigned      step, step_t, lead_i, lead_wait;

static void seq_step(void)
{
    const unsigned bar = step / 16u, s = step % 16u;

    if (lead_wait == 0u && lead_i < LEAD_NR) {
        const ev_t *e = &lead[lead_i++];
        if (e->note != R)
            note_on(V_LEAD, e->note, LVL_LEAD, 6u);
        lead_wait = e->len;
    }
    if (lead_wait)
        lead_wait--;
    if ((s & 1u) == 0u)
        note_on(V_BASS, bass_root[bar] + bass_step[s / 2u], LVL_BASS, 6u);
    if (drums[bar == 7u][s])
        noise_on(drums[bar == 7u][s]);
    if (++step == STEPS) {
        step = 0u;
        lead_i = 0u;
        lead_wait = 0u;
    }
}

//==========================================================
// SOUND EFFECTS: notes of a number of ticks on the effect voice
//==========================================================

static const ev_t sfx_move[]  = { {E6,4}, {B6,6}, {R,0} };
static const ev_t sfx_run[]   = { {C5,8}, {E5,8}, {G5,8}, {C6,50}, {R,0} };
static const ev_t sfx_error[] = { {E3,30}, {C3,50}, {R,0} };
static const ev_t sfx_plug[]  = { {G5,6}, {D6,6}, {G6,14}, {R,0} };
static const ev_t sfx_unplug[] = { {G5,6}, {D5,6}, {G4,14}, {R,0} };
static const ev_t *const sfx_list[] = { sfx_move, sfx_run, sfx_error, sfx_plug, sfx_unplug };
static const uint8_t sfx_decay[] = { 4u, 4u, 5u, 4u, 4u };

static volatile uint32_t sfx_req;           // effect + 1, from the main loop
static volatile bool     sfx_on;
static const ev_t       *sfx_ev;
static unsigned          sfx_id, sfx_wait;

static void sfx_next(void)
{
    if (sfx_ev->note == R) {
        sfx_on = false;
        return;
    }
    note_on(V_SFX, sfx_ev->note, (sfx_id == SFX_ERROR) ? LVL_ERROR : LVL_SFX, sfx_decay[sfx_id]);
    sfx_wait = sfx_ev->len;
    sfx_ev++;
}

//==========================================================
// THE BOOT CHIME
//==========================================================

#define CV           11u                    // tone voices
#define CV_NOISE     CV                     // the noise
#define CV_ALL       (CV + 1u)              // an event for every voice: their decay
#define MS(ms)       ((uint16_t) (((ms) * (SYNTH_FS / 100u) + 320u) / 640u))   // control ticks
#define FQ(hz)       ((int32_t) (((hz) * 205887u) / SYNTH_FS))                  // 2 pi fc / fs, Q15
#define DET          8u                     // detune: inc / 256, 7 cents

enum { E1 = 28, B3 = 59, E4 = 64, Gs4 = 68, Fs5 = 78, Fs6 = 90, Gs6 = 92,
       E7 = 100, Fs7 = 102, Gs7 = 104 };
enum { W_SINE, W_WARM, W_GLASS };           // sine; + its octave; + its twelfth

typedef struct {
    uint32_t phase, inc, inc_to;
    uint32_t vol;                           // envelope, Q16
    uint32_t att;                           // vol += att per tick up to full, then the decay
    int32_t  amp, level;
    uint8_t  decay, glide, wave;
} cvoice_t;

// At 'ms', voice v plays 'note' (from 'from', 0: none, gliding with a time
// constant of 2^glide ticks), detuned by det x 7 cents, reaching 'level' in
// 'att' ms, then halving every 0.7 x 2^decay ticks (without a glide the
// pitch is set at once). The noise's band-pass
// sweeps from 'from' to 'note' x 50 Hz. CV_ALL releases every voice.
typedef struct {
    uint16_t ms;
    uint8_t  v, note, from;
    int8_t   det;
    uint8_t  wave;
    uint16_t level, att;
    uint8_t  decay, glide;
} cev_t;

static const cev_t chime[] = {
    // the swell: a sub, a low pad rising an octave, a breath of noise opening
    {    0, 10, E1,  0,  0, W_SINE,   3200, 2000,  9, 0 },
    {    0,  0, E2,  E1, -1, W_WARM,  2600, 2000,  9, 8 },
    {    0,  1, E2,  E1,  1, W_WARM,  2600, 2000,  9, 8 },
    {  300,  2, E3,  E2, -1, W_WARM,  1100, 2000,  9, 8 },
    {  300,  3, E3,  E2,  1, W_WARM,  1100, 2000,  9, 8 },
    {    0, CV_NOISE, 50, 2, 0, 0,    5500, 1900,  8, 9 },
    // crystal: a climb into the reverb
    {  900,  8, E6,  0,  0, W_GLASS,   650,    0,  6, 0 },
    { 1020,  9, Fs6, 0,  0, W_GLASS,   650,    0,  6, 0 },
    { 1140,  8, Gs6, 0,  0, W_GLASS,   700,    0,  6, 0 },
    { 1260,  9, B6,  0,  0, W_GLASS,   700,    0,  6, 0 },
    { 1380,  8, E7,  0,  0, W_GLASS,   750,    0,  6, 0 },
    { 1500,  9, Fs7, 0,  0, W_GLASS,   750,    0,  6, 0 },
    { 1620,  8, Gs7, 0,  0, W_GLASS,   800,    0,  6, 0 },
    { 1780,  9, B6,  0,  0, W_GLASS,   650,    0,  7, 0 },
    // the bloom: Eadd9 opens over the sub and a wash of air
    { 2330, 10, E1,  0,  0, W_SINE,   5200,   40, 10, 0 },
    { 2330,  0, E2,  0, -1, W_WARM,   2600,  200, 10, 6 },
    { 2330,  1, E2,  0,  1, W_WARM,   2600,  200, 10, 6 },
    { 2330,  2, E3,  0, -1, W_WARM,   1800,  260, 10, 6 },
    { 2330,  3, E3,  0,  1, W_WARM,   1800,  260, 10, 6 },
    { 2330,  4, B3,  0, -1, W_WARM,   1500,  300, 10, 0 },
    { 2330,  5, E4,  0,  1, W_SINE,   1600,  300, 10, 0 },
    { 2330,  6, Gs4, 0, -1, W_SINE,   1300,  340, 10, 0 },
    { 2330,  7, Fs5, 0,  1, W_SINE,    900,  400, 10, 0 },
    { 2330,  8, E6,  0,  0, W_GLASS,  1200,    0,  7, 0 },
    { 2330, CV_NOISE, 36, 60, 0, 0,   2600,   20,  7, 7 },
    // sparkles after the light pulses
    { 2700,  9, B6,  0,  0, W_GLASS,   650,    0,  7, 0 },
    { 3100,  8, Gs6, 0,  0, W_GLASS,   550,    0,  7, 0 },
    { 3500,  9, E7,  0,  0, W_GLASS,   500,    0,  7, 0 },
    { 3950,  8, Fs6, 0,  0, W_GLASS,   450,    0,  7, 0 },
    { 4400,  9, B6,  0,  0, W_GLASS,   400,    0,  7, 0 },
    { 4900,  8, E7,  0,  0, W_GLASS,   350,    0,  7, 0 },
    { 5500, CV_ALL, 0, 0, 0, 0,          0,    0,  8, 0 },
};
#define CHIME_NR     (sizeof(chime) / sizeof(chime[0]))

// The reverb: four damped feedback combs into two all-pass diffusers
// (Schroeder-Moorer), about 2.3 s to fade by 60 dB
#define RV_C0        557u
#define RV_C1        593u
#define RV_C2        641u
#define RV_C3        677u
#define RV_A0        277u
#define RV_A1        211u
#define RV_FB        30147                  // comb feedback, Q15 (0.92)
#define RV_DAMP      13107                  // comb low-pass, Q15 (0.4)

static int16_t   rv_buf[RV_C0 + RV_C1 + RV_C2 + RV_C3 + RV_A0 + RV_A1];
static const uint16_t rv_len[6] = { RV_C0, RV_C1, RV_C2, RV_C3, RV_A0, RV_A1 };
static int16_t  *rv_line[6];
static unsigned  rv_pos[6];
static int32_t   rv_lp[4];

static cvoice_t          cv[CV + 1u];
static volatile uint32_t chime_req;         // 1 start, 2 fade out, from the main loop
static volatile bool     chime_on;
static unsigned          chime_t, chime_i;
static uint32_t          cmaster;           // output gain, Q16
static uint32_t          cm_decay;          // 0: steady, else its fade
static uint32_t          clfsr = 1u;
static int32_t           nz_f, nz_f_to, nz_lo, nz_bp;

static inline int16_t sat16(int32_t v)
{
    return (int16_t) ((v > 32767) ? 32767 : (v < -32767) ? -32767 : v);
}

static void chime_start(void)
{
    int16_t *p = rv_buf;
    for (unsigned k = 0; k < 6u; k++) {
        rv_line[k] = p;
        rv_pos[k]  = 0u;
        p += rv_len[k];
    }
    for (unsigned k = 0; k < sizeof(rv_buf) / sizeof(rv_buf[0]); k++)
        rv_buf[k] = 0;
    for (unsigned k = 0; k < 4u; k++)
        rv_lp[k] = 0;
    for (unsigned v = 0; v <= CV; v++) {
        cv[v].vol = 0u;
        cv[v].att = 0u;
        cv[v].amp = 0;
    }
    nz_lo = nz_bp = 0;
    chime_t  = 0u;
    chime_i  = 0u;
    cmaster  = 0xFFFFu;
    cm_decay = 0u;
    chime_on = true;
}

static void chime_tick(void)
{
    const uint32_t r = chime_req;
    if (r) {
        chime_req = 0u;
        if (r == 1u)
            chime_start();
        else if (chime_on) {
            chime_i  = CHIME_NR;
            cm_decay = 4u;
        }
    }
    if (!chime_on)
        return;

    for (; chime_i < CHIME_NR && MS(chime[chime_i].ms) <= chime_t; chime_i++) {
        const cev_t   *e = &chime[chime_i];
        cvoice_t      *p = &cv[e->v];
        const uint32_t n = MS(e->att);
        if (e->v == CV_ALL) {
            for (unsigned v = 0; v <= CV; v++) {
                cv[v].att   = 0u;
                cv[v].decay = e->decay;
            }
            continue;
        }
        if (e->v == CV_NOISE) {
            nz_f_to = FQ(e->note * 50u);
            nz_f    = FQ(e->from * 50u);
        } else {
            uint32_t inc = top_inc[e->note % 12u] >> (10u - e->note / 12u);
            if (e->det)
                inc = (e->det > 0) ? inc + (inc >> DET) : inc - (inc >> DET);
            p->inc_to = inc;
            if (e->from)
                inc = top_inc[e->from % 12u] >> (10u - e->from / 12u);
            if (e->from || e->glide == 0u || p->vol == 0u)
                p->inc = inc;
            p->wave = e->wave;
        }
        p->level = e->level;
        p->decay = e->decay;
        p->glide = e->glide;
        p->att   = n ? 0xFFFFu / n : 0u;
        if (!n)
            p->vol = 0xFFFFu;
    }

    bool live = false;
    for (unsigned v = 0; v <= CV; v++) {
        cvoice_t *p = &cv[v];
        if (p->att) {
            p->vol += p->att;
            if (p->vol >= 0xFFFFu) {
                p->vol = 0xFFFFu;
                p->att = 0u;
            }
        } else {
            p->vol -= p->vol >> p->decay;
            if (p->vol < 1024u)             // the slow decays stall below 2^decay
                p->vol = 0u;
        }
        if (v < CV && p->glide)
            p->inc += (uint32_t) ((int32_t) (p->inc_to - p->inc) >> p->glide);
        p->amp = (int32_t) ((((p->vol * (uint32_t) p->level) >> 16) * gain) >> 3);
        live |= (p->vol != 0u);
    }
    if (cv[CV_NOISE].glide)
        nz_f += (nz_f_to - nz_f) >> cv[CV_NOISE].glide;

    // once every voice is done the reverb rings out
    if (!live && chime_i == CHIME_NR && !cm_decay)
        cm_decay = 7u;
    if (cm_decay) {
        cmaster -= (cmaster >> cm_decay) + 1u;
        if (cmaster < 64u || cmaster > 0xFFFFu)
            chime_on = false;
    }
    chime_t++;
}

static inline int32_t para(uint32_t phase)  // a sine, Q14
{
    const int32_t x = (int32_t) phase >> 17;
    return (x * (16384 - ((x < 0) ? -x : x))) >> 12;
}

static int32_t chime_sample(void)
{
    int32_t dry = 0;
    for (unsigned v = 0; v < CV; v++) {
        cvoice_t *p = &cv[v];
        if (p->amp == 0)
            continue;
        p->phase += p->inc;
        int32_t y = para(p->phase);
        if (p->wave == W_WARM)
            y += para(p->phase << 1) >> 2;
        else if (p->wave == W_GLASS)
            y += para(p->phase * 3u) >> 1;
        dry += (y * p->amp) >> 14;
    }
    if (cv[CV_NOISE].amp) {                 // a state-variable band-pass, Q 2
        clfsr = (clfsr >> 1) ^ ((uint32_t) -(int32_t) (clfsr & 1u) & 0xB400u);
        const int32_t in = (clfsr & 1u) ? 8192 : -8192;
        nz_lo += (nz_f * nz_bp) >> 15;
        const int32_t hi = in - nz_lo - (nz_bp >> 1);
        nz_bp += (nz_f * hi) >> 15;
        dry += (nz_bp * cv[CV_NOISE].amp) >> 14;
    }

    int32_t in = dry >> 3, wet = 0;
    for (unsigned k = 0; k < 4u; k++) {
        int16_t *d = &rv_line[k][rv_pos[k]];
        rv_lp[k] += ((*d - rv_lp[k]) * (32768 - RV_DAMP)) >> 15;
        wet += *d;
        *d = sat16(in + ((rv_lp[k] * RV_FB) >> 15));
        if (++rv_pos[k] == rv_len[k])
            rv_pos[k] = 0u;
    }
    wet >>= 1;
    for (unsigned k = 4u; k < 6u; k++) {
        int16_t *d = &rv_line[k][rv_pos[k]];
        const int32_t b = *d;
        *d = sat16(wet + (b >> 1));
        wet = b - wet;
        if (++rv_pos[k] == rv_len[k])
            rv_pos[k] = 0u;
    }
    return (int32_t) ((((dry >> 1) + wet) * 3 * (int32_t) (cmaster >> 1)) >> 16);
}

//==========================================================
// CONTROL TICK AND RENDERING
//==========================================================

static void tick(void)
{
    const uint32_t r = sfx_req;
    if (r) {
        sfx_req = 0u;
        sfx_id  = r - 1u;
        sfx_ev  = sfx_list[sfx_id];
        sfx_on  = true;
        sfx_next();
    } else if (sfx_on && --sfx_wait == 0u) {
        sfx_next();
    }

    if (music_req != music_on && !(music_req && chime_on)) {   // the tune waits for the chime
        music_on = music_req;
        step = 0u; step_t = 0u; lead_i = 0u; lead_wait = 0u;
        for (unsigned v = V_LEAD; v <= V_NOISE; v++)
            voice[v].decay = 2u;            // started or stopped: fade what rings
    }
    if (music_on && step_t-- == 0u) {
        step_t = STEP_TICKS - 1u;
        seq_step();
    }
    chime_tick();

    for (unsigned v = 0; v < VOICES; v++) {
        voice_t *p = &voice[v];
        p->vol -= p->vol >> p->decay;
        if (p->vol < 256u)
            p->vol = 0u;
        p->amp = (int32_t) ((((p->vol * (uint32_t) p->level) >> 16) * gain) >> 3);
    }
}

void synth_render(uint32_t *out, unsigned n)
{
    voice_t *const ld = &voice[V_LEAD], *const bs = &voice[V_BASS];
    voice_t *const nz = &voice[V_NOISE], *const fx = &voice[V_SFX];

    for (unsigned i = 0; i < n; i++) {
        int32_t s;
        ld->phase += ld->inc;
        s = (ld->phase < 0x40000000u) ? ld->amp : -ld->amp;
        bs->phase += bs->inc;
        uint32_t t = bs->phase >> 16;
        if (t & 0x8000u)
            t = 0xFFFFu - t;
        s += (((int32_t) t - 16384) * bs->amp) >> 14;
        uint32_t p = nz->phase + nz->inc;
        if (p < nz->phase)
            lfsr = (lfsr >> 1) | (((lfsr ^ (lfsr >> 1)) & 1u) << 14);
        nz->phase = p;
        s += (lfsr & 1u) ? nz->amp : -nz->amp;
        fx->phase += fx->inc;
        s += (fx->phase < 0x80000000u) ? fx->amp : -fx->amp;
        if (chime_on)
            s += chime_sample();
        if (s > 32767)  s = 32767;
        if (s < -32767) s = -32767;
        out[i] = (uint32_t) (uint16_t) s * 0x00010001u;
        if (++sub == TICK) {
            sub = 0u;
            tick();
        }
    }
}

//==========================================================
// REQUESTS FROM THE MAIN LOOP
//==========================================================

void synth_music(bool on) { music_req = on; }
void synth_chime(bool on) { chime_req = on ? 1u : 2u; }
void synth_volume(unsigned v) { gain = (v > SYNTH_VOL_MAX) ? SYNTH_VOL_MAX : v; }
void synth_sfx(sfx_t s)   { sfx_req = (uint32_t) s + 1u; }
bool synth_sfx_busy(void) { return sfx_req != 0u || sfx_on; }
