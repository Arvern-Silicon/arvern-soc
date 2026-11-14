//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      s3m.c  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Scream Tracker 3 S3M: a 96-byte header (title, the counts, the initial
// speed, tempo and volumes, 32 channel settings), the order list, the
// instruments' and patterns' places in the file (16-byte paragraphs), the
// channels' pannings. An instrument header gives its sample's place,
// length, loop, volume and C-4 rate; a pattern is 64 packed rows (a byte
// per cell gives the channel and which of note + instrument, volume,
// command + parameter follow; 0 ends the row). Samples are 8- or 16-bit,
// signed or unsigned (the header's format word).
//
// A note's period: 8363 x 16 x the octave's period / 2^octave / the C-4
// rate; it plays at 14317056 / period Hz. A row lasts 'speed' ticks, a tick
// 2.5 / tempo seconds.
//
// Sequencer: the ST3 commands A (speed), B (order jump), C (pattern break),
// D (volume slides, fine ones), E / F (portamentos, fine and extra fine),
// G (tone portamento), H / U (vibrato, fine), I (tremor), J (arpeggio),
// K / L (vibrato / tone portamento + volume slide), O (sample offset), Q
// (retrigger + volume change), R (tremolo), S (waveforms, panning, pattern
// loop, note cut, note delay, pattern delay), T (tempo, slides), V (global
// volume), X (panning); D E F I J K L Q R S share one parameter memory.
// The song ends at its end mark, at its last order, or when it comes back to
// a row it played (outside a pattern loop): played once. A dry run of the
// sequencer at load gives its length.
//
// The header's master volume scales every channel (64: as is).
//
// Mixing (trk.c): a channel's panning 0 .. 16 (left .. right); a mono
// module plays every channel on both sides, whatever its panning.
//----------------------------------------------------------------------------
#include <string.h>
#include "s3m.h"

#define ROWS        64u
#define ORD_MAX     256u
#define INS_MAX     100u
#define PAT_MAX     256u
#define PER_MIN     56
#define PER_MAX     27392
#define STEP_K      21276249u               // (14317056 / TRK_RATE) << 16
#define NO_NOTE     255u
#define NOTE_CUT    254u
#define CMD(c)      (1u << ((c) - 'A' + 1))     // a command's bit (A = 1)
#define SHARED      (CMD('D') | CMD('E') | CMD('F') | CMD('I') | CMD('J') | CMD('K') | CMD('L') | CMD('Q') | CMD('R') | CMD('S'))

typedef struct {
    const int8_t *data;                     // NULL: no sample
    uint32_t      len, loop_start, loop_len;  // samples; looped when loop_len > 0
    uint32_t      c2spd;
    uint8_t       volume;
} smp_t;

typedef struct {
    uint8_t note, ins, vol, cmd, par;
} cell_t;

typedef struct {
    const smp_t *s;
    int32_t  period, target, out_period;
    int32_t  vol, out_vol;
    uint8_t  cmd, par, mem;                 // the row's command, its parameter, the shared memory
    uint8_t  porta, vib_speed, vib_depth, vib_pos, vib_wave, vib_fine;
    uint8_t  trem_speed, trem_depth, trem_pos, trem_wave;
    uint8_t  tremor, offset, retrig, loop_row, loop_count, pan, note;
    cell_t   delayed;                       // SDx: the cell waiting for its tick
} ch_t;

static smp_t       *smp;                   // in the module's memory, as the tables below
static unsigned     n_ins;
static uint8_t      order[ORD_MAX];
static unsigned     n_orders;
static const uint8_t **pat;                // packed rows, NULL: empty
static const uint8_t **pat_end;            // their end
static unsigned     n_pat;
static uint8_t      chmap[32];              // the file's channel -> voice (0xFF: none)
static unsigned     nch;
static uint8_t      pan0[S3M_CH_MAX];       // the initial pannings
static ch_t         chn[S3M_CH_MAX];
static trk_voice_t  voice[S3M_CH_MAX];
static trk_song_t   song;
static unsigned     speed0, tempo0, gvol0, mvol, fast_slides;

static unsigned speed, tempo, gvol, tick, row, ord, pdelay, pdelay_left, repeat;
static unsigned jump_ord, break_row, loop_row_to;     // the row's B, C, SBx (~0u: none)
static unsigned ended;
static uint32_t tick_left, tick_rem;        // the tick's frames, the remainder carried
static uint8_t *visited;                   // n_orders x 8 bytes (64 rows)
static cell_t   cells[S3M_CH_MAX];
static const uint8_t *row_p;                // the next row's bytes in the current pattern
static unsigned row_pat, row_next;

