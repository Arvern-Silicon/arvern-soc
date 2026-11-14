//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      inflate.h  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// zlib decompression, one stream at a time: inflate_begin reads its header
// from byte() (the next compressed byte, -1 at the end) with mem
// (inflate_mem() bytes) its state and window; inflate_read the next n
// bytes: the bytes given (fewer at the stream's end), < 0 on an error.
//----------------------------------------------------------------------------
#ifndef INFLATE_H
#define INFLATE_H

#include <stdint.h>

uint32_t inflate_mem(void);
void    *inflate_begin(int (*byte)(void), void *mem);
int      inflate_read(uint8_t *dst, uint32_t n);

#endif
