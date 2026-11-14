//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      img.c  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The file, through a 4 KB buffer, and the working memory (img.h).
//----------------------------------------------------------------------------
#include <string.h>
#include "img.h"

#define BUF_N   4096u

static img_read_t rd_fn;
static img_seek_t seek_fn;
static uint32_t   file_size;
static uint8_t    buf[BUF_N];
static uint32_t   b_off;                    // the file offset of buf[0]
const uint8_t    *img_p = buf, *img_e = buf;   // the next byte, the buffer's end (img_byte)
#define b_pos     ((unsigned) (img_p - buf))
#define b_len     ((unsigned) (img_e - buf))
static img_area_t area[IMG_AREAS];
static unsigned   n_area;

void img_begin(img_read_t rd, img_seek_t seek, uint32_t size, const img_area_t *areas, unsigned n)
{
    rd_fn = rd;
    seek_fn = seek;
    file_size = size;
    img_p = img_e = buf;
    b_off = 0;
    n_area = (n > IMG_AREAS) ? IMG_AREAS : n;
    memcpy(area, areas, n_area * sizeof(img_area_t));
    for (unsigned k = 0; k < n_area; k++) {                 // 4-byte aligned
        const uint32_t a = (4u - ((uint32_t) (uintptr_t) area[k].base & 3u)) & 3u;
        area[k].base += a;
        area[k].size = (area[k].size > a) ? (area[k].size - a) & ~3u : 0u;
    }
}

static int refill(void)
{
    b_off += b_len;
    const unsigned got = rd_fn(buf, BUF_N);
    img_p = buf;
    img_e = buf + got;
    return got ? 0 : -1;
}

int img_refill_byte(void)
{
    if (img_p >= img_e && refill())
        return -1;
    return *img_p++;
}

int img_read(uint8_t *dst, uint32_t n)
{
    while (n) {
        if (b_pos >= b_len) {
            if (n >= BUF_N) {                               // straight into dst
                b_off += b_len;
                img_p = img_e = buf;
                const unsigned got = rd_fn(dst, n);
                b_off += got;
                if (got < n)
                    return -1;
                return 0;
            }
            if (refill())
                return -1;
        }
        unsigned k = b_len - b_pos;
        if (k > n) k = n;
        memcpy(dst, img_p, k);
        img_p += k;
        dst += k;
        n -= k;
    }
    return 0;
}

uint32_t img_pos(void)  { return b_off + b_pos; }
uint32_t img_size(void) { return file_size; }

int img_seek(uint32_t offset)
{
    if (offset >= b_off && offset <= b_off + b_len) {       // in the buffer
        img_p = buf + (offset - b_off);
        return 0;
    }
    if (offset > file_size || seek_fn(offset))
        return -1;
    b_off = offset;
    img_p = img_e = buf;
    return 0;
}

int img_skip(uint32_t n)
{
    return img_seek(img_pos() + n);
}

void *img_take(uint32_t n)
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

int img_fits(const uint32_t *sz, unsigned n)
{
    uint32_t left[IMG_AREAS];
    for (unsigned k = 0; k < n_area; k++)
        left[k] = area[k].size;
    for (unsigned i = 0; i < n; i++) {
        const uint32_t b = (sz[i] + 3u) & ~3u;
        unsigned k = 0;
        while (k < n_area && left[k] < b)
            k++;
        if (k == n_area)
            return 0;
        left[k] -= b;
    }
    return 1;
}
