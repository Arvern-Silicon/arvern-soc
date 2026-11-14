//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      loader.c  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The loader (see loader.h). An image is read in three parts, at sector
// boundaries around the loader block: [0, a) straight into place, [a, b)
// into the staging area, [b, size) straight into place, its CRC-32 computed
// on the way. Once the first byte is written the menu is gone: a read error
// or a CRC mismatch stops here, with the error on the LEDs and the TTY (and
// on the screen for a mismatch); the mode button reloads the menu.
//----------------------------------------------------------------------------
#include <stdint.h>
#include <string.h>
#include "tang_board.h"
#include "led_key_sw.h"
#include "ff.h"
#include "loader.h"

#define CHUNK       4096u                   // file read size

extern lks_t lks;
extern char  _loader_start[], _loader_end[], _stage_start[], _app_max[];
extern void  boot_launch(const void *src, uint32_t bytes, void *dst) __attribute__((noreturn));

static uint32_t crc_table[256], crc;

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256u; i++) {
        uint32_t c = i;
        for (unsigned k = 0; k < 8u; k++)
            c = (c >> 1) ^ ((c & 1u) ? 0xEDB88320u : 0u);
        crc_table[i] = c;
    }
    crc = 0xFFFFFFFFu;
}

static void crc_add(const uint8_t *p, uint32_t n)
{
    uint32_t c = crc;
    while (n--)
        c = crc_table[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    crc = c;
}

static void bar_draw(const loader_bar_t *b, uint32_t done, uint32_t total)
{
    const unsigned fw = total ? (unsigned) ((done >> 8) * b->w / ((total >> 8) + 1u)) : 0u;
    for (unsigned r = 0; r < b->h; r++) {
        memset(b->at + r * SCR_W, b->done, fw);
        memset(b->at + r * SCR_W + fw, b->todo, b->w - fw);
    }
}

static void __attribute__((noreturn)) fail(int err, FRESULT fr)
{
    static const char msg_read[] = "boot_menu: read error while loading, reload the menu (mode button)\n";
    static const char msg_crc[]  = "boot_menu: CRC mismatch, the image is not started, reload the menu (mode button)\n";
    menu.state   = 4u;
    menu.error   = (uint32_t) err;
    menu.fresult = (uint32_t) fr;
    lks_led_set(&lks, 0x20u | (uint32_t) err);
    for (const char *s = (err == ERR_CRC) ? msg_crc : msg_read; *s; s++)
        lks_tty_putc(&lks, *s);
    for (;;)
        ;
}

static void read_part(FIL *f, uint8_t *dst, uint32_t n, uint32_t *done, uint32_t total, const loader_bar_t *bar)
{
    while (n) {
        UINT     got = 0;
        uint32_t k   = (n < CHUNK) ? n : CHUNK;
        FRESULT  fr  = f_read(f, dst, k, &got);
        if (fr == FR_OK && got != k)
            fr = FR_INT_ERR;
        if (fr != FR_OK)
            fail(ERR_READ, fr);
        crc_add(dst, k);
        dst += k; n -= k; *done += k;
        bar_draw(bar, *done, total);
    }
}

void loader_run(FIL *f, uint32_t size, const loader_bar_t *bar, const loader_check_t *check)
{
    uint8_t *const rom = (uint8_t *) BOARD_ROM_BASE;
    const uint32_t l0  = ((uint32_t) _loader_start - BOARD_ROM_BASE) & ~511u;
    uint32_t       l1  = ((uint32_t) _loader_end - BOARD_ROM_BASE + 511u) & ~511u;
    if (l1 > (uint32_t) _app_max)
        l1 = (uint32_t) _app_max;
    const uint32_t a = (size < l0) ? size : l0;
    const uint32_t b = (size < l1) ? size : l1;
    uint32_t done = 0;

    menu.staged = b - a;
    crc_init();
    memset(_stage_start + ((b - a) & ~3u), 0, 4u);     // the tail of the last word
    read_part(f, rom, a, &done, size, bar);
    read_part(f, (uint8_t *) _stage_start, b - a, &done, size, bar);
    read_part(f, rom + b, size - b, &done, size, bar);
    (void) f_close(f);
    menu.crc = ~crc;
    if (check->on && menu.crc != check->expected) {
        memcpy(check->at, check->msg, check->bytes);
        fail(ERR_CRC, FR_OK);
    }
    boot_launch(_stage_start, (b - a + 3u) & ~3u, rom + a);
}
