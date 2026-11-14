//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      xm.c  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// FastTracker 2 XM: a header (title, the counts, the order list, the
// initial speed and tempo, linear or Amiga frequencies), the patterns
// (packed: a cell's note, instrument, volume column, effect and parameter,
// each present or not), the instruments (a sample per note, volume and
// panning envelopes, fadeout, auto-vibrato), each followed by its samples'
// headers and data (8 or 16 bits, delta-coded; forward or ping-pong loops,
// a ping-pong loop is unrolled at load). A row lasts 'speed' ticks, a tick
// 2.5 / tempo seconds.
//
// Pitch, linear: period = 7680 - 64 x note - finetune / 2, played at
// 8363 x 2^((4608 - period) / 768) Hz. Amiga: period = 6848 x
// 2^((48 - note) / 12 - finetune / 1536), played at 8363 x 6848 / period
// Hz. Slides and vibrato move the period.
//
// Sequencer: the effects 0-F (as ProTracker's, 8xx full-range panning),
// G / H (global volume, slide), K (key off), L (envelope position), P
// (panning slide), R (retrigger + volume change), T (tremor), X1 / X2
// (extra fine portamento), and the volume column (volume, slides, fine
// slides, vibrato speed and depth, panning, panning slides, tone
// portamento); each effect keeps its own parameter memory. A key off
// releases the envelopes' sustain and starts the fadeout (cuts the note
// without a volume envelope). The song ends at its last order or when it
// comes back to a row it played (outside a pattern loop): played once. A
// dry run of the sequencer at load gives its length.
//
// Mixing (trk.c): a voice's panning 0 .. 255 to the mixer's 0 .. 16.
//----------------------------------------------------------------------------
#include <string.h>
#include "xm.h"

#define ORD_MAX     256u
#define PAT_MAX     256u
#define INS_MAX     128u
#define SMP_MAX     256u                    // all the instruments' samples
#define ENV_PTS     12u
#define NOTE_OFF    97u
#define STEP_LIN    3181584u                // (8363 << 24) / TRK_RATE
#define STEP_AMI    85107374u               // 8363 x 6848 x 65536 / TRK_RATE
#define FADE_MAX    32768

typedef struct {
    const int8_t *data;                     // NULL: no sample
    uint32_t      len, loop_start, loop_len;  // samples; looped when loop_len > 0
    uint8_t       volume, pan;
    int8_t        fine, rel;
} smp_t;

typedef struct {
    uint16_t x;
    uint8_t  y;
} env_pt_t;

typedef struct {
    env_pt_t pt[ENV_PTS];
    uint8_t  n, sus, loop_s, loop_e, type;  // type: 1 on, 2 sustain, 4 loop
} env_t;

typedef struct {
    uint8_t  map[96];                       // note -> sample (an index into smp[], 0xFF: none)
    env_t    vol, pan;
    uint16_t fadeout;
    uint8_t  vib_type, vib_sweep, vib_depth, vib_rate;
} ins_t;

typedef struct {
    uint8_t note, ins, vol, eff, par;
} cell_t;

typedef struct {
    const ins_t *in;
    const smp_t *s;
    int32_t  period, target, out_period;
    int32_t  vol, out_vol;
    int32_t  pan, out_pan;                  // 0 .. 255
    int32_t  fade;                          // FADE_MAX .. 0
    uint16_t venv_pos, penv_pos;
    uint8_t  released, note, eff, par, vcol;
    uint8_t  mem[42];                       // each effect's last parameter (36 .. 41: E1 E2 EA EB X1 X2)
    uint8_t  porta, vib_speed, vib_depth, vib_pos, vib_wave;
    uint8_t  trem_speed, trem_depth, trem_pos, trem_wave;
    uint8_t  tremor, retrig, offset, loop_row, loop_count;
    uint16_t av_pos, av_amp;                // auto-vibrato
    cell_t   delayed;                       // EDx: the cell waiting for its tick
} ch_t;

static smp_t        *smp;                   // in the module's memory
static unsigned      n_smp;
static ins_t        *ins;
static unsigned      n_ins;
static uint8_t       order[ORD_MAX];
static unsigned      n_orders;
static const uint8_t **pat, **pat_end;     // in the module's memory, as the tables below
static uint16_t     *pat_rows;
static unsigned      n_pat, nch, linear;
static uint8_t      *visited;               // n_orders x 32 bytes (256 rows)
static ch_t         *chn;
static trk_voice_t   voice[XM_CH_MAX];
static trk_song_t    song;
static unsigned      speed0, tempo0;