static const uint16_t oct_period[12] = { 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 907 };
// 2^(-n / 12) in Q16, n = 0 .. 15 (arpeggio)
static const uint32_t semi_mul[16] = {
    65536, 61858, 58386, 55109, 52016, 49097, 46341, 43740, 41285, 38968, 36781, 34716, 32768, 30929, 29193, 27554
};
static const uint8_t vib_sine[32] = {
      0,  24,  49,  74,  97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120,  97,  74,  49,  24
};

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

static int32_t note_period(unsigned note, const smp_t *s)
{
    const uint32_t c2 = (s && s->c2spd) ? s->c2spd : 8363u;
    const uint32_t oct = note >> 4, n = (note & 15u) % 12u;
    return clampi((int32_t) (((8363u * 16u * oct_period[n]) >> oct) / c2), PER_MIN, PER_MAX);
}

static void trigger(ch_t *c, uint32_t offset)
{
    trk_voice_t *v = &voice[c - chn];
    const smp_t *s = c->s;
    v->on = 0;
    if (!s || !s->data)
        return;
    if (offset >= s->len)
        offset = s->loop_len ? s->loop_start : s->len;
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

// Dxy on tick t: fine slides on tick 0, the others on the later ticks (and
// on tick 0 with the fast slides)
static void vol_slide(ch_t *c, unsigned t)
{
    const unsigned x = c->mem >> 4, y = c->mem & 15u;
    if (y == 15u && x) {
        if (!t) c->vol += (int32_t) x;
    } else if (x == 15u && y) {
        if (!t) c->vol -= (int32_t) y;
    } else if (t || fast_slides) {
        if (!y) c->vol += (int32_t) x;
        else    c->vol -= (int32_t) y;
    }
    c->vol = clampi(c->vol, 0, 64);
}

// E (dir 1) / F (dir -1): fine and extra fine on tick 0, the others later
static void porta(ch_t *c, unsigned t, int dir)
{
    const unsigned p = c->mem;
    int32_t d;
    if (p >= 0xF0u)      d = t ? 0 : (int32_t) (p & 15u) * 4;
    else if (p >= 0xE0u) d = t ? 0 : (int32_t) (p & 15u);
    else                 d = t ? (int32_t) p * 4 : 0;
    if (c->period)
        c->period = clampi(c->period + dir * d, PER_MIN, PER_MAX);
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

static void vibrato(ch_t *c)
{
    const int32_t sh = c->vib_fine ? 7 : 5;
    c->out_period = c->period + ((wave(c->vib_wave, c->vib_pos) * c->vib_depth) >> sh);
    c->vib_pos = (uint8_t) (c->vib_pos + c->vib_speed);
}

//==========================================================
// THE PATTERNS
//==========================================================

// The cells of the current order's row (from the pattern's start when the
// row is not the next one); a pattern cut short ends in empty rows
static void read_row(void)
{
    const unsigned p = order[ord];
    for (unsigned k = 0; k < nch; k++)
        cells[k] = (cell_t) { NO_NOTE, 0, 0xFF, 0, 0 };
    if (p >= n_pat || !pat[p])
        return;
    if (row_pat != p || row_next != row || !row_p) {
        row_pat = p;
        row_p = pat[p];
        row_next = 0;
    }
    const uint8_t *const end = pat_end[p];
    while (row_next <= row) {
        cell_t   c = { NO_NOTE, 0, 0xFF, 0, 0 };
        uint8_t  what;
        while (row_p < end && (what = *row_p++) != 0u) {
            const unsigned n = ((what & 32u) ? 2u : 0u) + ((what & 64u) ? 1u : 0u) + ((what & 128u) ? 2u : 0u);
            if ((unsigned) (end - row_p) < n) {
                row_p = end;
                break;
            }
            if (what & 32u) { c.note = row_p[0]; c.ins = row_p[1]; row_p += 2; }
            if (what & 64u) { c.vol = row_p[0]; row_p += 1; }
            if (what & 128u) { c.cmd = row_p[0]; c.par = row_p[1]; row_p += 2; }
            const unsigned v = chmap[what & 31u];
            if (row_next == row && v < nch)
                cells[v] = c;
            c = (cell_t) { NO_NOTE, 0, 0xFF, 0, 0 };
        }
        row_next++;
    }
}

//==========================================================
// THE SEQUENCER
//==========================================================

// A cell's note, instrument and volume (tick 0, or SDx's tick)
static void cell_note(ch_t *c, const cell_t *e, unsigned cmd)
{
    if (e->ins && e->ins <= n_ins) {
        c->s = &smp[e->ins - 1u];
        c->vol = c->s->volume;
    }
    if (e->note == NOTE_CUT) {
        voice[c - chn].on = 0;
    } else if (e->note < NOTE_CUT) {
        c->note = e->note;
        const int32_t p = note_period(e->note, c->s);
        if (cmd == 'G' - 64 || cmd == 'L' - 64) {
            c->target = p;
            if (!c->period)
                c->period = p;
        } else {
            c->period = p;
            c->target = 0;
            trigger(c, (cmd == 'O' - 64) ? (uint32_t) c->offset << 8 : 0u);
        }
    }
    if (e->vol <= 64u)
        c->vol = e->vol;
}

static void row_start(void)
{
    if (visited[ord * 8u + (row >> 3)] & (1u << (row & 7u))) {
        unsigned looping = 0;
        for (unsigned k = 0; k < nch; k++)
            looping |= chn[k].loop_count;
        if (!looping) {
            ended = 1;
            return;
        }
    }
    visited[ord * 8u + (row >> 3)] |= (uint8_t) (1u << (row & 7u));

    read_row();
    for (unsigned k = 0; k < nch; k++) {
        ch_t *c = &chn[k];
        const cell_t *e = &cells[k];
        const unsigned cmd = e->cmd;
        uint8_t par = e->par;
        c->cmd = (uint8_t) cmd;
        if (cmd < 32u && (SHARED & (1u << cmd)) && (cmd + 64 != 'S' || par)) {
            if (par) c->mem = par;
            else     par = c->mem;
        }
        c->par = par;
        const unsigned x = par >> 4, y = par & 15u;
        if (cmd == 'O' - 64 && par)
            c->offset = par;
        if (cmd == 'S' - 64 && x == 0xDu && y) {
            c->delayed = *e;                    // the note waits for tick y
        } else {
            c->delayed.note = NO_NOTE;
            cell_note(c, e, cmd);
        }
        switch (cmd + 64) {
        case 'A': if (par) speed = par; break;
        case 'B': jump_ord = par; break;
        case 'C': break_row = x * 10u + y; if (break_row >= ROWS) break_row = 0; break;
        case 'D': vol_slide(c, 0); break;
        case 'E': porta(c, 0, 1); break;
        case 'F': porta(c, 0, -1); break;
        case 'G': if (par) c->porta = par; break;
        case 'H':
        case 'U':
            if (x) c->vib_speed = (uint8_t) x;
            if (y) c->vib_depth = (uint8_t) y;
            c->vib_fine = (cmd + 64 == 'U');
            break;
        case 'K': case 'L': if (fast_slides) vol_slide(c, 0); break;
        case 'Q': break;
        case 'R': if (x) c->trem_speed = (uint8_t) x; if (y) c->trem_depth = (uint8_t) y; break;
        case 'S':
            switch (x) {
            case 0x3: c->vib_wave = (uint8_t) y; break;
            case 0x4: c->trem_wave = (uint8_t) y; break;
            case 0x8: c->pan = (uint8_t) ((y * 16u + 7u) / 15u); break;
            case 0xB:
                if (!y) {
                    c->loop_row = (uint8_t) row;
                } else {
                    c->loop_count = c->loop_count ? (uint8_t) (c->loop_count - 1u) : (uint8_t) y;
                    if (c->loop_count)
                        loop_row_to = c->loop_row;
                }
                break;
            case 0xC: if (!y) c->vol = 0; break;
            case 0xE: if (!pdelay_left) pdelay = y; break;
            default: break;
            }
            break;
        case 'T': if (par >= 0x20u) tempo = par; break;
        case 'V': gvol = (par > 64u) ? 64u : par; break;
        case 'X': c->pan = (par <= 0x80u) ? (uint8_t) (par >> 3) : 8u; break;
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
        switch (c->cmd + 64) {
        case 'D': vol_slide(c, t); break;
        case 'E': porta(c, t, 1); break;
        case 'F': porta(c, t, -1); break;
        case 'G': tone_porta(c); break;
        case 'K': vol_slide(c, t); break;
        case 'L': tone_porta(c); vol_slide(c, t); break;
        case 'Q':
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
        case 'S':
            if (x == 0xCu && t == y)
                c->vol = 0;
            else if (x == 0xDu && t == y)
                cell_note(c, &c->delayed, 0);
            break;
        case 'T':
            if (par < 0x10u)      tempo = (tempo > 32u + par) ? tempo - par : 32u;
            else if (par < 0x20u) tempo = (tempo + y < 255u) ? tempo + y : 255u;
            break;
        default: break;
        }
        c->out_period = c->period;
        c->out_vol = c->vol;
        switch (c->cmd + 64) {
        case 'H': case 'U': case 'K': vibrato(c); break;
        case 'I': {                                         // on x + 1 ticks, off y + 1
            const unsigned on = x + 1u, off = y + 1u;
            if (c->tremor >= on) c->out_vol = 0;
            if (++c->tremor >= on + off) c->tremor = 0;
            break;
        }
        case 'J':
            if (c->period) {
                const unsigned n = (t % 3u == 1u) ? x : (t % 3u == 2u) ? y : 0u;
                c->out_period = (int32_t) (((uint32_t) c->period * semi_mul[n]) >> 16);
            }
            break;
        case 'R':
            c->out_vol = clampi(c->vol + ((wave(c->trem_wave, c->trem_pos) * c->trem_depth) >> 6), 0, 64);
            c->trem_pos = (uint8_t) (c->trem_pos + c->trem_speed);
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
        const int32_t v = (((clampi(c->out_vol, 0, 64) * (int32_t) gvol) >> 6) * (int32_t) mvol) >> 6;
        o->gv = v;
        o->pan = c->pan;
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
    if (loop_row_to != ~0u) {
        row = loop_row_to;
    } else if (jump_ord != ~0u || break_row != ~0u) {
        ord = (jump_ord != ~0u) ? jump_ord : ord + 1u;
        row = (break_row != ~0u) ? break_row : 0u;
    } else if (++row >= ROWS) {
        row = 0;
        ord++;
    }
    jump_ord = break_row = loop_row_to = ~0u;
    while (ord < n_orders && order[ord] == 254u)            // the markers
        ord++;
    if (ord != from)
        for (unsigned k = 0; k < nch; k++)
            chn[k].loop_row = 0;
    if (ord >= n_orders || order[ord] == 255u)
        ended = 1;
}

static uint32_t s3m_tick(void)
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
    for (unsigned k = 0; k < nch; k++) {
        chn[k].pan = pan0[k];
        chn[k].delayed.note = NO_NOTE;
    }
    memset(visited, 0, n_orders * 8u);
    speed = speed0;
    tempo = tempo0;
    gvol = gvol0;
    tick = row = ord = 0;
    while (ord < n_orders && order[ord] == 254u)
        ord++;
    pdelay = pdelay_left = repeat = 0;
    jump_ord = break_row = loop_row_to = ~0u;
    ended = (ord >= n_orders || order[ord] == 255u);
    tick_left = tick_rem = 0;
    row_p = 0;
}

//==========================================================
// LOADING
//==========================================================

static uint32_t le16(const uint8_t *p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }

int s3m_load(trk_read_t rd, trk_seek_t seek, uint32_t size, const trk_area_t *areas, unsigned n_areas,
             trk_info_t *info)
{
    uint8_t  h[0x60];
    uint16_t ins_para[INS_MAX], pat_para[PAT_MAX];
    trk_areas(areas, n_areas);
    if (size < sizeof(h) || rd(h, sizeof(h)) != sizeof(h) || memcmp(h + 0x2C, "SCRM", 4))
        return -1;
    const uint32_t ordn = le16(h + 0x20), insn = le16(h + 0x22), patn = le16(h + 0x24);
    const uint32_t flags = le16(h + 0x26), cwt = le16(h + 0x28), ffi = le16(h + 0x2A);
    if (!ordn || ordn > ORD_MAX || insn > INS_MAX || patn > PAT_MAX)
        return -1;
    memcpy(info->title, h, 28);
    info->title[28] = 0;
    gvol0 = (h[0x30] > 64u) ? 64u : h[0x30];
    speed0 = h[0x31] ? h[0x31] : 6u;
    tempo0 = (h[0x32] >= 32u) ? h[0x32] : 125u;
    const unsigned stereo = h[0x33] >> 7;
    mvol = h[0x33] & 0x7Fu;                                 // the master volume, 64 = unity here
    if (mvol < 16u) mvol = 16u;
    fast_slides = (cwt == 0x1300u) || (flags & 64u);

    // the channels: 0-7 left, 8-15 right (others: AdLib or off)
    nch = 0;
    for (unsigned k = 0; k < 32u; k++) {
        chmap[k] = 0xFF;
        const uint8_t cs = h[0x40 + k];
        if (cs < 16u) {
            if (nch >= S3M_CH_MAX)
                return -1;
            pan0[nch] = !stereo ? 8u : (cs < 8u) ? 3u : 13u;
            chmap[k] = (uint8_t) nch++;
        }
    }
    if (!nch)
        return -1;
    n_orders = ordn;
    n_ins = insn;
    n_pat = patn;
    if (rd(order, ordn) != ordn)
        return -1;
    uint8_t b[64];
    for (unsigned k = 0; k < insn; k++) {
        if (rd(b, 2) != 2u) return -1;
        ins_para[k] = (uint16_t) le16(b);
    }
    for (unsigned k = 0; k < patn; k++) {
        if (rd(b, 2) != 2u) return -1;
        pat_para[k] = (uint16_t) le16(b);
    }
    if (h[0x35] == 252u && stereo) {                        // the channels' pannings
        uint8_t dp[32];
        if (rd(dp, 32) != 32u) return -1;
        for (unsigned k = 0; k < 32u; k++)
            if (chmap[k] != 0xFFu && (dp[k] & 0x20u))
                pan0[chmap[k]] = (uint8_t) (((dp[k] & 15u) * 16u + 7u) / 15u);
    }

    // the tables, then the patterns, packed
    pat = (const uint8_t **) (void *) trk_take(2u * PAT_MAX * sizeof(uint8_t *));
    smp = (smp_t *) (void *) trk_take(INS_MAX * sizeof(smp_t));
    visited = trk_take(n_orders * 8u);
    if (!pat || !smp || !visited)
        return -1;
    pat_end = pat + PAT_MAX;
    memset(pat, 0, 2u * PAT_MAX * sizeof(uint8_t *));
    for (unsigned k = 0; k < patn; k++) {
        const uint32_t at = (uint32_t) pat_para[k] * 16u;
        if (!pat_para[k] || at + 2u > size)
            continue;
        if (seek(at) || rd(b, 2) != 2u)
            return -1;
        uint32_t n = le16(b);
        n = (n < 2u) ? 0u : n - 2u;
        if (at + 2u + n > size) n = size - at - 2u;
        uint8_t *p = trk_take(n + 1u);
        if (!p || rd(p, n) != n)
            return -1;
        p[n] = 0;
        pat[k] = p;
        pat_end[k] = p + n;
    }

    // the samples: 8 bits signed
    memset(smp, 0, INS_MAX * sizeof(smp_t));
    for (unsigned k = 0; k < insn; k++) {
        const uint32_t at = (uint32_t) ins_para[k] * 16u;
        if (!ins_para[k] || at + 0x50u > size)
            continue;
        uint8_t ih[0x50];
        if (seek(at) || rd(ih, sizeof(ih)) != sizeof(ih))
            return -1;
        smp_t *s = &smp[k];
        s->volume = (ih[0x1C] > 64u) ? 64u : ih[0x1C];
        s->c2spd = le32(ih + 0x20);
        if (ih[0] != 1u || ih[0x1E])                        // not a sample, or packed
            continue;
        const uint32_t fl = ih[0x1F], w = (fl & 4u) ? 2u : 1u;
        const uint32_t from = (((uint32_t) ih[0x0D] << 16) | le16(ih + 0x0E)) * 16u;
        uint32_t len = le32(ih + 0x10);
        if (!len || from >= size)
            continue;
        if (from + len * w > size)                          // a truncated file
            len = (size - from) / w;
        uint32_t ls = le32(ih + 0x14), le = le32(ih + 0x18);
        if ((fl & 1u) && le > ls && ls < len) {
            if (le > len) le = len;
            s->loop_start = ls;
            s->loop_len = le - ls;
            len = le;                                       // nothing plays after the loop
        }
        int8_t *d = (int8_t *) trk_take(len + 1u);
        if (!d || seek(from))
            return -1;
        const uint8_t flip = (ffi == 2u) ? 0x80u : 0u;      // unsigned samples
        for (uint32_t i = 0; i < len; ) {
            uint32_t n = (len - i) * w;
            if (n > sizeof(b)) n = sizeof(b);
            if (rd(b, n) != n)
                return -1;
            for (uint32_t j = 0; j < n; j += w)
                d[i++] = (int8_t) (b[j + w - 1u] ^ flip);  // a 16-bit sample's upper byte
        }
        d[len] = s->loop_len ? d[s->loop_start] : 0;        // the interpolation's next byte
        s->data = d;
        s->len = len;
    }

    song.v = voice;
    song.n_voices = nch;
    song.mono = !stereo;
    song.shift = (nch <= 4u) ? 11u : (nch <= 8u) ? 12u : 13u;
    song.tick = s3m_tick;

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
