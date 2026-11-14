//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      scale.c  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The output (img.h): a decoder's rows fitted to the screen, centred, as
// RGB565 with a 4 x 4 ordered dither.
//
// A picture larger than the screen shrinks to fit, its aspect kept: each
// screen pixel the average of the source area it covers, a source pixel
// on a boundary shared by weight (a box filter). Across, source column x
// spans [x dw, (x + 1) dw) and screen column X [X sw, (X + 1) sw); a
// screen column's sum (scaled to 8.8 by a reciprocal when 255 sw sh would
// not fit 32 bits) adds to the screen row's accumulator, the same way down:
// a source row inside a screen row adds as is (weight dh, applied at the
// end), the one across its end by its share, and the rest carries to the
// next. A channel at a time (few registers: RV32E). A smaller one
// grows by the largest whole factor that fits (each pixel a k x k square),
// or, by out_begin_smooth (a picture decoded smaller than it is), to its fit
// by bilinear interpolation: each source row interpolated across once (the
// columns' positions in carry), the last two kept in acc, a screen row
// between them. Frames (out_rect) take the nearest
// source pixel instead: a screen pixel shows the source pixel its centre's
// position falls in.
//----------------------------------------------------------------------------
#include <string.h>
#include "img.h"
#include "scale.h"

static fb_t    *fb;
static unsigned fb_w, fb_h;
static unsigned sw, sh, dw, dh, ox, oy, up;     // up: the whole factor (0: shrinking)
static uint32_t acc[3u * OUT_W_MAX];            // the screen row's whole source rows' column sums
static uint32_t carry[3u * OUT_W_MAX];          // its share of the source row across its start (weighted)
static unsigned cur_y;                          // its index; smooth: the next screen row out
static unsigned smooth;                         // growing to the fit, bilinear
static uint8_t *h_prev, *h_cur;                 // smooth: the last two source rows, interpolated across
static uint32_t ypos, ybnd;                     // the next source row's start, the screen row's end (units)
static uint32_t rh, rv;                         // 8.8 columns: 2^(40 - hs) / sw; 2^(32 + vs) / the sum's divisor
static unsigned hs, vs, big;                    // big: the columns scaled to 8.8 (255 sw sh over 32 bits)
static uint32_t div_half;                       // the divisor / 2

// v + the dither, to 5 or 6 bits, saturated (the tables: v up to 255 + 7)
static uint8_t q5[264], q6[264];

static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

static void (*sync_fn)(void);

void out_sync(void (*sync)(void))
{
    sync_fn = sync;
}

static inline void sync(void)
{
    if (sync_fn)
        sync_fn();
}

void out_target(fb_t *frame, unsigned w, unsigned h)
{
    for (unsigned v = 0; v < 264u; v++) {
        q5[v] = (uint8_t) ((v > 255u) ? 31u : v >> 3);
        q6[v] = (uint8_t) ((v > 255u) ? 63u : v >> 2);
    }
    fb = frame;
    fb_w = (w > OUT_W_MAX) ? OUT_W_MAX : w;
    fb_h = h;
}

void out_geometry(unsigned *x, unsigned *y, unsigned *w, unsigned *h)
{
    *x = ox; *y = oy; *w = dw; *h = dh;
}

static inline uint16_t rgb565(unsigned r, unsigned g, unsigned b, unsigned x, unsigned y)
{
    const unsigned d = bayer[y & 3u][x & 3u];
    return (uint16_t) ((q5[r + (d >> 1)] << 11) | (q6[g + (d >> 2)] << 5) | q5[b + (d >> 1)]);
}

void out_fit(uint32_t w, uint32_t h, unsigned *fw, unsigned *fh)
{
    if ((uint64_t) w * fb_h > (uint64_t) h * fb_w) {        // width-bound
        *fw = fb_w;
        *fh = (unsigned) (((uint64_t) h * fb_w + w / 2u) / w);
    } else {
        *fh = fb_h;
        *fw = (unsigned) (((uint64_t) w * fb_h + h / 2u) / h);
    }
    if (!*fw) *fw = 1;
    if (!*fh) *fh = 1;
}

int out_begin(unsigned w, unsigned h)
{
    if (!w || !h || !fb)
        return -1;
    sw = w;
    sh = h;
    out_fit(w, h, &dw, &dh);
    up = 0;
    smooth = 0;
    if (w <= fb_w && h <= fb_h) {                           // grows by a whole factor
        up = fb_w / w;
        if (fb_h / h < up) up = fb_h / h;
        dw = w * up;
        dh = h * up;
    }
    ox = (fb_w - dw) / 2u;
    oy = (fb_h - dh) / 2u;
    if (!up) {
        memset(acc, 0, sizeof(acc));
        memset(carry, 0, sizeof(carry));
        cur_y = 0;
        ypos = 0;
        ybnd = sh;
        big = ((uint64_t) sw * sh * 255u) >> 32 != 0u;
        for (hs = 0; ((1ull << (40u - hs)) / sw) >> 32; hs++)
            ;
        rh = (uint32_t) ((1ull << (40u - hs)) / sw);
        const uint32_t dv = big ? 256u * sh : sw * sh;
        for (vs = 0; (2u << vs) < dv && vs < 31u; vs++)
            ;
        rv = (uint32_t) ((1ull << (32u + vs)) / dv);
        div_half = dv / 2u;
    }
    return 0;
}