static unsigned speed, tempo, gvol, tick, row, ord, pdelay, pdelay_left, repeat;
static unsigned jump_ord, break_row, loop_row_to;     // the row's B, D, E6 (~0u: none)
static unsigned ended;
static uint32_t tick_left, tick_rem;        // the tick's frames, the remainder carried
static cell_t   cells[XM_CH_MAX];
static const uint8_t *row_p;
static unsigned row_pat, row_next;

static const uint32_t semi_pow[12] = {
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715
};
static const uint32_t fine_pow[64] = {
    65536, 65595, 65654, 65714, 65773, 65832, 65892, 65951, 66011, 66071, 66130, 66190, 66250, 66309, 66369, 66429,
    66489, 66549, 66609, 66670, 66730, 66790, 66850, 66911, 66971, 67032, 67092, 67153, 67213, 67274, 67335, 67395,
    67456, 67517, 67578, 67639, 67700, 67761, 67823, 67884, 67945, 68007, 68068, 68129, 68191, 68252, 68314, 68376,
    68438, 68499, 68561, 68623, 68685, 68747, 68809, 68871, 68933, 68996, 69058, 69120, 69183, 69245, 69308, 69370
};
static const uint8_t vib_sine[32] = {
      0,  24,  49,  74,  97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120,  97,  74,  49,  24
};

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

// 2^(x / 768) in Q16 for x in -4608 .. 4607
static uint32_t pow2_768(int32_t x)
{
    int32_t oct = x >> 6;                   // in semitones first
    const uint32_t f = (uint32_t) x & 63u;
    int32_t o = (oct >= 0) ? oct / 12 : -((11 - oct) / 12);
    const uint32_t s = (uint32_t) (oct - o * 12);
    const uint32_t m = (uint32_t) (((uint64_t) semi_pow[s] * fine_pow[f]) >> 16);   // [1, 2) in Q16
    return (o >= 0) ? m << o : m >> -o;
}

// A note's period (note 0 = C-0, after the sample's relative note)
static int32_t note_period(int32_t note, int32_t fine)
{
    note = clampi(note, 0, 118);
    if (linear)
        return 7680 - note * 64 - fine / 2;
    return (int32_t) ((6848u * pow2_768((48 - note) * 64 - fine / 2)) >> 16);
}

static uint32_t period_step(int32_t p)
{
    if (p <= 0)
        return 0u;
    if (linear) {
        const int32_t x = clampi(4608 - p, -4608, 4607);
        return (uint32_t) (((uint64_t) STEP_LIN * pow2_768(x)) >> 24);
    }
    return STEP_AMI / (uint32_t) p;
}

static int32_t wave(uint8_t w, uint8_t pos)
{
    const uint8_t p = pos & 63u;
    switch (w & 3u) {
    case 1:  return (p < 32u) ? 255 - (int32_t) p * 8 : -(int32_t) (p - 32u) * 8;  // ramp down
    case 2:  return (p < 32u) ? 255 : -255;                                        // square
    default: return (p < 32u) ? vib_sine[p] : -(int32_t) vib_sine[p - 32u];
    }
}

// An effect's parameter, its memory when 0
static uint8_t param(ch_t *c, unsigned eff, uint8_t par)
{
    if (par)
        c->mem[eff] = par;
    return c->mem[eff];
}

static void trigger(ch_t *c, uint32_t offset)
{
    trk_voice_t *v = &voice[c - chn];
    const smp_t *s = c->s;
    v->on = 0;
    if (!s || !s->data)
        return;
    if (offset >= s->len)
        return;
    v->d = s->data;
    v->pos = offset;
    v->frac = 0;
    if (s->loop_len) {
        v->end = s->loop_start + s->loop_len;
        v->loop_len = s->loop_len;
    } else {
        v->end = s->len;
        v->loop_len = 0;
    }
    v->on = (v->pos < v->end);
    if (!(c->vib_wave & 4u)) c->vib_pos = 0;
    if (!(c->trem_wave & 4u)) c->trem_pos = 0;
    c->retrig = 0;
    c->tremor = 0;
}

// The instrument's defaults: volume, panning, envelopes, fadeout
static void ins_reset(ch_t *c)
{
    if (c->s) {
        c->vol = c->s->volume;
        c->pan = c->s->pan;
    }
    c->venv_pos = c->penv_pos = 0;
    c->fade = FADE_MAX;
    c->released = 0;
    c->av_pos = c->av_amp = 0;
}

