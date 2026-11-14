//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      flac.c  (Tang Console 138K audio player)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The FLAC format (RFC 9639): "fLaC", metadata blocks (STREAMINFO first),
// then frames. A frame: a header (sync code, block size, rate, channel
// assignment, sample size, frame number, CRC-8), one subframe a channel,
// zero bits to a byte boundary, a CRC-16. A subframe: CONSTANT, VERBATIM,
// FIXED (orders 0-4) or LPC (orders 1-32, quantized coefficients and a
// shift), with "wasted" low bits; its residual is Rice coded in 2^order
// partitions, each with its parameter (or an escape to raw values). Stereo
// may be coded as left/side, side/right or mid/side, the side channel one
// bit wider. An LPC sum takes 64 bits when the sample size, the
// coefficients' precision and the order could overflow 32 (the reference
// decoder's rule).
//
// The bit reader keeps up to 32 bits, most significant first, refilled a
// byte at a time from the buffer, the buffer from the read function.
//----------------------------------------------------------------------------
#include <string.h>
#include "flac.h"

static flac_read_t rd_fn;
static flac_skip_t skip_fn;
static uint8_t    *rbuf;
static unsigned    rsize, rlen, rpos;
static uint32_t    cache;                   // bits, most significant first
static unsigned    nbits;                   // valid bits in cache
static unsigned    pad;                     // of which zeros past the stream's end (the last ones)
static int         at_end;

static flac_info_t si;

#if defined(__riscv)
// flac_rice.S: Rice residuals, the reader's state in registers
typedef struct {
    uint32_t       c;                       // the cache
    int32_t        f;                       // 24 - its valid bits
    const uint8_t *p, *lim;                 // the next byte; the last a sample may start at
} flac_bits_t;
int32_t *flac_rice(flac_bits_t *b, int32_t *x, int32_t *end, unsigned k);
#endif
static int32_t     chan[2][FLAC_BLOCK_MAX];

//==========================================================
// BIT READER
//==========================================================

static inline int byte_in(void)
{
    if (rpos == rlen) {
        rlen = at_end ? 0u : rd_fn(rbuf, rsize);
        rpos = 0;
        if (!rlen) {
            at_end = 1;
            return -1;
        }
    }
    return rbuf[rpos++];
}

static inline void fill(void)
{
    while (nbits <= 24u) {
        const int b = byte_in();
        if (b < 0) {                            // past the end: zeros
            nbits += 8u;
            pad += 8u;
            continue;
        }
        cache |= (uint32_t) b << (24u - nbits);
        nbits += 8u;
    }
}

// n bits, 1 to 24
static inline uint32_t bits(unsigned n)
{
    fill();
    const uint32_t v = cache >> (32u - n);
    cache <<= n;
    nbits -= n;
    if (pad > nbits)
        pad = nbits;
    return v;
}

// The stream's bits not yet read
static inline int ended(unsigned min)
{
    return at_end && (rpos == rlen) && (nbits - pad < min);
}

// n bits, 0 to 32
static uint32_t bits32(unsigned n)
{
    if (n == 0u)
        return 0u;
    if (n <= 24u)
        return bits(n);
    const uint32_t hi = bits(n - 16u);
    return (hi << 16) | bits(16u);
}

// A signed value of n bits, 0 to 32
static inline int32_t sbits(unsigned n)
{
    if (n == 0u)
        return 0;
    const uint32_t v = bits32(n);
    return (n == 32u) ? (int32_t) v : (int32_t) (v << (32u - n)) >> (32u - n);
}

// The zeros before the next 1 (which is consumed)
static inline uint32_t unary(void)
{
    uint32_t z = 0;
    for (;;) {
        fill();
        if (cache) {
            const unsigned k = (unsigned) __builtin_clz(cache);
            cache = (k == 31u) ? 0u : cache << (k + 1u);  // no shift by 32
            nbits -= k + 1u;
            if (pad > nbits)
                pad = nbits;
            return z + k;
        }
        z += nbits;
        nbits = pad = 0;
        if (at_end && (rpos == rlen))
            return z;                           // a damaged stream: stop somewhere
    }
}

static void align(void)
{
    const unsigned k = nbits % 8u;
    cache <<= k;
    nbits -= k;
}

// The byte position: the reader is byte aligned; nbits / 8 bytes are cached
static void skip_bytes(uint32_t n)
{
    while (n && nbits) {
        (void) bits(8u);
        n--;
    }
    const uint32_t in_buf = rlen - rpos;
    if (n <= in_buf) {
        rpos += n;
        return;
    }
    n -= in_buf;
    rpos = rlen = 0;
    if (skip_fn(n))
        at_end = 1;
}

