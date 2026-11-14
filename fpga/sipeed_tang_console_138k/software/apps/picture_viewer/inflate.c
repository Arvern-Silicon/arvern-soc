//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      inflate.c  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository
// root.
//----------------------------------------------------------------------------
// zlib (RFC 1950) and deflate (RFC 1951) decompression, pulled: the caller
// asks for n bytes, the decoder runs until it has them. Stored, fixed and
// dynamic Huffman blocks; the 32 KB window a circular buffer. A Huffman
// table: a 10-bit look-ahead (the symbol and its length), the longer codes
// by the canonical counts.
//----------------------------------------------------------------------------
#include <string.h>
#include "inflate.h"

#define WIN         32768u
#define LOOK        10u

typedef struct {
    uint16_t look[1u << LOOK];              // symbol << 4 | length (0: longer)
    uint16_t count[16];                     // codes of each length
    uint16_t sym[320];                      // the symbols in code order
} htab_t;

typedef struct {
    int    (*byte)(void);
    uint8_t *win;
    uint32_t wpos;                          // the window's next byte
    uint32_t bits;
    unsigned nbits;
    unsigned final, type, stored_left;      // the block: last, type (3: none yet), a stored block's bytes
    unsigned copy_len, copy_dist;           // a back-reference not finished
    unsigned done;
    htab_t   lit, dist;
} inf_t;

static inf_t *S;

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t  len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t  dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const uint8_t  cl_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

static inline int need(unsigned n)
{
    while (S->nbits < n) {
        const int c = S->byte();
        if (c < 0) return -1;
        S->bits |= (uint32_t) c << S->nbits;
        S->nbits += 8;
    }
    return 0;
}

static inline unsigned getbits(unsigned n)
{
    if (need(n)) { S->done = 2; return 0; }
    const unsigned v = S->bits & ((1u << n) - 1u);
    S->bits >>= n;
    S->nbits -= n;
    return v;
}

static inline unsigned rev(unsigned v, unsigned n)
{
    unsigned r = 0;
    for (unsigned i = 0; i < n; i++, v >>= 1)
        r = (r << 1) | (v & 1u);
    return r;
}

static int build(htab_t *t, const uint8_t *lens, unsigned n)
{
    uint16_t offs[16];
    memset(t->count, 0, sizeof(t->count));
    for (unsigned i = 0; i < n; i++) t->count[lens[i]]++;
    t->count[0] = 0;
    offs[1] = 0;
    for (unsigned l = 1; l < 15u; l++) offs[l + 1] = (uint16_t) (offs[l] + t->count[l]);
    for (unsigned i = 0; i < n; i++)
        if (lens[i]) t->sym[offs[lens[i]]++] = (uint16_t) i;
    memset(t->look, 0, sizeof(t->look));
    unsigned code = 0, k = 0;
    for (unsigned l = 1; l <= LOOK; l++) {
        for (unsigned i = 0; i < t->count[l]; i++, code++, k++) {
            const unsigned r = rev(code, l);                // codes arrive LSB first
            for (unsigned f = r; f < (1u << LOOK); f += 1u << l)
                t->look[f] = (uint16_t) ((t->sym[k] << 4) | l);
        }
        code <<= 1;
    }
    return 0;
}