static void vol_slide(ch_t *c, uint8_t p)
{
    if (p >> 4) c->vol += p >> 4;
    else        c->vol -= p & 15u;
    c->vol = clampi(c->vol, 0, 64);
}

static void tone_porta(ch_t *c)
{
    if (!c->target || !c->period)
        return;
    const int32_t d = (int32_t) c->porta * 4;
    if (c->period < c->target)
        c->period = (c->period + d > c->target) ? c->target : c->period + d;
    else if (c->period > c->target)
        c->period = (c->period - d < c->target) ? c->target : c->period - d;
}

static void porta(ch_t *c, int32_t d)
{
    if (c->period)
        c->period = clampi(c->period + d, 1, 32000);
}

static void vibrato(ch_t *c)
{
    c->out_period = c->period + ((wave(c->vib_wave, c->vib_pos) * c->vib_depth) >> 5);
    c->vib_pos = (uint8_t) (c->vib_pos + c->vib_speed);
}

//==========================================================
// THE PATTERNS
//==========================================================

// The cells of the current order's row; a pattern cut short ends in empty
// rows
static void read_row(void)
{
    const unsigned p = order[ord];
    for (unsigned k = 0; k < nch; k++)
        cells[k] = (cell_t) { 0, 0, 0, 0, 0 };
    if (p >= n_pat || !pat[p])
        return;
    if (row_pat != p || row_next != row || !row_p) {
        row_pat = p;
        row_p = pat[p];
        row_next = 0;
    }
    const uint8_t *const end = pat_end[p];
    while (row_next <= row) {
        for (unsigned k = 0; k < nch; k++) {
            cell_t c = { 0, 0, 0, 0, 0 };
            if (row_p < end) {
                uint8_t f = *row_p;
                if (f & 0x80u) row_p++;
                else           f = 0x1F;
                const unsigned n = (unsigned) __builtin_popcount(f & 0x1Fu);
                if ((unsigned) (end - row_p) < n) {
                    row_p = end;
                } else {
                    if (f & 1u)  c.note = *row_p++;
                    if (f & 2u)  c.ins  = *row_p++;
                    if (f & 4u)  c.vol  = *row_p++;
                    if (f & 8u)  c.eff  = *row_p++;
                    if (f & 16u) c.par  = *row_p++;
                }
            }
            if (row_next == row)
                cells[k] = c;
        }
        row_next++;
    }
}

//==========================================================
// THE SEQUENCER
//==========================================================

// A cell's note and instrument (tick 0, or EDx's tick)
static void cell_note(ch_t *c, const cell_t *e)
{
    const unsigned porta_note = (e->eff == 3u || e->eff == 5u || (e->vol >> 4) == 0xFu);
    if (e->ins && e->ins <= n_ins)
        c->in = &ins[e->ins - 1u];
    if (e->note == NOTE_OFF) {
        c->released = 1;
        if (!c->in || !(c->in->vol.type & 1u))
            c->vol = 0;
    } else if (e->note && e->note < NOTE_OFF) {
        if (c->in && !porta_note) {
            const uint8_t k = c->in->map[e->note - 1u];
            c->s = (k < n_smp) ? &smp[k] : 0;
        }
        if (c->s) {
            const int32_t p = note_period((int32_t) e->note - 1 + c->s->rel, c->s->fine);
            c->note = e->note;
            if (porta_note && c->period) {
                c->target = p;
            } else {
                c->period = p;
                c->target = 0;
                trigger(c, (e->eff == 9u) ? (uint32_t) c->offset << 8 : 0u);
            }
        }
    }
    if (e->ins && c->s)
        ins_reset(c);
    if (e->vol >= 0x10u && e->vol <= 0x50u)
        c->vol = e->vol - 0x10u;
    else if ((e->vol >> 4) == 0xCu)
        c->pan = (e->vol & 15u) * 16;
}