//==========================================================
// METADATA
//==========================================================

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }

// A Vorbis comment field: "NAME=value" when name matches (any case)
static void comment(const char *f, uint32_t n, const char *name, char *dst, unsigned max)
{
    const unsigned l = (unsigned) strlen(name);
    if ((n <= l) || (f[l] != '='))
        return;
    for (unsigned i = 0; i < l; i++)
        if ((f[i] & ~0x20) != name[i])
            return;
    n -= l + 1u;
    if (n > max - 1u)
        n = max - 1u;
    memcpy(dst, f + l + 1u, n);
    dst[n] = 0;
}

static void vorbis_comment(uint32_t len, char *title, char *artist, unsigned max)
{
    uint8_t  t[4];
    char     f[64];
    uint32_t left = len;
    // the vendor string, then a count of "NAME=value" fields, each after its length
    for (unsigned i = 0; i < 4u; i++) t[i] = (uint8_t) bits(8u);
    uint32_t n = le32(t);
    if (n + 8u > left)
        goto skip;
    skip_bytes(n);
    left -= 4u + n;
    for (unsigned i = 0; i < 4u; i++) t[i] = (uint8_t) bits(8u);
    left -= 4u;
    for (uint32_t k = le32(t); k && (left >= 4u); k--) {
        for (unsigned i = 0; i < 4u; i++) t[i] = (uint8_t) bits(8u);
        left -= 4u;
        n = le32(t);
        if (n > left)
            break;
        const uint32_t m = (n < sizeof(f)) ? n : sizeof(f);
        for (uint32_t i = 0; i < m; i++)
            f[i] = (char) bits(8u);
        if (n > m)
            skip_bytes(n - m);
        left -= n;
        comment(f, m, "TITLE", title, max);
        comment(f, m, "ARTIST", artist, max);
    }
skip:
    skip_bytes(left);
}

int flac_open(flac_read_t rd, flac_skip_t skip, uint8_t *buf, unsigned size,
              flac_info_t *info, char *title, char *artist, unsigned text_max)
{
    rd_fn = rd;
    skip_fn = skip;
    rbuf = buf;
    rsize = size;
    rlen = rpos = 0;
    cache = 0;
    nbits = pad = 0;
    at_end = 0;
    if (bits(16u) != 0x664Cu || bits(16u) != 0x6143u)    // "fLaC"
        return -1;
    int got_info = 0;
    for (;;) {
        const uint32_t last = bits(1u), type = bits(7u), len = bits(24u);
        if (type == 0u && len >= 34u) {
            (void) bits(16u);                               // min block size
            si.block_max = bits(16u);
            (void) bits(24u);                               // min, max frame size
            (void) bits(24u);
            si.rate     = bits(20u);
            si.channels = bits(3u) + 1u;
            si.bps      = bits(5u) + 1u;
            const uint32_t hi = bits(4u);
            si.frames   = (hi ? 0u : bits32(32u));          // over 2^32 frames: unknown
            if (hi)
                (void) bits32(32u);
            skip_bytes(16u + len - 34u);                    // MD5
            got_info = 1;
        } else if (type == 4u) {
            vorbis_comment(len, title, artist, text_max);
        } else {
            skip_bytes(len);
        }
        if (last || at_end)
            break;
    }
    *info = si;
    return (got_info && !at_end && (si.channels <= 2u) && (si.bps >= 4u) && (si.bps <= 24u) &&
            (si.block_max <= FLAC_BLOCK_MAX) && si.rate) ? 0 : -1;
}

void *flac_buffers(uint32_t *size)
{
    *size = sizeof(chan);
    return chan;
}

//==========================================================
// FRAMES
//==========================================================

static const uint8_t crc8_t[16] = { 0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
                                    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D };
static uint8_t crc8;
static uint32_t hbyte(void)                 // a header byte, through the CRC-8
{
    const uint32_t v = bits(8u);
    crc8 ^= (uint8_t) v;
    crc8 = (uint8_t) ((crc8 << 4) ^ crc8_t[crc8 >> 4]);
    crc8 = (uint8_t) ((crc8 << 4) ^ crc8_t[crc8 >> 4]);
    return v;
}

