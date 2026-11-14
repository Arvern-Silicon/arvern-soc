//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      sdlink.c  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The SD card over the debugger (see sdlink.h): the mailbox, the commands
// on FatFs, and the session's panel.
//----------------------------------------------------------------------------
#include <string.h>
#include "tang_board.h"
#include "demo_kit.h"
#include "ff.h"
#include "gfx.h"
#include "sdlink.h"

#define BOX          ((sdlink_box_t *) SDLINK_MAILBOX)
#define BUF          ((uint8_t *) SDLINK_BUF)

// The session's panel: centred, three lines
#define PANEL_W      336u
#define PANEL_H      84u
#define PANEL_X      ((SCR_W - PANEL_W) / 2u)
#define PANEL_Y      ((SCR_H - PANEL_H) / 2u)
#define CHAR_W       8u                     // the menu font, 8 x 16
#define CHAR_H       16u
#define LINE_X       (PANEL_X + 16u)
#define LINE_CH      ((PANEL_W - 32u) / CHAR_W)

static FATFS    fs;
static uint32_t files, bytes;

void sdlink_init(void)
{
    BOX->version  = 2u;
    BOX->buf      = SDLINK_BUF;
    BOX->buf_size = SDLINK_BUF_SIZE;
    BOX->done     = BOX->seq;
    BOX->magic    = SDLINK_MAGIC;
}

static uint32_t crc32_file(FIL *f, FRESULT *fr)
{
    uint32_t c = 0xFFFFFFFFu;
    UINT     n;
    do {
        if ((*fr = f_read(f, BUF, SDLINK_BUF_SIZE, &n)) != FR_OK)
            return 0u;
        for (UINT i = 0; i < n; i++) {
            c ^= BUF[i];
            for (unsigned k = 0; k < 8u; k++)
                c = (c >> 1) ^ (0xEDB88320u & (uint32_t) -(int32_t) (c & 1u));
        }
    } while (n == SDLINK_BUF_SIZE);
    return ~c;
}

static char *put_num(char *d, uint32_t v)
{
    char t[10];
    unsigned n = 0;
    do { t[n++] = (char) ('0' + v % 10u); v /= 10u; } while (v);
    while (n)
        *d++ = t[--n];
    return d;
}

static FRESULT list(const char *path, uint32_t *len)
{
    DIR     d;
    FILINFO fi;
    FRESULT fr;
    char   *p = (char *) BUF, *const end = (char *) BUF + SDLINK_BUF_SIZE - 32u;

    if ((fr = f_opendir(&d, path)) != FR_OK)
        return fr;
    while ((fr = f_readdir(&d, &fi)) == FR_OK && fi.fname[0] && p < end) {
        if (fi.fattrib & AM_DIR) {
            *p++ = 'D';
        } else {
            *p++ = 'F';
            *p++ = ' ';
            p = put_num(p, (uint32_t) fi.fsize);
        }
        *p++ = ' ';
        for (const char *s = fi.fname; *s; s++)
            *p++ = *s;
        *p++ = '\n';
    }
    f_closedir(&d);
    *len = (uint32_t) (p - (char *) BUF);
    return fr;
}

