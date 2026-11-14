//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      sdlink.h  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The SD card over the debugger: a host (tools/sdlink.py, through a J-Link)
// reads and writes the card's files with the menu's FatFs, through a mailbox
// at a fixed SRAM address, while the core runs.
//
// The host fills the arguments, then increments seq; the menu runs the
// command, sets status and the results, then copies seq into done. Data
// goes through buf (SDLINK_BUF, SDLINK_BUF_SIZE bytes, in the loader's
// staging area: unused while the menu runs). Paths are absolute, 8.3 names.
//
//   HELLO              a session starts: the menu shows a panel and ignores
//                      its inputs until BYE, or SDLINK_IDLE_MS without a command
//   LIST   path        buf: one line per entry, "D NAME\n" or "F <size> NAME\n";
//                      length: its bytes
//   STAT   path        result: the size; status FR_NO_FILE without it
//   READ   path, offset, length    buf: the bytes; length: how many were read
//   WRITE  path, offset, length    from buf; offset 0 creates or truncates
//   MKDIR  path
//   RENAME path, path2 (path2 is replaced if it exists)
//   DELETE path        a file or an empty folder
//   CRC    path        result: the file's CRC-32 (that of zlib)
//   BYE                the session ends: the menu reads the card again
//
// status: a FatFs FRESULT (0 = FR_OK), or SDLINK_ERR_* for the others.
//----------------------------------------------------------------------------
#ifndef SDLINK_H
#define SDLINK_H

#include <stdbool.h>
#include <stdint.h>

#define SDLINK_MAILBOX   0x8001FE00u
#define SDLINK_BUF       0x80010000u
#define SDLINK_BUF_SIZE  0xFE00u                    // up to the mailbox
#define SDLINK_MAGIC     0x4B4C4453u                // "SDLK"
#define SDLINK_IDLE_MS   10000u
#define SDLINK_PATH      120u

enum { SDLINK_HELLO = 1, SDLINK_LIST, SDLINK_STAT, SDLINK_READ, SDLINK_WRITE, SDLINK_MKDIR,
       SDLINK_RENAME, SDLINK_DELETE, SDLINK_CRC, SDLINK_BYE };
#define SDLINK_ERR_CMD   100u                       // unknown command
#define SDLINK_ERR_SIZE  101u                       // a length over the buffer
#define SDLINK_ERR_CARD  102u                       // no card mounted

typedef struct {
    volatile uint32_t magic;                        // SDLINK_MAGIC once the menu serves
    volatile uint32_t version;                      // 2
    volatile uint32_t buf, buf_size;                // the data buffer
    volatile uint32_t seq;                          // host: incremented with each command
    volatile uint32_t done;                         // menu: seq once the command is done
    volatile uint32_t cmd;
    volatile uint32_t status;
    volatile uint32_t offset;
    volatile uint32_t length;
    volatile uint32_t result;
    volatile uint32_t alive;                        // menu: counts its polls (version 2)
    volatile uint32_t reserved[4];
    char              path[SDLINK_PATH];
    char              path2[SDLINK_PATH];
} sdlink_box_t;

void sdlink_init(void);
// Once a poll: true when a session began (it runs until it ends, the
// display and the inputs to the session); the caller then reads the card
// again and redraws
bool sdlink_poll(void);

#endif // SDLINK_H