// The residual of a subframe of block samples, order warm-up samples first
static int residual(int32_t *x, unsigned block, unsigned order)
{
    const unsigned method = bits(2u);
    if (method > 1u)
        return -1;
    const unsigned pbits = method ? 5u : 4u, escape = method ? 31u : 15u;
    const unsigned porder = bits(4u), parts = 1u << porder;
    if ((block >> porder) < order || ((block >> porder) << porder) != block)
        return -1;
    unsigned i = order;
    for (unsigned p = 0; p < parts; p++) {
        const unsigned n = (block >> porder) - (p ? 0u : order);
        const unsigned k = bits(pbits);
        if (k == escape) {
            const unsigned nb = bits(5u);
            for (unsigned j = 0; j < n; j++)
                x[i++] = sbits(nb);
        } else {
            int32_t *xp = x + i, *const xe = x + i + n;
            while (xp < xe) {
#if defined(__riscv)
                if ((k <= 24u) && !pad && (rlen - rpos >= 8u)) {
                    flac_bits_t b = { cache, 24 - (int32_t) nbits, rbuf + rpos, rbuf + rlen - 8 };
                    xp = flac_rice(&b, xp, xe, k);
                    cache = b.c;
                    nbits = (unsigned) (24 - b.f);
                    rpos  = (unsigned) (b.p - rbuf);
                    if (xp == xe)
                        break;
                }
#endif
                // a sample near the buffer's end, after a quotient longer
                // than the cache, or with k over 24: the generic reader
                const uint32_t v = (unary() << k) | bits32(k);
                *xp++ = (int32_t) (v >> 1) ^ -(int32_t) (v & 1u);
            }
            i += n;
        }
    }
    return 0;
}

static int subframe(int32_t *x, unsigned block, unsigned bps)
{
    if (bits(1u))
        return -1;
    const unsigned type = bits(6u);
    unsigned wasted = 0;
    if (bits(1u))
        wasted = unary() + 1u;
    if (wasted >= bps)
        return -1;
    bps -= wasted;

    if (type == 0u) {                                   // CONSTANT
        const int32_t v = sbits(bps);
        for (unsigned i = 0; i < block; i++)
            x[i] = v;
    } else if (type == 1u) {                            // VERBATIM
        for (unsigned i = 0; i < block; i++)
            x[i] = sbits(bps);
    } else if (type >= 8u && type <= 12u) {             // FIXED
        const unsigned o = type - 8u;
        if (o > block)
            return -1;
        for (unsigned i = 0; i < o; i++)
            x[i] = sbits(bps);
        if (residual(x, block, o))
            return -1;
        switch (o) {
        case 1: for (unsigned i = 1; i < block; i++) x[i] += x[i - 1]; break;
        case 2: for (unsigned i = 2; i < block; i++) x[i] += 2 * x[i - 1] - x[i - 2]; break;
        case 3: for (unsigned i = 3; i < block; i++) x[i] += 3 * (x[i - 1] - x[i - 2]) + x[i - 3]; break;
        case 4: for (unsigned i = 4; i < block; i++) x[i] += 4 * (x[i - 1] + x[i - 3]) - 6 * x[i - 2] - x[i - 4]; break;
        default: break;
        }
    } else if (type >= 32u) {                           // LPC
        const unsigned o = type - 31u;
        int32_t c[32];
        if (o > block)
            return -1;
        for (unsigned i = 0; i < o; i++)
            x[i] = sbits(bps);
        const unsigned prec = bits(4u) + 1u;
        const int32_t  shift = sbits(5u);
        if (prec == 16u || shift < 0)
            return -1;
        for (unsigned i = 0; i < o; i++)
            c[i] = sbits(prec);
        if (residual(x, block, o))
            return -1;
        // c[0] weighs the newest sample
        unsigned lg = 0;
        while ((1u << lg) < o)
            lg++;
        if ((bps + prec + lg <= 32u) && (o <= 12u)) {
            // the taps from the oldest down, entered by the order
            for (unsigned i = o; i < block; i++) {
                const int32_t *h = &x[i - 1];
                int32_t s = 0;
                switch (o) {
                case 12: s += c[11] * h[-11]; /* fall through */
                case 11: s += c[10] * h[-10]; /* fall through */
                case 10: s += c[9] * h[-9];   /* fall through */
                case 9:  s += c[8] * h[-8];   /* fall through */
                case 8:  s += c[7] * h[-7];   /* fall through */
                case 7:  s += c[6] * h[-6];   /* fall through */
                case 6:  s += c[5] * h[-5];   /* fall through */
                case 5:  s += c[4] * h[-4];   /* fall through */
                case 4:  s += c[3] * h[-3];   /* fall through */
                case 3:  s += c[2] * h[-2];   /* fall through */
                case 2:  s += c[1] * h[-1];   /* fall through */
                default: s += c[0] * h[0];
                }
                x[i] += s >> shift;
            }
        } else if (bps + prec + lg <= 32u) {
            for (unsigned i = o; i < block; i++) {
                int32_t s = 0;
                const int32_t *h = &x[i - 1];
                for (unsigned j = 0; j < o; j++)
                    s += c[j] * h[-(int) j];
                x[i] += s >> shift;
            }
        } else {
            for (unsigned i = o; i < block; i++) {
                int64_t s = 0;
                const int32_t *h = &x[i - 1];
                for (unsigned j = 0; j < o; j++)
                    s += (int64_t) c[j] * h[-(int) j];
                x[i] += (int32_t) (s >> shift);
            }
        }
    } else {
        return -1;
    }
    if (wasted)
        for (unsigned i = 0; i < block; i++)
            x[i] = (int32_t) ((uint32_t) x[i] << wasted);
    return 0;
}

