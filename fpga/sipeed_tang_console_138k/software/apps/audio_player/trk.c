//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      trk.c  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The tracker modules' loading and mixer (trk.h). The frames are mixed in
// blocks of at most MIX_N, inside one tick: the groups' sums, sample x
// volume, then the panned output.
//----------------------------------------------------------------------------
#include <string.h>
#include "trk.h"

#define MIX_N       256u

#if defined(__riscv)
// trk_mix.S: a voice into a group's sums, the same results as below
typedef struct {
    const int8_t *p, *pe;                   // the next sample, the end
    uint32_t      loop_len;
    uint32_t      fr, sf;                   // the position's and the step's fractions, << 16
    uint32_t      si;                       // the step's whole samples
    int32_t       g;                        // the volume
} trk_mix_t;
int trk_mix(trk_mix_t *v, int32_t *acc, int32_t *end);
#endif

static trk_area_t area[TRK_AREAS];
static unsigned   n_area;

void trk_areas(const trk_area_t *areas, unsigned n)
{
    n_area = (n > TRK_AREAS) ? TRK_AREAS : n;
    memcpy(area, areas, n_area * sizeof(trk_area_t));
}

uint8_t *trk_take(uint32_t n)
{
    n = (n + 3u) & ~3u;
    for (unsigned k = 0; k < n_area; k++)
        if (area[k].size >= n) {
            uint8_t *p = area[k].base;
            area[k].base += n;
            area[k].size -= n;
            return p;
        }
    return 0;
}

static const trk_song_t *song;
static uint32_t tick_left;
static unsigned ended;

// group sums (sample x volume) and the own-panning sums (sample x gain)
static int32_t mix_g[TRK_GROUPS][MIX_N], mix_l[MIX_N], mix_r[MIX_N];

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

// n frames: the two groups' sums panned, the own-panning sums added (when
// given), to 16 bits, packed (right in the upper half)
static void __attribute__((noinline)) out_frames(volatile uint32_t *dst, unsigned n, const int32_t *ga, const int32_t *gb,
                                                 const int32_t gn[4], const int32_t *ol, const int32_t *orr, unsigned sh)
{
    const int32_t al = gn[0], ar = gn[1], bl = gn[2], br = gn[3];
    volatile uint32_t *const end = dst + n;
    if (ol) {
        for (; dst < end; dst++) {
            const int32_t a = *ga++, b = *gb++;
            const int32_t l = clampi((al * a + bl * b + *ol++) >> sh, -32768, 32767);
            const int32_t r = clampi((ar * a + br * b + *orr++) >> sh, -32768, 32767);
            *dst = ((uint32_t) r << 16) | (uint16_t) l;
        }
    } else {
        for (; dst < end; dst++) {
            const int32_t a = *ga++, b = *gb++;
            const int32_t l = clampi((al * a + bl * b) >> sh, -32768, 32767);
            const int32_t r = clampi((ar * a + br * b) >> sh, -32768, 32767);
            *dst = ((uint32_t) r << 16) | (uint16_t) l;
        }
    }
}

void trk_start(const trk_song_t *s)
{
    song = s;
    tick_left = 0;
    ended = 0;
    for (unsigned k = 0; k < s->n_voices; k++)
        s->v[k].on = 0;
}

// A voice into a group's sums g (sample x volume), or without a group into
// the side sums with its gains gl, gr (sample x gain)
static void mix_voice(trk_voice_t *c, unsigned n, int32_t *g, int32_t gl, int32_t gr)
{
    const int8_t *d = c->d;
    uint32_t pos = c->pos, step = c->step;
    uint32_t frac = c->frac;
    const int32_t gv = c->gv;
#if defined(__riscv)
    if (g) {
        trk_mix_t mv = { d + pos, d + c->end, c->loop_len, frac << 16, (step & 0xFFFFu) << 16, step >> 16, gv };
        c->on   = (uint8_t) trk_mix(&mv, g, g + n);
        c->pos  = (uint32_t) (mv.p - d);
        c->frac = (uint16_t) (mv.fr >> 16);
        return;
    }
#endif
    for (unsigned i = 0; i < n; i++) {
        const int32_t s0 = d[pos], s1 = d[pos + 1u];
        const int32_t v = (s0 << 8) + (((s1 - s0) * (int32_t) frac) >> 8);
        if (g) {
            g[i] += v * gv;
        } else {
            mix_l[i] += v * gl;
            mix_r[i] += v * gr;
        }
        frac += step & 0xFFFFu;
        pos += (step >> 16) + (frac >> 16);
        frac &= 0xFFFFu;
        if (pos >= c->end) {
            if (!c->loop_len) {
                c->on = 0;
                break;
            }
            do
                pos -= c->loop_len;
            while (pos >= c->end);
        }
    }
    c->pos = pos;
    c->frac = (uint16_t) frac;
}