static uint32_t run(sdlink_box_t *b)
{
    FIL      f;
    FRESULT  fr = FR_OK;
    FILINFO  fi;
    UINT     n = 0;
    uint32_t len = b->length;

    b->path[SDLINK_PATH - 1u]  = '\0';
    b->path2[SDLINK_PATH - 1u] = '\0';
    switch (b->cmd) {
    case SDLINK_HELLO:
        fr = f_mount(&fs, "", 1);
        break;
    case SDLINK_LIST:
        fr = list(b->path, &len);
        b->length = len;
        break;
    case SDLINK_STAT:
        fr = f_stat(b->path, &fi);
        b->result = (fr == FR_OK) ? (uint32_t) fi.fsize : 0u;
        break;
    case SDLINK_READ:
    case SDLINK_WRITE:
        if (len > SDLINK_BUF_SIZE)
            return SDLINK_ERR_SIZE;
        if (b->cmd == SDLINK_READ)
            fr = f_open(&f, b->path, FA_READ);
        else
            fr = f_open(&f, b->path, FA_WRITE | ((b->offset == 0u) ? FA_CREATE_ALWAYS : FA_OPEN_EXISTING));
        if (fr == FR_OK) {
            fr = f_lseek(&f, b->offset);
            if (fr == FR_OK)
                fr = (b->cmd == SDLINK_READ) ? f_read(&f, BUF, len, &n) : f_write(&f, BUF, len, &n);
            const FRESULT fc = f_close(&f);
            if (fr == FR_OK)
                fr = fc;
        }
        b->length = n;
        if (b->cmd == SDLINK_WRITE && fr == FR_OK && n == len) {
            bytes += n;
            if (b->offset == 0u)
                files++;
        }
        break;
    case SDLINK_MKDIR:
        fr = f_mkdir(b->path);
        break;
    case SDLINK_RENAME:
        (void) f_unlink(b->path2);
        fr = f_rename(b->path, b->path2);
        break;
    case SDLINK_DELETE:
        fr = f_unlink(b->path);
        break;
    case SDLINK_CRC:
        fr = f_open(&f, b->path, FA_READ);
        if (fr == FR_OK) {
            b->result = crc32_file(&f, &fr);
            (void) f_close(&f);
        }
        break;
    case SDLINK_BYE:
        break;
    default:
        return SDLINK_ERR_CMD;
    }
    return (uint32_t) fr;
}

static void panel_line(unsigned row, const char *s, uint8_t colour)
{
    const unsigned y = PANEL_Y + 12u + row * (CHAR_H + 4u);
    rect(LINE_X, y, LINE_CH * CHAR_W, CHAR_H, C_PANEL);
    text(LINE_X, y, s, colour, C_PANEL, LINE_CH);
}

static void panel_status(const sdlink_box_t *b)
{
    static const char *const op[] = { "", "Connected", "Listing", "Checking", "Reading", "Writing",
                                      "Creating", "Renaming", "Deleting", "Verifying", "Done" };
    char l[64], *p = l;
    const char *o = (b->cmd <= SDLINK_BYE) ? op[b->cmd] : "?";
    while (*o)
        *p++ = *o++;
    if (b->path[0] && b->cmd != SDLINK_HELLO && b->cmd != SDLINK_BYE) {
        *p++ = ' ';
        for (const char *s = b->path; *s && p < l + sizeof(l) - 1u; s++)
            *p++ = *s;
    }
    *p = '\0';
    panel_line(1u, l, C_TEXT);
    p = put_num(l, files);
    memcpy(p, " files, ", 8u);
    p = put_num(p + 8, (bytes + 1023u) / 1024u);
    memcpy(p, " KB written", 12u);
    panel_line(2u, l, C_DIM);
}

static void session(void)
{
    sdlink_box_t *const b = BOX;
    uint32_t last = ms_now();

    files = bytes = 0u;
    gfx_glass(GL_BIG, PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    panel_line(0u, "J-Link: SD card", C_ACCENT);
    for (;;) {
        if (b->seq != b->done) {
            const uint32_t cmd = b->cmd;
            b->status = run(b);
            b->done   = b->seq;
            panel_status(b);
            last = ms_now();
            if (cmd == SDLINK_BYE)
                break;
            continue;                       // the next command at once
        }
        if (ms_now() - last > SDLINK_IDLE_MS)
            break;
#ifdef SIMULATION
        wait_ms(1u);
#else
        wait_frames(1u);
#endif
        (void) gfx_frame();
    }
}

bool sdlink_poll(void)
{
    sdlink_box_t *const b = BOX;
    if (b->magic != SDLINK_MAGIC)
        sdlink_init();
    b->alive++;                             // the host sees the menu serving
    if (b->seq == b->done)
        return false;
    if (b->cmd != SDLINK_HELLO) {           // outside a session: refused
        b->status = SDLINK_ERR_CMD;
        b->done   = b->seq;
        return false;
    }
    session();
    return true;
}
