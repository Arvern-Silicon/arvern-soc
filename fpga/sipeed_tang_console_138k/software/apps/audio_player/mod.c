//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      mod.c  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// ProTracker MOD: a 1084-byte header (title, 31 sample headers, the song's
// order list, a signature giving the channel count), the patterns (64 rows
// of one 4-byte cell a channel: sample, Amiga period, effect, parameter),
// then the samples (signed 8 bits). A row lasts 'speed' ticks, a tick
// 2.5 / tempo seconds; tick 0 starts the row's notes, every tick runs the
// effects. A period P plays at 3546895 / P Hz (PAL Amiga).
//
// Sequencer: the ProTracker effects (arpeggio, porta up/down, tone porta,
// vibrato, tremolo and their volume-slide forms, panning, sample offset,
// volume slide, position jump, set volume, pattern break, the E commands
// fine slides, waveforms, finetune, pattern loop, retrigger, note cut,
// note delay, pattern delay, and speed / tempo). The song ends at its last
// order, at speed 0, or when it comes back to a row it played (outside a
// pattern loop): played once. A dry run of the sequencer at load gives its
// length.
//
// Mixing (trk.c): the channels are panned left, right, right, left,
// softened (3/4 one side, 1/4 the other: pannings 4 and 12 of 16); 8xx and
// E8x pan to the nearest of the mixer's 17 pannings.
//----------------------------------------------------------------------------
#include <string.h>
#include "mod.h"

#define ROWS        64u
#define PER_MIN     113
#define PER_MAX     856
#define STEP_K      5270838u                // (3546895 / TRK_RATE) << 16

typedef struct {
    const int8_t *data;                     // NULL: no sample
    uint32_t      len, loop_start, loop_len;  // samples; looped when loop_len > 2
    uint8_t       volume;
    int8_t        finetune;
} smp_t;

typedef struct {
    // sequencer
    const smp_t *s;
    int32_t  period, target, out_period;
    int32_t  vol, out_vol;
    int8_t   fine;
    uint8_t  eff, par;
    uint8_t  porta, vib_speed, vib_depth, vib_pos, vib_wave;
    uint8_t  trem_speed, trem_depth, trem_pos, trem_wave;
    uint8_t  offset, loop_row, loop_count, pan;
    uint16_t delay_period;                  // ED: the note waiting for its tick
    const smp_t *delay_s;
} ch_t;

static smp_t    smp[32];
static uint8_t  order[128], n_orders;
static const uint8_t *pat;
static unsigned nch;
static ch_t     chn[MOD_CH_MAX];
static trk_voice_t voice[MOD_CH_MAX];
static trk_song_t  song;

static unsigned speed, tempo, tick, row, ord, pdelay, pdelay_left, repeat;
static unsigned jump_ord, break_row, loop_row_to;     // the row's B, D, E6 (~0u: none)
static unsigned ended;
static uint32_t tick_left, tick_rem;          // the tick's frames, the remainder carried
static uint8_t  visited[128][ROWS / 8u];

static const uint16_t base_period[36] = {
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
};
// 2^(-f / 96) in Q16 for the finetunes f = 0..7, -8..-1 (the 4-bit field)
static const uint32_t fine_mul[16] = {
    65536, 65065, 64596, 64132, 63670, 63212, 62757, 62306,
    69433, 68933, 68438, 67945, 67456, 66971, 66489, 66011
};
static const uint8_t vib_sine[32] = {
      0,  24,  49,  74,  97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120,  97,  74,  49,  24
};

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

static int32_t note_period(unsigned note, int fine)
{
    return (int32_t) ((base_period[note > 35u ? 35u : note] * fine_mul[fine & 15] + 32768u) >> 16);
}

// A period of the file (finetune 0) at a finetune
static int32_t fine_period(uint32_t p, int fine)
{
    return (int32_t) ((p * fine_mul[fine & 15] + 32768u) >> 16);
}

// The note nearest to a period (for arpeggio and finetune)
static unsigned period_note(int32_t p, int fine)
{
    unsigned best = 0;
    int32_t  bd = 0x7FFFFFFF;
    for (unsigned n = 0; n < 36u; n++) {
        const int32_t d = note_period(n, fine) - p;
        const int32_t a = (d < 0) ? -d : d;
        if (a < bd) {
            bd = a;
            best = n;
        }
    }
    return best;
}