static void row_start(void)
{
    if (visited[ord * 32u + (row >> 3)] & (1u << (row & 7u))) {
        unsigned looping = 0;
        for (unsigned k = 0; k < nch; k++)
            looping |= chn[k].loop_count;
        if (!looping) {
            ended = 1;
            return;
        }
    }
    visited[ord * 32u + (row >> 3)] |= (uint8_t) (1u << (row & 7u));

    read_row();
    for (unsigned k = 0; k < nch; k++) {
        ch_t *c = &chn[k];
        const cell_t *e = &cells[k];
        const unsigned eff = (e->eff < 36u) ? e->eff : 0u;
        uint8_t par = e->par;
        c->eff = (uint8_t) eff;
        c->vcol = e->vol;
        if (eff == 9u && par)
            c->offset = par;
        if (eff == 0xEu && (par >> 4) == 0xDu && (par & 15u)) {
            c->delayed = *e;                    // the note waits for tick y
            c->par = par;
            continue;
        }
        c->delayed.note = 0;
        cell_note(c, e);
        // volume column, tick 0
        const unsigned vx = e->vol >> 4, vy = e->vol & 15u;
        switch (vx) {
        case 0x8: c->vol = clampi(c->vol - (int32_t) vy, 0, 64); break;
        case 0x9: c->vol = clampi(c->vol + (int32_t) vy, 0, 64); break;
        case 0xA: c->vib_speed = (uint8_t) (vy << 2); break;
        case 0xB: if (vy) c->vib_depth = (uint8_t) vy; break;
        case 0xF: if (vy) c->porta = (uint8_t) (vy << 4); break;
        default: break;
        }
        // effects, tick 0
        switch (eff) {
        case 0x0: break;
        case 0x1: case 0x2: case 0xA: case 0x5: case 0x6: case 0x11: case 0x19: case 0x1B:
            par = param(c, eff, par); break;
        case 0x3: if (par) c->porta = par; break;
        case 0x4: if (par >> 4) c->vib_speed = par >> 4; if (par & 15u) c->vib_depth = par & 15u; break;
        case 0x7: if (par >> 4) c->trem_speed = par >> 4; if (par & 15u) c->trem_depth = par & 15u; break;
        case 0x8: c->pan = par; break;
        case 0xB: jump_ord = par; break;
        case 0xC: c->vol = (par > 64u) ? 64 : par; break;
        case 0xD: break_row = (par >> 4) * 10u + (par & 15u); break;
        case 0xE: {
            const unsigned x = par >> 4, y = par & 15u;
            switch (x) {
            case 0x1: porta(c, -(int32_t) (param(c, 36u, (uint8_t) y) & 15u) * 4); break;
            case 0x2: porta(c, (int32_t) (param(c, 37u, (uint8_t) y) & 15u) * 4); break;
            case 0x4: c->vib_wave = (uint8_t) y; break;
            case 0x5: if (c->s) c->period = note_period((int32_t) c->note - 1 + c->s->rel, (int32_t) (y * 16u) - 128); break;
            case 0x6:
                if (!y) {
                    c->loop_row = (uint8_t) row;
                } else {
                    c->loop_count = c->loop_count ? (uint8_t) (c->loop_count - 1u) : (uint8_t) y;
                    if (c->loop_count)
                        loop_row_to = c->loop_row;
                }
                break;
            case 0x7: c->trem_wave = (uint8_t) y; break;
            case 0xA: c->vol = clampi(c->vol + (int32_t) (param(c, 38u, (uint8_t) y) & 15u), 0, 64); break;
            case 0xB: c->vol = clampi(c->vol - (int32_t) (param(c, 39u, (uint8_t) y) & 15u), 0, 64); break;
            case 0xC: if (!y) c->vol = 0; break;
            case 0xE: if (!pdelay_left) pdelay = y; break;
            default: break;
            }
            break;
        }
        case 0xF:
            if (par && par < 32u) speed = par;
            else if (par >= 32u) tempo = par;
            break;
        case 0x10: gvol = (par > 64u) ? 64u : par; break;                   // G
        case 0x14: if (!par) { c->released = 1; if (!c->in || !(c->in->vol.type & 1u)) c->vol = 0; } break;  // K00
        case 0x15: c->venv_pos = c->penv_pos = par; break;                  // L
        case 0x1D: par = param(c, eff, par); break;                         // T
        case 0x21: {                                                        // X1y / X2y
            const unsigned x = par >> 4, y = par & 15u;
            if (x == 1u)      porta(c, -(int32_t) (param(c, 40u, (uint8_t) y) & 15u));
            else if (x == 2u) porta(c, (int32_t) (param(c, 41u, (uint8_t) y) & 15u));
            break;
        }
        default: break;
        }
        c->par = par;
        if (break_row != ~0u && break_row >= 256u) break_row = 0;
    }
}