static inline uint32_t mulhu(uint32_t a, uint32_t b)
{
    return (uint32_t) (((uint64_t) a * b) >> 32);
}

// The screen row cur_y out: acc holds its weighted sums (sw sh a pixel,
// or 256 sh when big)
int out_begin_smooth(unsigned w, unsigned h)
{
    if (out_begin(w, h))
        return -1;
    if (!up)                                                // shrinking
        return 0;
    out_fit(w, h, &dw, &dh);
    ox = (fb_w - dw) / 2u;
    oy = (fb_h - dh) / 2u;
    up = 0;
    smooth = 1;
    cur_y = 0;
    h_prev = (uint8_t *) acc;
    h_cur = h_prev + 3u * OUT_W_MAX;
    for (unsigned X = 0; X < dw; X++) {                     // the source position of X's centre, 8.8
        const int32_t xs = (int32_t) ((((2u * X + 1u) * sw) << 8) / (2u * dw)) - 128;
        uint32_t x0 = 0, f = 0;
        if (xs > 0) { x0 = (uint32_t) xs >> 8; f = (uint32_t) xs & 255u; }
        if (x0 >= sw - 1u) { x0 = sw - 1u; f = 0; }
        carry[X] = x0 << 8 | f;
    }
    return 0;
}

// Smooth: source row y interpolated across, then the screen rows whose
// source position falls on it (or between y - 1 and y) out
static void smooth_rows(unsigned y, const uint8_t *rgb)
{
    uint8_t *t = h_prev;
    h_prev = h_cur;
    h_cur = t;
    for (unsigned X = 0; X < dw; X++, t += 3) {
        const uint32_t c = carry[X], fx = c & 255u;
        const uint8_t *a = rgb + 3u * (c >> 8);
        t[0] = (uint8_t) ((a[0] * (256u - fx) + a[3] * fx + 128u) >> 8);
        t[1] = (uint8_t) ((a[1] * (256u - fx) + a[4] * fx + 128u) >> 8);
        t[2] = (uint8_t) ((a[2] * (256u - fx) + a[5] * fx + 128u) >> 8);
    }
    while (cur_y < dh) {
        const int32_t ys = (int32_t) ((((2u * cur_y + 1u) * sh) << 8) / (2u * dh)) - 128;
        uint32_t y0 = 0, fy = 0;
        if (ys > 0) { y0 = (uint32_t) ys >> 8; fy = (uint32_t) ys & 255u; }
        if (y0 >= sh - 1u) { y0 = sh - 1u; fy = 0; }
        if (fy ? (y0 + 1u != y) : (y0 != y))
            break;
        const uint8_t *a = fy ? h_prev : h_cur, *b = h_cur;
        const uint32_t ga = 256u - fy;
        fb_t *d = fb + (oy + cur_y) * fb_w + ox;
        for (unsigned X = 0; X < dw; X++, a += 3, b += 3)
            d[X] = rgb565((a[0] * ga + b[0] * fy + 128u) >> 8, (a[1] * ga + b[1] * fy + 128u) >> 8,
                          (a[2] * ga + b[2] * fy + 128u) >> 8, ox + X, oy + cur_y);
        cur_y++;
    }
}

static void flush(void)
{
    fb_t *d = fb + (oy + cur_y) * fb_w + ox;
    uint32_t *a = acc;
    for (unsigned X = 0; X < dw; X++, a += 3) {
        d[X] = rgb565(mulhu(a[0] + div_half, rv) >> vs, mulhu(a[1] + div_half, rv) >> vs,
                      mulhu(a[2] + div_half, rv) >> vs, ox + X, oy + cur_y);
        a[0] = a[1] = a[2] = 0;
    }
    cur_y++;
}

// A screen column's sum of a source row's channel at src (stride 3): its
// whole source columns, and the one across its end by its share r (*r: the
// room left, *p: the part of the shared one already in)
#define COLUMN_SUM(sum)                                                         \
    do {                                                                        \
        uint32_t u_ = 0;                                                        \
        for (; r > w; r -= w, src += 3)                                         \
            u_ += *src;                                                         \
        const uint32_t v_ = *src, w1_ = w - r;                                  \
        src += 3;                                                               \
        sum = (u_ + v_) * w + p - v_ * w1_;                                     \
        p = v_ * w1_;                                                           \
        r = n - w1_;                                                            \
        if (b8) sum = mulhu(sum << s8, r8);                                     \
    } while (0)

// A source row inside the screen row: its column sums added as they are
static void row_inner(const uint8_t *src, uint32_t *a)
{
    const uint32_t w = dw, n = sw, r8 = rh;
    const unsigned b8 = big, s8 = hs;
    uint32_t r = n, p = 0;
    for (uint32_t *e = a + 3u * w; a < e; a += 3) {
        uint32_t sum;
        COLUMN_SUM(sum);
        *a += sum;
    }
}

