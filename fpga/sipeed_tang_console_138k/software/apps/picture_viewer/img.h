//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      img.h  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The decoders' common parts: the file they read (through the caller's
// callbacks), the working memory they take, and the picture they output.
//
// A decoder reads its file with img_read / img_seek and takes memory with
// img_take (areas the caller gives); it outputs rows of RGB888 pixels, top
// to bottom (scale.c fits them to the screen). An animated GIF also
// outputs frames: the rectangles a frame changes, with a transparency mask.
//----------------------------------------------------------------------------
#ifndef IMG_H
#define IMG_H

#include <stdint.h>

#define IMG_AREAS   4u

typedef enum {
    IMG_OK          =  0,
    IMG_ERR_READ    = -1,                   // the file ended or could not be read
    IMG_ERR_FORMAT  = -2,                   // not a file of this format, or a damaged one
    IMG_ERR_UNSUPP  = -3,                   // a feature this viewer does not decode
    IMG_ERR_MEMORY  = -4,                   // too large for the memory
} img_status_t;

// The file: the next bytes into dst (at most n), the bytes read; a seek to
// offset, 0 when done
typedef unsigned (*img_read_t)(uint8_t *dst, unsigned n);
typedef int      (*img_seek_t)(uint32_t offset);

typedef struct {
    uint8_t *base;
    uint32_t size;
} img_area_t;

typedef struct {
    uint32_t width, height;                 // the file's picture
    unsigned out_w, out_h;                  // the rows the decoder outputs (a JPEG's scaled IDCT)
    char     format[12];                    // "JPEG", "PNG", ... for the screen
    unsigned frames;                        // GIF: frames decoded so far (1 for a still)
} img_info_t;

// The file and the memory, before a decoder starts
void      img_begin(img_read_t rd, img_seek_t seek, uint32_t size, const img_area_t *areas, unsigned n);
// The file's next n bytes into dst: 0 when they all came
int       img_read(uint8_t *dst, uint32_t n);
// One byte (-1 at the end), skip n bytes, seek to an offset, the position
int       img_refill_byte(void);
extern const uint8_t *img_p, *img_e;        // the buffer's next byte, its end
static inline int img_byte(void)
{
    return (img_p < img_e) ? *img_p++ : img_refill_byte();
}
int       img_skip(uint32_t n);
int       img_seek(uint32_t offset);
uint32_t  img_pos(void);
uint32_t  img_size(void);
// n bytes of working memory, 4-byte aligned (NULL: none left); whether n
// blocks of sz[] bytes, taken in that order, would all be given
void     *img_take(uint32_t n);
int       img_fits(const uint32_t *sz, unsigned n);

// The output (scale.c). out_begin: the rows' size (w x h); out_row: row y
// (0 .. h - 1, in order) of RGB888 pixels. A frame (GIF): out_rect gives
// pixels x0 .. x0 + n - 1 of row y, mask[i] = 0 keeps the screen's pixel.
int       out_begin(unsigned w, unsigned h);
// out_begin for a picture decoded smaller than it is: grown to its fit by
// bilinear interpolation, not by a whole factor
int       out_begin_smooth(unsigned w, unsigned h);
// The size a w x h picture takes on the screen (a JPEG picks its reduction by it)
void      out_fit(uint32_t w, uint32_t h, unsigned *fw, unsigned *fh);
void      out_row(unsigned y, const uint8_t *rgb);
void      out_rect(unsigned y, unsigned x0, unsigned n, const uint8_t *rgb, const uint8_t *mask);
void      out_fill(unsigned x0, unsigned y0, unsigned w, unsigned h, uint32_t rgb);

#endif