// The volume column and the effects on the later ticks
static void tick_effects(unsigned t)
{
    for (unsigned k = 0; k < nch; k++) {
        ch_t *c = &chn[k];
        const uint8_t par = c->par, x = par >> 4, y = par & 15u;
        const unsigned vx = c->vcol >> 4, vy = c->vcol & 15u;
        switch (vx) {
        case 0x6: c->vol = clampi(c->vol - (int32_t) vy, 0, 64); break;
        case 0x7: c->vol = clampi(c->vol + (int32_t) vy, 0, 64); break;
        case 0xD: c->pan = clampi(c->pan - (int32_t) vy, 0, 255); break;
        case 0xE: c->pan = clampi(c->pan + (int32_t) vy, 0, 255); break;
        case 0xF: tone_porta(c); break;
        default: break;
        }
        switch (c->eff) {
        case 0x1: porta(c, -(int32_t) par * 4); break;
        case 0x2: porta(c, (int32_t) par * 4); break;
        case 0x3: tone_porta(c); break;
        case 0x5: tone_porta(c); vol_slide(c, par); break;
        case 0x6: vol_slide(c, par); break;
        case 0xA: vol_slide(c, par); break;
        case 0xE:
            if (x == 0x9u && y && (t % y) == 0u)
                trigger(c, 0);
            else if (x == 0xCu && t == y)
                c->vol = 0;
            else if (x == 0xDu && t == y)
                cell_note(c, &c->delayed);
            break;
        case 0x11:                                                          // H: global volume slide
            if (x) gvol = (gvol + x > 64u) ? 64u : gvol + x;
            else   gvol = (gvol > y) ? gvol - y : 0u;
            break;
        case 0x14: if (t == par) { c->released = 1; if (!c->in || !(c->in->vol.type & 1u)) c->vol = 0; } break;
        case 0x19:                                                          // P: panning slide
            if (x) c->pan = clampi(c->pan + (int32_t) x, 0, 255);
            else   c->pan = clampi(c->pan - (int32_t) y, 0, 255);
            break;
        case 0x1B:                                                          // R: retrigger + volume
            if (y && ++c->retrig >= y) {
                static const int8_t add[16] = { 0, -1, -2, -4, -8, -16, 0, 0, 0, 1, 2, 4, 8, 16, 0, 0 };
                if (x == 6u)        c->vol = c->vol * 2 / 3;
                else if (x == 7u)   c->vol >>= 1;
                else if (x == 0xEu) c->vol = c->vol * 3 / 2;
                else if (x == 0xFu) c->vol <<= 1;
                else                c->vol += add[x];
                c->vol = clampi(c->vol, 0, 64);
                trigger(c, 0);
            }
            break;
        default: break;
        }
    }
}

// An envelope's value at pos (the points' y, interpolated), then pos on
static int32_t env_step(const env_t *e, uint16_t *pos, unsigned released, int32_t idle)
{
    if (!(e->type & 1u) || e->n < 1u)
        return idle;
    const unsigned n = (e->n > ENV_PTS) ? ENV_PTS : e->n;
    uint16_t p = *pos;
    unsigned i = 0;
    while (i + 1u < n && e->pt[i + 1u].x <= p)
        i++;
    int32_t v = e->pt[i].y;
    if (i + 1u < n && e->pt[i + 1u].x > e->pt[i].x)
        v += ((int32_t) e->pt[i + 1u].y - e->pt[i].y) * (int32_t) (p - e->pt[i].x)
             / (int32_t) (e->pt[i + 1u].x - e->pt[i].x);
    // on: held at the sustain point while the key is down, the loop
    if ((e->type & 2u) && !released && e->sus < n && p == e->pt[e->sus].x)
        return v;
    p++;
    if ((e->type & 4u) && e->loop_e < n && e->loop_s <= e->loop_e && p >= e->pt[e->loop_e].x)
        p = e->pt[e->loop_s].x;
    if (p > e->pt[n - 1u].x)
        p = e->pt[n - 1u].x;
    *pos = p;
    return v;
}