// The source row across the screen row's end: the whole rows weighted (dh),
// the carry from the row across its start, this one's share; the rest
// carried
static void row_last(const uint8_t *src, uint32_t *a, uint32_t *cy, uint32_t wy0, uint32_t wy1)
{
    const uint32_t w = dw, n = sw, h = dh, r8 = rh;
    const unsigned b8 = big, s8 = hs;
    uint32_t r = n, p = 0;
    for (uint32_t *e = a + 3u * w; a < e; a += 3, cy += 3) {
        uint32_t sum;
        COLUMN_SUM(sum);
        *a = *a * h + *cy + sum * wy0;
        *cy = sum * wy1;
    }
}

void out_row(unsigned y, const uint8_t *rgb)
{
    if (y >= sh)
        return;
    sync();
    if (smooth) {
        smooth_rows(y, rgb);
        return;
    }
    if (up == 1u) {                                         // 1:1, the dither by the row's four offsets
        const unsigned Y = oy + y;
        fb_t *d = fb + Y * fb_w + ox;
        unsigned d5[4], d6[4];
        for (unsigned i = 0; i < 4u; i++) {
            d5[i] = bayer[Y & 3u][(ox + i) & 3u] >> 1;
            d6[i] = bayer[Y & 3u][(ox + i) & 3u] >> 2;
        }
        for (unsigned x = 0; x < sw; x++, rgb += 3)
            d[x] = (uint16_t) ((q5[rgb[0] + d5[x & 3u]] << 11) | (q6[rgb[1] + d6[x & 3u]] << 5) | q5[rgb[2] + d5[x & 3u]]);
        return;
    }
    if (up) {
        for (unsigned r = 0; r < up; r++) {
            const unsigned Y = oy + y * up + r;
            fb_t *d = fb + Y * fb_w + ox;
            const uint8_t *p = rgb;
            for (unsigned x = 0, X = 0; x < sw; x++, p += 3)
                for (unsigned k = 0; k < up; k++, X++)
                    d[X] = rgb565(p[0], p[1], p[2], ox + X, Y);
        }
        return;
    }
    // this row inside the screen row, or across its end: then its share
    // (wy0) completes it, the rest (wy1) carries to the next
    const uint32_t yend = ypos + dh;
    const int last = (yend >= ybnd);
    const uint32_t wy0 = last ? ybnd - ypos : dh, wy1 = dh - wy0;
    for (unsigned c = 0; c < 3u; c++) {
        if (last)
            row_last(rgb + c, acc + c, carry + c, wy0, wy1);
        else
            row_inner(rgb + c, acc + c);
    }
    ypos = yend;
    if (last) {
        flush();
        ybnd += sh;
    }
}

// The screen columns (or rows) showing source position x of n: X in
// [ceil(x d / n), ceil((x + 1) d / n)) (32 bits: x < 32768, d <= 480)
static inline unsigned near_lo(unsigned x, unsigned d, unsigned n)
{
    return (x * d + n - 1u) / n;
}

void out_rect(unsigned y, unsigned x0, unsigned n, const uint8_t *rgb, const uint8_t *mask)
{
    if (y >= sh || x0 >= sw)
        return;
    if (x0 + n > sw)
        n = sw - x0;
    sync();
    const unsigned Y0 = near_lo(y, dh, sh), Y1 = near_lo(y + 1u, dh, sh);
    for (unsigned Y = Y0; Y < Y1; Y++) {
        fb_t *d = fb + (oy + Y) * fb_w + ox;
        // the columns stepped: x dw = q sw + r, the first column ceil(x dw / sw)
        unsigned q = (x0 * dw) / sw, r = (x0 * dw) % sw;
        unsigned X0 = q + (r != 0u);
        for (unsigned i = 0; i < n; i++, rgb += 3) {
            r += dw;
            while (r >= sw) { r -= sw; q++; }
            const unsigned X1 = q + (r != 0u);
            if (!mask || mask[i])
                for (unsigned X = X0; X < X1; X++)
                    d[X] = rgb565(rgb[0], rgb[1], rgb[2], ox + X, oy + Y);
            X0 = X1;
        }
        rgb -= 3u * n;
    }
}

void out_fill(unsigned x0, unsigned y0, unsigned w, unsigned h, uint32_t rgb)
{
    const unsigned X0 = near_lo(x0, dw, sw), X1 = near_lo((x0 + w > sw) ? sw : x0 + w, dw, sw);
    const unsigned Y0 = near_lo(y0, dh, sh), Y1 = near_lo((y0 + h > sh) ? sh : y0 + h, dh, sh);
    sync();
    for (unsigned Y = Y0; Y < Y1; Y++) {
        fb_t *d = fb + (oy + Y) * fb_w + ox;
        for (unsigned X = X0; X < X1; X++)
            d[X] = rgb565((rgb >> 16) & 255u, (rgb >> 8) & 255u, rgb & 255u, ox + X, oy + Y);
    }
}
