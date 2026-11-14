//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      loader.h  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The loader: reads an opened APP.BIN into the program memory and starts it.
//
// It lives in the loader block at the top of the program memory (link.ld),
// with everything it calls: FatFs, the SD-card layer, the SPI driver, the C
// and compiler support libraries. The rest of the menu sits below and is
// overwritten by the image, so the caller hands over a quiet board
// (interrupts off, audio stopped, GPU idle) and the loader never returns:
// the bytes of the image that land below the block are read straight into
// place, those over the block go to the SRAM staging area and the launch
// routine (stub.S) copies them at the end. tools/check_loader.py checks the
// block calls and reads nothing below it.
//----------------------------------------------------------------------------
#ifndef LOADER_H
#define LOADER_H

#include <stdbool.h>
#include <stdint.h>
#include "ff.h"

enum { ERR_NONE, ERR_MOUNT, ERR_NO_DIR, ERR_NO_APPS, ERR_READ, ERR_SIZE, ERR_CRC };

// Debugger view (magic 'BMNU')
typedef struct {
    uint32_t magic;
    uint32_t state;                         // 0 mount, 1 scan, 2 menu, 3 load, 4 error
    uint32_t error;                         // ERR_*
    uint32_t fresult;                       // the FatFs result of the last error
    uint32_t apps;
    uint32_t sel;
    uint32_t size;                          // the image being loaded
    uint32_t staged;                        // its bytes staged (over the loader block)
    uint32_t crc;                           // the image's CRC-32 as read
    uint32_t crc_expected;                  // from APP.CRC
} menu_status_t;

extern menu_status_t menu;

// The progress bar: w x h pixels at 'at' in the frame (SCR_W bytes a row)
typedef struct {
    uint8_t *at;
    unsigned w, h;
    uint8_t  done, todo;                    // colours
} loader_bar_t;

// The integrity check: the image's CRC-32 (that of zlib) against APP.CRC.
// On a mismatch the loader copies 'bytes' of a message prepared in the
// other frame ('msg') onto the screen ('at') instead of starting the image.
typedef struct {
    bool           on;
    uint32_t       expected;
    const uint8_t *msg;
    uint8_t       *at;
    uint32_t       bytes;
} loader_check_t;

void loader_run(FIL *f, uint32_t size, const loader_bar_t *bar, const loader_check_t *check)
    __attribute__((noreturn));

#endif // LOADER_H