// The voices' step, volume and panning: tremolo, vibrato, arpeggio,
// tremor, the envelopes, the fadeout, the auto-vibrato
static void voices(unsigned t)
{
    for (unsigned k = 0; k < nch; k++) {
        ch_t *c = &chn[k];
        trk_voice_t *o = &voice[k];
        const uint8_t par = c->par, x = par >> 4, y = par & 15u;
        int32_t period = c->period, vol = c->vol;
        switch (c->eff) {
        case 0x0:
            if (par && period) {
                const unsigned n = (t % 3u == 1u) ? x : (t % 3u == 2u) ? y : 0u;
                period = linear ? period - (int32_t) n * 64
                                : (int32_t) (((uint32_t) period * (uint32_t) pow2_768(-(int32_t) n * 64)) >> 16);
            }
            break;
        case 0x4: case 0x6:
            if (t) { vibrato(c); period = c->out_period; }
            break;
        case 0x7:
            vol += (wave(c->trem_wave, c->trem_pos) * c->trem_depth) >> 6;
            if (t) c->trem_pos = (uint8_t) (c->trem_pos + c->trem_speed);
            break;
        case 0x1D: {                                                        // T: on x + 1 ticks, off y + 1
            const unsigned on = x + 1u, off = y + 1u;
            if (c->tremor >= on) vol = 0;
            if (++c->tremor >= on + off) c->tremor = 0;
            break;
        }
        default: break;
        }
        if ((c->vcol >> 4) == 0xBu && t)                                    // volume-column vibrato
            { vibrato(c); period = c->out_period; }
        const ins_t *in = c->in;
        int32_t ev = 64, ep = 32;
        if (in) {
            ev = env_step(&in->vol, &c->venv_pos, c->released, 64);
            ep = env_step(&in->pan, &c->penv_pos, c->released, 32);
            if (c->released) {
                c->fade -= in->fadeout;
                if (c->fade < 0) c->fade = 0;
            }
            if (in->vib_depth && in->vib_rate) {                            // auto-vibrato
                const uint32_t full = (uint32_t) in->vib_depth << 8;
                c->av_amp = (uint16_t) ((!in->vib_sweep || c->av_amp + full / in->vib_sweep >= full) ? full
                                        : c->av_amp + full / in->vib_sweep);
                period += (wave(in->vib_type == 1u ? 2u : in->vib_type == 2u ? 1u : 0u, (uint8_t) (c->av_pos >> 2))
                           * (int32_t) (c->av_amp >> 8)) >> 6;
                c->av_pos = (uint16_t) (c->av_pos + in->vib_rate);
            }
        }
        vol = clampi(vol, 0, 64);
        // volume x envelope x fadeout x global volume: 0 .. 64
        const int32_t v = (int32_t) ((((uint32_t) vol * (uint32_t) clampi(ev, 0, 64) * (uint32_t) (c->fade >> 7)) >> 14)
                                     * gvol >> 6);
        const int32_t pan = clampi(c->pan + ((clampi(ep, 0, 64) - 32) * (128 - ((c->pan > 128) ? c->pan - 128 : 128 - c->pan))) / 32, 0, 255);
        o->step = period_step(period);
        o->gv = v;
        o->pan = (uint8_t) ((pan * TRK_PAN_MAX + 127) / 255);
    }
}

// One tick of the song, then on to the next
static void song_tick(void)
{
    if (tick == 0u && !repeat)
        row_start();
    else
        tick_effects(tick);
    if (ended) {
        tick_left = 0;
        return;
    }
    voices(tick);
    const uint32_t num = TRK_RATE * 5u + tick_rem;
    tick_left = num / (2u * tempo);
    tick_rem = num % (2u * tempo);
    if (++tick < speed)
        return;
    tick = 0;
    if (pdelay) {
        pdelay_left = pdelay;
        pdelay = 0;
    }
    if (pdelay_left) {
        pdelay_left--;
        repeat = 1;
        return;
    }
    repeat = 0;
    const unsigned from = ord;
    const unsigned rows = (order[ord] < n_pat) ? pat_rows[order[ord]] : 64u;
    if (loop_row_to != ~0u) {
        row = loop_row_to;
    } else if (jump_ord != ~0u || break_row != ~0u) {
        ord = (jump_ord != ~0u) ? jump_ord : ord + 1u;
        row = (break_row != ~0u) ? break_row : 0u;
    } else if (++row >= rows) {
        row = 0;
        ord++;
    }
    jump_ord = break_row = loop_row_to = ~0u;
    if (ord != from)
        for (unsigned k = 0; k < nch; k++)
            chn[k].loop_row = 0;
    if (ord >= n_orders)
        ended = 1;
    else if (row >= ((order[ord] < n_pat) ? pat_rows[order[ord]] : 64u))
        row = 0;
}

static uint32_t xm_tick(void)
{
    if (ended)
        return 0u;
    song_tick();
    return tick_left;
}

static void song_reset(void)
{
    memset(chn, 0, nch * sizeof(ch_t));
    memset(voice, 0, sizeof(voice));
    for (unsigned k = 0; k < nch; k++) {
        chn[k].pan = 128;
        chn[k].fade = FADE_MAX;
    }
    memset(visited, 0, n_orders * 32u);
    speed = speed0;
    tempo = tempo0;
    gvol = 64;
    tick = row = ord = 0;
    pdelay = pdelay_left = repeat = 0;
    jump_ord = break_row = loop_row_to = ~0u;
    ended = (n_orders == 0u);
    tick_left = tick_rem = 0;
    row_p = 0;
}