unsigned trk_render(sink_t *s, unsigned n)
{
    unsigned done = 0;
    while (done < n) {
        if (!tick_left) {
            if (ended || !(tick_left = song->tick())) {
                ended = 1;
                break;
            }
            continue;
        }
        unsigned k = n - done;
        if (k > tick_left) k = tick_left;
        if (k > MIX_N) k = MIX_N;
        // the groups: one a panning, in the voices' order
        const unsigned mono = song->mono;
        uint8_t  slot[TRK_PAN_MAX + 1u];
        unsigned gpan[TRK_GROUPS];
        unsigned ng = 0, own = 0;
        memset(slot, 0xFF, sizeof(slot));
        for (unsigned v = 0; v < song->n_voices; v++) {
            const trk_voice_t *c = &song->v[v];
            if (!c->on || !c->step)
                continue;
            const unsigned p = mono ? 0u : (c->pan > TRK_PAN_MAX) ? TRK_PAN_MAX : c->pan;
            if (slot[p] == 0xFFu && ng < TRK_GROUPS) {
                slot[p] = (uint8_t) ng;
                gpan[ng] = p;
                memset(mix_g[ng], 0, k * sizeof(int32_t));
                ng++;
            }
            if (slot[p] == 0xFFu && !own) {
                memset(mix_l, 0, k * sizeof(int32_t));
                memset(mix_r, 0, k * sizeof(int32_t));
                own = 1;
            }
        }
        for (unsigned v = 0; v < song->n_voices; v++) {
            trk_voice_t *c = &song->v[v];
            if (!c->on || !c->step)
                continue;
            const unsigned p = mono ? 0u : (c->pan > TRK_PAN_MAX) ? TRK_PAN_MAX : c->pan;
            if (slot[p] != 0xFFu)
                mix_voice(c, k, mix_g[slot[p]], 0, 0);
            else
                mix_voice(c, k, 0, c->gv * (int32_t) (mono ? TRK_PAN_MAX : TRK_PAN_MAX - p),
                          c->gv * (int32_t) (mono ? TRK_PAN_MAX : p));
        }
        // two groups in the frame loop, the others folded into the side sums
        const int32_t *ga = mix_g[0], *gb = mix_g[0];
        int32_t al = 0, ar = 0, bl = 0, br = 0;
        for (unsigned g = 0; g < ng; g++) {
            const int32_t gl = (int32_t) (mono ? TRK_PAN_MAX : TRK_PAN_MAX - gpan[g]);
            const int32_t gr = (int32_t) (mono ? TRK_PAN_MAX : gpan[g]);
            if (g == 0u) {
                ga = mix_g[0]; al = gl; ar = gr;
            } else if (g == 1u) {
                gb = mix_g[1]; bl = gl; br = gr;
            } else {
                if (!own) {
                    memset(mix_l, 0, k * sizeof(int32_t));
                    memset(mix_r, 0, k * sizeof(int32_t));
                    own = 1;
                }
                for (unsigned i = 0; i < k; i++) {
                    mix_l[i] += gl * mix_g[g][i];
                    mix_r[i] += gr * mix_g[g][i];
                }
            }
        }
        // into the ring, in at most two runs (its wrap)
        uint32_t w = *s->w;
        for (unsigned i = 0; i < k; ) {
            unsigned m = s->mask + 1u - (w & s->mask);
            if (m > k - i) m = k - i;
            const int32_t gn[4] = { al, ar, bl, br };
            out_frames(s->ring + (w & s->mask), m, ga + i, gb + i, gn, own ? mix_l + i : 0, own ? mix_r + i : 0, song->shift);
            i += m;
            w += m;
        }
        *s->w = w;
        done += k;
        tick_left -= k;
    }
    return done;
}