int flac_frame(sink_t *s)
{
    static const uint8_t sizes[8] = { 0, 8, 12, 0, 16, 20, 24, 32 };
    unsigned bcode, rcode, ca, scode, block, bps;
    for (;;) {
        // the sync code on a byte boundary
        align();
        for (;;) {
            if (ended(16u))
                return 0;
            fill();
            if ((cache >> 18) == 0x3FFEu)               // 14 bits 11111111111110
                break;
            (void) bits(8u);
        }
        crc8 = 0;
        (void) hbyte();
        (void) hbyte();
        const uint32_t b2 = hbyte(), b3 = hbyte();
        bcode = b2 >> 4;
        rcode = b2 & 15u;
        ca    = b3 >> 4;
        scode = (b3 >> 1) & 7u;
        // the frame or sample number, UTF-8 coded: a continuation byte for
        // each leading one after the first
        const uint32_t b0 = hbyte();
        for (uint32_t m = 0x40u; (b0 & 0x80u) && (b0 & m) && m > 1u; m >>= 1)
            (void) hbyte();
        if (bcode == 6u) {
            block = hbyte() + 1u;
        } else if (bcode == 7u) {
            block = hbyte() << 8;
            block = (block | hbyte()) + 1u;
        } else {
            block = (bcode == 1u) ? 192u : (bcode <= 5u) ? 576u << (bcode - 2u) : 256u << (bcode - 8u);
        }
        if (rcode == 12u) {
            (void) hbyte();
        } else if (rcode == 13u || rcode == 14u) {
            (void) hbyte();
            (void) hbyte();
        }
        bps = scode ? sizes[scode] : si.bps;
        (void) hbyte();                                 // the CRC-8: crc8 is now 0
        if (!crc8)
            break;                                      // else a false sync: look further
    }
    if (bcode == 0u || block > FLAC_BLOCK_MAX || ca > 10u || bps < 4u || bps > 24u)
        return -1;
    const unsigned ch = (ca < 8u) ? ca + 1u : 2u;
    if (ch > 2u || (si.channels && ch != si.channels))
        return -1;

    for (unsigned c = 0; c < ch; c++) {
        const unsigned side = ((ca == 8u || ca == 10u) && c == 1u) || (ca == 9u && c == 0u);
        if (subframe(chan[c], block, bps + side))
            return -1;
    }
    align();
    (void) bits(16u);                                   // the CRC-16

    // decorrelate, scale to 16 bits, into the ring
    int32_t *l = chan[0], *r = chan[ch - 1u];
    if (ca == 8u) {
        for (unsigned i = 0; i < block; i++) r[i] = l[i] - r[i];
    } else if (ca == 9u) {
        for (unsigned i = 0; i < block; i++) l[i] += r[i];
    } else if (ca == 10u) {
        for (unsigned i = 0; i < block; i++) {
            const int32_t sd = r[i], m = (l[i] * 2) | (sd & 1);
            l[i] = (m + sd) >> 1;
            r[i] = (m - sd) >> 1;
        }
    }
    uint32_t w = *s->w;
    if (bps >= 16u) {
        const unsigned sh = bps - 16u;
        for (unsigned i = 0; i < block; i++, w++)
            s->ring[w & s->mask] = ((uint32_t) (uint16_t) (r[i] >> sh) << 16) | (uint16_t) (l[i] >> sh);
    } else {
        const unsigned sh = 16u - bps;
        for (unsigned i = 0; i < block; i++, w++)
            s->ring[w & s->mask] = ((uint32_t) (uint16_t) (r[i] << sh) << 16) | (uint16_t) (l[i] << sh);
    }
    *s->w = w;
    return (int) block;
}