static int decode(const htab_t *t)
{
    if (need(LOOK) && S->nbits == 0) return -1;             // near the end: fewer bits
    const uint16_t e = t->look[S->bits & ((1u << LOOK) - 1u)];
    if (e && (e & 15u) <= S->nbits) {
        S->bits >>= e & 15u;
        S->nbits -= e & 15u;
        return e >> 4;
    }
    // canonical, a bit at a time
    int code = 0, first = 0, index = 0;
    for (unsigned l = 1; l < 16u; l++) {
        if (need(1)) return -1;
        code |= (int) (S->bits & 1u);
        S->bits >>= 1;
        S->nbits--;
        const int count = t->count[l];
        if (code - count < first)
            return t->sym[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int block_header(void)
{
    S->final = getbits(1);
    S->type = getbits(2);
    if (S->type == 0u) {                                    // stored: to a byte, then LEN, NLEN
        S->bits >>= S->nbits & 7u;
        S->nbits -= S->nbits & 7u;
        const unsigned len = getbits(16), nlen = getbits(16);
        if ((len ^ 0xFFFFu) != nlen) return -1;
        S->stored_left = len;
    } else if (S->type == 1u) {                             // fixed
        uint8_t l[320];
        unsigned i = 0;
        for (; i < 144u; i++) l[i] = 8;
        for (; i < 256u; i++) l[i] = 9;
        for (; i < 280u; i++) l[i] = 7;
        for (; i < 288u; i++) l[i] = 8;
        build(&S->lit, l, 288);
        for (i = 0; i < 30u; i++) l[i] = 5;
        build(&S->dist, l, 30);
    } else if (S->type == 2u) {                             // dynamic
        const unsigned hlit = getbits(5) + 257u, hdist = getbits(5) + 1u, hclen = getbits(4) + 4u;
        uint8_t cl[19], l[320];
        memset(cl, 0, sizeof(cl));
        for (unsigned i = 0; i < hclen; i++) cl[cl_order[i]] = (uint8_t) getbits(3);
        build(&S->lit, cl, 19);
        for (unsigned i = 0; i < hlit + hdist; ) {
            const int s = decode(&S->lit);
            if (s < 0) return -1;
            if (s < 16) { l[i++] = (uint8_t) s; continue; }
            unsigned rep, v = 0;
            if (s == 16) { if (!i) return -1; v = l[i - 1u]; rep = 3u + getbits(2); }
            else if (s == 17) rep = 3u + getbits(3);
            else rep = 11u + getbits(7);
            if (i + rep > hlit + hdist) return -1;
            while (rep--) l[i++] = (uint8_t) v;
        }
        build(&S->lit, l, hlit);
        build(&S->dist, l + hlit, hdist);
    } else {
        return -1;
    }
    return 0;
}

void *inflate_begin(int (*byte)(void), void *mem)
{
    S = (inf_t *) mem;
    memset(S, 0, sizeof(*S));
    S->byte = byte;
    S->win = (uint8_t *) mem + sizeof(inf_t);
    S->type = 3;
    // zlib header: CM 8, the check
    const int a = byte(), b = byte();
    if (b < 0 || (a & 15) != 8 || ((a << 8) | b) % 31 || (b & 32))
        return 0;
    return S;
}

uint32_t inflate_mem(void)
{
    return (uint32_t) sizeof(inf_t) + WIN;
}

int inflate_read(uint8_t *dst, uint32_t n)
{
    uint32_t got = 0;
    while (got < n) {
        if (S->copy_len) {                                  // a back-reference in progress
            uint32_t from = (S->wpos - S->copy_dist) & (WIN - 1u);
            while (S->copy_len && got < n) {
                const uint8_t c = S->win[from];
                from = (from + 1u) & (WIN - 1u);
                S->win[S->wpos] = c;
                S->wpos = (S->wpos + 1u) & (WIN - 1u);
                dst[got++] = c;
                S->copy_len--;
            }
            continue;
        }
        if (S->done)
            return (S->done == 1u) ? (int) got : -1;
        if (S->type == 3u) {
            if (block_header()) return -1;
            continue;
        }
        if (S->type == 0u) {                                // stored bytes
            if (!S->stored_left) {
                if (S->final) S->done = 1; else S->type = 3;
                continue;
            }
            const int c = (S->nbits >= 8u) ? (int) getbits(8) : S->byte();
            if (c < 0) return -1;
            S->win[S->wpos] = (uint8_t) c;
            S->wpos = (S->wpos + 1u) & (WIN - 1u);
            dst[got++] = (uint8_t) c;
            S->stored_left--;
            continue;
        }
        const int s = decode(&S->lit);
        if (s < 0) return -1;
        if (s < 256) {
            S->win[S->wpos] = (uint8_t) s;
            S->wpos = (S->wpos + 1u) & (WIN - 1u);
            dst[got++] = (uint8_t) s;
        } else if (s == 256) {
            if (S->final) S->done = 1; else S->type = 3;
        } else {
            const unsigned li = (unsigned) s - 257u;
            if (li >= 29u) return -1;
            const unsigned len = len_base[li] + getbits(len_extra[li]);
            const int d = decode(&S->dist);
            if (d < 0 || d >= 30) return -1;
            const unsigned dist = dist_base[d] + getbits(dist_extra[d]);
            if (dist > WIN) return -1;
            S->copy_len = len;
            S->copy_dist = dist;
        }
    }
    return (int) got;
}
