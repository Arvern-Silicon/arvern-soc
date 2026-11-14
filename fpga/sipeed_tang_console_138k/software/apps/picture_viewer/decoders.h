//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      decoders.h  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The decoders: each reads the file img_begin opened, from its start, and
// outputs the picture (img.h); an img_status_t.
//----------------------------------------------------------------------------
#ifndef DECODERS_H
#define DECODERS_H

#include "img.h"

int bmp_decode(img_info_t *info);
int qoi_decode(img_info_t *info);
int jpeg_decode(img_info_t *info);
int png_decode(img_info_t *info);

// GIF: the first frame by gif_open (a still: the only one), the next ones
// by gif_next (its delay in milliseconds, < 0 at an error; it loops)
int gif_open(img_info_t *info, unsigned *delay_ms);
int gif_next(img_info_t *info, unsigned *delay_ms);

#endif