//==========================================================
// LOADING
//==========================================================

static uint32_t le16(const uint8_t *p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }

static trk_read_t rd_fn;
static uint32_t   rd_left;

static int rd(uint8_t *d, uint32_t n)
{
    if (n > rd_left || rd_fn(d, n) != n)
        return 1;
    rd_left -= n;
    return 0;
}

static int skip(uint32_t n)
{
    uint8_t b[64];
    while (n) {
        const uint32_t k = (n > sizeof(b)) ? sizeof(b) : n;
        if (rd(b, k))
            return 1;
        n -= k;
    }
    return 0;
}

static void env_load(env_t *e, const uint8_t *pts, unsigned n, const uint8_t *idx, uint8_t type)
{
    e->n = (uint8_t) ((n > ENV_PTS) ? ENV_PTS : n);
    for (unsigned i = 0; i < ENV_PTS; i++) {
        e->pt[i].x = (uint16_t) le16(pts + i * 4u);
        e->pt[i].y = (uint8_t) ((le16(pts + i * 4u + 2u) > 64u) ? 64u : le16(pts + i * 4u + 2u));
    }
    e->sus = idx[0];
    e->loop_s = idx[1];
    e->loop_e = idx[2];
    e->type = type;
}

int xm_load(trk_read_t read, uint32_t size, const trk_area_t *areas, unsigned n_areas, trk_info_t *info)
{
    uint8_t h[80];
    rd_fn = read;
    rd_left = size;
    trk_areas(areas, n_areas);
    if (rd(h, 60) || memcmp(h, "Extended Module: ", 17))
        return -1;
    memcpy(info->title, h + 17, 20);
    info->title[20] = 0;
    if (rd(h, 20))
        return -1;
    const uint32_t hs = le32(h), songlen = le16(h + 4);
    nch = le16(h + 8);
    n_pat = le16(h + 10);
    n_ins = le16(h + 12);
    linear = le16(h + 14) & 1u;
    speed0 = le16(h + 16) ? le16(h + 16) : 6u;
    tempo0 = (le16(h + 18) >= 32u) ? le16(h + 18) : 125u;
    if (!nch || nch > XM_CH_MAX || !songlen || songlen > ORD_MAX || n_pat > PAT_MAX || n_ins > INS_MAX || hs < 20u + 256u)
        return -1;
    n_orders = songlen;
    if (rd(order, 256) || skip(hs - 20u - 256u))
        return -1;
    visited = trk_take(n_orders * 32u);
    pat = (const uint8_t **) (void *) trk_take(2u * PAT_MAX * sizeof(uint8_t *));
    pat_rows = (uint16_t *) (void *) trk_take(PAT_MAX * sizeof(uint16_t));
    chn = (ch_t *) (void *) trk_take(nch * sizeof(ch_t));
    if (!visited || !pat || !pat_rows || !chn)
        return -1;
    pat_end = pat + PAT_MAX;

    // the patterns, packed
    memset(pat, 0, 2u * PAT_MAX * sizeof(uint8_t *));
    for (unsigned k = 0; k < n_pat; k++) {
        if (rd(h, 9))
            return -1;
        const uint32_t hl = le32(h), rows = le16(h + 5), ps = le16(h + 7);
        if (hl < 9u || skip(hl - 9u))
            return -1;
        pat_rows[k] = (uint16_t) ((rows && rows <= 256u) ? rows : 64u);
        if (!ps)
            continue;
        uint8_t *p = trk_take(ps);
        if (!p || rd(p, ps))
            return -1;
        pat[k] = p;
        pat_end[k] = p + ps;
    }

    // the instruments and their samples (8 bits signed)
    ins = (ins_t *) (void *) trk_take(n_ins * sizeof(ins_t));
    smp = (smp_t *) (void *) trk_take(SMP_MAX * sizeof(smp_t));
    if ((n_ins && !ins) || !smp)
        return -1;
    memset(ins, 0, n_ins * sizeof(ins_t));
    n_smp = 0;
    for (unsigned k = 0; k < n_ins; k++) {
        static uint8_t ih[263];
        ins_t *in = &ins[k];
        memset(in->map, 0xFF, sizeof(in->map));
        if (rd(ih, 4))
            return -1;
        const uint32_t isz = le32(ih);
        if (isz < 29u)
            return -1;
        const uint32_t got = (isz > sizeof(ih)) ? sizeof(ih) : isz;
        if (rd(ih + 4, got - 4u) || skip(isz - got))
            return -1;
        const uint32_t ns = le16(ih + 27);
        if (!ns)
            continue;
        if (got < 241u)
            return -1;
        const uint32_t shs = le32(ih + 29);
        if (shs < 40u || n_smp + ns > SMP_MAX)
            return -1;
        for (unsigned i = 0; i < 96u; i++)
            in->map[i] = (uint8_t) ((ih[33 + i] < ns) ? n_smp + ih[33 + i] : 0xFFu);
        env_load(&in->vol, ih + 129, ih[225], ih + 227, ih[233]);
        env_load(&in->pan, ih + 177, ih[226], ih + 230, ih[234]);
        in->vib_type = ih[235];
        in->vib_sweep = ih[236];
        in->vib_depth = ih[237];
        in->vib_rate = ih[238];
        in->fadeout = (uint16_t) le16(ih + 239);
        // the sample headers, then the data
        uint32_t bytes[16];
        uint8_t  types[16];
        if (ns > 16u)
            return -1;
        for (unsigned i = 0; i < ns; i++) {
            uint8_t sh[40];
            if (rd(sh, 40) || skip(shs - 40u))
                return -1;
            smp_t *s = &smp[n_smp + i];
            memset(s, 0, sizeof(*s));
            const unsigned w = (sh[14] & 16u) ? 2u : 1u;
            bytes[i] = le32(sh);
            types[i] = sh[14];
            s->len = le32(sh) / w;
            s->loop_start = le32(sh + 4) / w;
            s->loop_len = ((sh[14] & 3u) && s->loop_start < s->len) ? le32(sh + 8) / w : 0u;
            if (s->loop_start + s->loop_len > s->len)
                s->loop_len = s->len - s->loop_start;
            s->volume = (sh[12] > 64u) ? 64u : sh[12];
            s->fine = (int8_t) sh[13];
            s->pan = sh[15];
            s->rel = (int8_t) sh[16];
        }
        for (unsigned i = 0; i < ns; i++) {
            smp_t *s = &smp[n_smp + i];
            const unsigned w = (types[i] & 16u) ? 2u : 1u, ping = ((types[i] & 3u) == 2u) && s->loop_len > 1u;
            if (!s->len) {
                if (skip(bytes[i]))
                    return -1;
                continue;
            }
            uint32_t len = s->loop_len ? s->loop_start + s->loop_len : s->len;    // nothing plays after the loop
            const uint32_t keep = len;
            if (ping)
                len += s->loop_len - 2u;                                            // the loop backwards
            int8_t *d = (int8_t *) trk_take(len + 1u);
            if (!d)
                return -1;
            // delta-coded: the running sum's (upper) byte
            uint8_t b[64];
            int32_t acc = 0;
            uint32_t out = 0, left = bytes[i];
            while (left) {
                uint32_t n = (left > sizeof(b)) ? sizeof(b) : left;
                n -= n % w;
                if (!n) { if (skip(left)) return -1; break; }
                if (rd(b, n))
                    return -1;
                for (uint32_t j = 0; j < n; j += w) {
                    if (w == 2u) {
                        acc = (int16_t) (acc + (int16_t) le16(b + j));
                        if (out < keep) d[out++] = (int8_t) (acc >> 8);
                    } else {
                        acc = (int8_t) (acc + (int8_t) b[j]);
                        if (out < keep) d[out++] = (int8_t) acc;
                    }
                }
                left -= n;
            }
            while (out < keep)
                d[out++] = 0;
            if (ping) {
                for (uint32_t j = 0; j < s->loop_len - 2u; j++)
                    d[keep + j] = d[keep - 2u - j];
                s->loop_len = 2u * s->loop_len - 2u;
            }
            s->len = s->loop_len ? s->loop_start + s->loop_len : keep;
            d[s->len] = s->loop_len ? d[s->loop_start] : 0;                         // the interpolation's next byte
            s->data = d;
        }
        n_smp += ns;
    }

    song.v = voice;
    song.n_voices = nch;
    song.mono = 0;
    song.shift = (nch <= 4u) ? 11u : (nch <= 16u) ? 12u : 13u;
    song.tick = xm_tick;

    // the song's length, by a dry run (at most an hour)
    song_reset();
    uint32_t frames = 0;
    while (!ended && frames < TRK_RATE * 3600u) {
        song_tick();
        frames += tick_left;
    }
    info->frames = frames;
    info->channels = nch;
    song_reset();
    trk_start(&song);
    return 0;
}