static void trigger(ch_t *c, const smp_t *s, uint32_t offset)
{
    trk_voice_t *v = &voice[c - chn];
    v->on = 0;
    if (!s || !s->data || offset >= s->len)
        return;
    v->d = s->data;
    v->pos = offset;
    v->frac = 0;
    if (s->loop_len > 2u) {
        v->end = s->loop_start + s->loop_len;
        v->loop_len = s->loop_len;
    } else {
        v->end = s->len;
        v->loop_len = 0;
    }
    v->on = (v->pos < v->end);
    if (!(c->vib_wave & 4u)) c->vib_pos = 0;
    if (!(c->trem_wave & 4u)) c->trem_pos = 0;
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

static void vol_slide(ch_t *c, uint8_t p)
{
    if (p >> 4)
        c->vol = clampi(c->vol + (p >> 4), 0, 64);
    else
        c->vol = clampi(c->vol - (p & 15), 0, 64);
}

static void tone_porta(ch_t *c)
{
    if (!c->target)
        return;
    if (c->period < c->target)
        c->period = (c->period + c->porta > c->target) ? c->target : c->period + c->porta;
    else if (c->period > c->target)
        c->period = (c->period - c->porta < c->target) ? c->target : c->period - c->porta;
}

static void row_start(void)
{
    if (visited[ord][row >> 3] & (1u << (row & 7u))) {
        unsigned looping = 0;
        for (unsigned k = 0; k < nch; k++)
            looping |= chn[k].loop_count;
        if (!looping) {
            ended = 1;
            return;
        }
    }
    visited[ord][row >> 3] |= (uint8_t) (1u << (row & 7u));

    const uint8_t *cell = pat + ((uint32_t) (order[ord] * ROWS + row) * nch) * 4u;
    for (unsigned k = 0; k < nch; k++, cell += 4) {
        ch_t *c = &chn[k];
        const unsigned sn = (cell[0] & 0xF0u) | (cell[2] >> 4);
        const unsigned per = ((cell[0] & 0x0Fu) << 8) | cell[1];
        const uint8_t eff = cell[2] & 0x0Fu, par = cell[3], x = par >> 4, y = par & 15u;
        c->eff = eff;
        c->par = par;
        c->delay_period = 0;
        c->delay_s = 0;
        const smp_t *s = (sn && sn < 32u) ? &smp[sn] : 0;
        if (eff == 0xE && x == 5u)
            c->fine = (int8_t) ((y & 8u) ? (int) y - 16 : (int) y);
        if (s) {
            c->s = s;
            c->vol = s->volume;
            if (!(eff == 0xE && x == 5u))
                c->fine = s->finetune;
        }
        if (eff == 9 && par)
            c->offset = par;
        if (eff == 0xE && x == 0xD && y) {                  // the note waits for tick y
            c->delay_period = (uint16_t) per;
            c->delay_s = s ? s : c->s;
        } else if (per) {
            const int32_t p = fine_period(per, c->fine);
            if (eff == 3 || eff == 5) {
                c->target = p;
            } else {
                c->period = p;
                c->target = 0;
                trigger(c, c->s, (eff == 9) ? (uint32_t) c->offset << 8 : 0u);
            }
        }
        switch (eff) {
        case 0x3: if (par) c->porta = par; break;
        case 0x4: if (x) c->vib_speed = x; if (y) c->vib_depth = y; break;
        case 0x7: if (x) c->trem_speed = x; if (y) c->trem_depth = y; break;
        case 0x8: c->pan = par; break;
        case 0xB: jump_ord = par; break;
        case 0xC: c->vol = (par > 64u) ? 64 : par; break;
        case 0xD: break_row = (uint32_t) clampi(x * 10 + y, 0, ROWS - 1u); break;
        case 0xE:
            switch (x) {
            case 0x1: c->period = clampi(c->period - y, PER_MIN, PER_MAX); break;
            case 0x2: c->period = clampi(c->period + y, PER_MIN, PER_MAX); break;
            case 0x4: c->vib_wave = y; break;
            case 0x6:
                if (!y) {
                    c->loop_row = (uint8_t) row;
                } else {
                    c->loop_count = c->loop_count ? (uint8_t) (c->loop_count - 1u) : y;
                    if (c->loop_count)
                        loop_row_to = c->loop_row;
                }
                break;
            case 0x7: c->trem_wave = y; break;
            case 0x8: c->pan = (uint8_t) (y * 17u); break;
            case 0xA: c->vol = clampi(c->vol + y, 0, 64); break;
            case 0xB: c->vol = clampi(c->vol - y, 0, 64); break;
            case 0xC: if (!y) c->vol = 0; break;
            case 0xE: if (!pdelay_left) pdelay = y; break;
            default: break;
            }
            break;
        case 0xF:
            if (!par) ended = 1;
            else if (par < 32u) speed = par;
            else tempo = par;
            break;
        default: break;
        }
        c->out_period = c->period;
        c->out_vol = c->vol;
    }
}

static void tick_effects(unsigned t)
{
    for (unsigned k = 0; k < nch; k++) {
        ch_t *c = &chn[k];
        const uint8_t par = c->par, x = par >> 4, y = par & 15u;
        c->out_period = c->period;
        c->out_vol = c->vol;
        switch (c->eff) {
        case 0x0:
            if (par && c->period) {
                const unsigned n = period_note(c->period, c->fine);
                const unsigned a = (t % 3u == 1u) ? x : (t % 3u == 2u) ? y : 0u;
                c->out_period = note_period(n + a, c->fine);
            }
            break;
        case 0x1: c->period = clampi(c->period - par, PER_MIN, PER_MAX); c->out_period = c->period; break;
        case 0x2: c->period = clampi(c->period + par, PER_MIN, PER_MAX); c->out_period = c->period; break;
        case 0x3: tone_porta(c); c->out_period = c->period; break;
        case 0x5: tone_porta(c); c->out_period = c->period; vol_slide(c, par); c->out_vol = c->vol; break;
        case 0x4:
        case 0x6:
            if (c->eff == 6) {
                vol_slide(c, par);
                c->out_vol = c->vol;
            }
            c->out_period = c->period + ((wave(c->vib_wave, c->vib_pos) * c->vib_depth) >> 7);
            c->vib_pos = (uint8_t) (c->vib_pos + c->vib_speed);
            break;
        case 0x7:
            c->out_vol = clampi(c->vol + ((wave(c->trem_wave, c->trem_pos) * c->trem_depth) >> 6), 0, 64);
            c->trem_pos = (uint8_t) (c->trem_pos + c->trem_speed);
            break;
        case 0xA: vol_slide(c, par); c->out_vol = c->vol; break;
        case 0xE:
            if (x == 0x9 && y && (t % y) == 0u)
                trigger(c, c->s, 0);
            else if (x == 0xC && t == y)
                c->vol = c->out_vol = 0;
            else if (x == 0xD && t == y) {
                if (c->delay_s) {
                    c->s = c->delay_s;
                    c->vol = c->out_vol = c->s->volume;
                }
                if (c->delay_period) {
                    c->period = c->out_period = fine_period(c->delay_period, c->fine);
                    trigger(c, c->s, 0);
                }
            }
            break;
        default: break;
        }
    }
}

// The voices' step and gains from their period, volume and panning
static void voices(void)
{
    for (unsigned k = 0; k < nch; k++) {
        const ch_t *c = &chn[k];
        trk_voice_t *o = &voice[k];
        const int32_t p = c->out_period;
        o->step = (p > 0) ? STEP_K / (uint32_t) p : 0u;
        const int32_t v = clampi(c->out_vol, 0, 64);
        o->gv = v;
        o->pan = (uint8_t) ((c->pan * TRK_PAN_MAX + 127u) / 255u);     // 64, 192: 4, 12
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
    voices();
    // the tick's frames: TRK_RATE x 2.5 / tempo, the remainder carried
    const uint32_t num = TRK_RATE * 5u + tick_rem;
    tick_left = num / (2u * tempo);
    tick_rem = num % (2u * tempo);
    if (++tick < speed)
        return;
    tick = 0;
    // EEx: the row x times more, its notes not played again
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
    if (loop_row_to != ~0u) {                       // E6x: back in the pattern
        row = loop_row_to;
    } else if (jump_ord != ~0u || break_row != ~0u) {
        ord = (jump_ord != ~0u) ? jump_ord : ord + 1u;
        row = (break_row != ~0u) ? break_row : 0u;
    } else if (++row >= ROWS) {
        row = 0;
        ord++;
    }
    jump_ord = break_row = loop_row_to = ~0u;
    if (ord != from)
        for (unsigned k = 0; k < nch; k++)
            chn[k].loop_row = 0;
    if (ord >= n_orders)
        ended = 1;
}

// The mixer's tick: its frames, 0 at the song's end
static uint32_t mod_tick(void)
{
    if (ended)
        return 0u;
    song_tick();
    return tick_left;
}

static void song_reset(void)
{
    memset(chn, 0, sizeof(chn));
    memset(voice, 0, sizeof(voice));
    for (unsigned k = 0; k < nch; k++)
        chn[k].pan = ((k & 3u) == 0u || (k & 3u) == 3u) ? 64u : 192u;     // L R R L, 3/4 to one side
    memset(visited, 0, sizeof(visited));
    speed = 6;
    tempo = 125;
    tick = row = ord = 0;
    pdelay = pdelay_left = repeat = 0;
    jump_ord = break_row = loop_row_to = ~0u;
    ended = 0;
    tick_left = tick_rem = 0;
}

//==========================================================
// LOADING
//==========================================================

static uint32_t be16(const uint8_t *p) { return ((uint32_t) p[0] << 8) | p[1]; }

int mod_load(trk_read_t rd, uint32_t size, const trk_area_t *areas, unsigned n_areas, trk_info_t *info)
{
    uint8_t h[1084];
    trk_areas(areas, n_areas);
    if (size < sizeof(h) || rd(h, sizeof(h)) != sizeof(h))
        return -1;
    const uint8_t *sig = h + 1080;
    if (!memcmp(sig, "M.K.", 4) || !memcmp(sig, "M!K!", 4) || !memcmp(sig, "FLT4", 4) || !memcmp(sig, "4CHN", 4))
        nch = 4;
    else if (!memcmp(sig, "6CHN", 4))
        nch = 6;
    else if (!memcmp(sig, "8CHN", 4) || !memcmp(sig, "FLT8", 4) || !memcmp(sig, "OCTA", 4) || !memcmp(sig, "CD81", 4))
        nch = 8;
    else if (sig[0] >= '1' && sig[0] <= '9' && sig[1] >= '0' && sig[1] <= '9' && (sig[2] == 'C') && (sig[3] == 'H' || sig[3] == 'N'))
        nch = (sig[0] - '0') * 10u + (sig[1] - '0');
    else if (sig[0] >= '1' && sig[0] <= '9' && !memcmp(sig + 1, "CHN", 3))
        nch = sig[0] - '0';
    else
        return -1;
    if (nch < 1u || nch > MOD_CH_MAX)
        return -1;

    memcpy(info->title, h, 20);
    info->title[20] = 0;
    info->channels = nch;
    n_orders = h[950];
    if (!n_orders || n_orders > 128u)
        return -1;
    unsigned n_pat = 0;
    for (unsigned k = 0; k < 128u; k++) {
        order[k] = h[952 + k];
        if (order[k] >= n_pat)
            n_pat = order[k] + 1u;
    }
    const uint32_t pbytes = n_pat * ROWS * nch * 4u;
    uint8_t *pp = trk_take(pbytes);
    if (!pp || rd(pp, pbytes) != pbytes)
        return -1;
    pat = pp;

    uint32_t left = size - sizeof(h) - pbytes;
    memset(smp, 0, sizeof(smp));
    for (unsigned k = 1; k < 32u; k++) {
        const uint8_t *sh = h + 20 + (k - 1u) * 30u;
        smp_t *s = &smp[k];
        uint32_t len = be16(sh + 22) * 2u, ls = be16(sh + 26) * 2u, ll = be16(sh + 28) * 2u;
        s->finetune = (int8_t) (sh[24] & 15u);
        s->volume = (sh[25] > 64u) ? 64u : sh[25];
        if (!len)
            continue;
        if (ls + ll > len) {                                // loop start given in bytes (old trackers)
            if (ls / 2u + ll <= len)
                ls /= 2u;
            else
                ll = (ls < len) ? len - ls : 0u;
        }
        const uint32_t n = (len < left) ? len : left;       // a truncated file: the rest silent
        int8_t *d = (int8_t *) trk_take(len + 1u);
        if (!d)
            return -1;
        if (n && rd((uint8_t *) d, n) != n)
            return -1;
        memset(d + n, 0, len + 1u - n);
        left -= n;
        s->data = d;
        s->len = len;
        s->loop_start = ls;
        s->loop_len = ll;
        if (ll > 2u)
            d[ls + ll] = d[ls];                             // the interpolation's next byte
    }

    // the song's length, by a dry run (at most an hour)
    song_reset();
    uint32_t frames = 0;
    while (!ended && frames < TRK_RATE * 3600u) {
        song_tick();
        frames += tick_left;
    }
    info->frames = frames;
    song_reset();
    song.v = voice;
    song.n_voices = nch;
    song.mono = 0;
    song.shift = (nch <= 4u) ? 11u : 12u;
    song.tick = mod_tick;
    trk_start(&song);
    return 0;
}
