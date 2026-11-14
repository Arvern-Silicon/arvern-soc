/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      console.c (zork)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The text console of the Zork port, on the 1 bpp frame: 8 x 16 characters
 * (the demo kit's mono font), the top line the game's status (inverted),
 * the rest scrolling. Words are wrapped at the right edge; a "[MORE]" waits
 * for a key when a screenful has gone by since the last input.
 *
 * Input, a line at a time: a USB keyboard (letters, digits, punctuation,
 * Backspace, Enter), or a gamepad, whose buttons type whole commands:
 *   d-pad  north, south, west, east     A  look       B  inventory
 *   X      take all                     Y  wait       Start  Enter
 *   Select up  /  Select down: up, down
 *
 * Scrolling and clearing are the GPU's (copies and fills on the frame, which
 * the refresh shows as it is); the CPU draws the characters, after waiting
 * for the GPU (it must not write the words a queued command still moves).
 *----------------------------------------------------------------------------*/
#include <string.h>
#include "tang.h"
#include "font_mono.h"
#include "belenos.h"

extern blns_t arv_blns;                         /* board.c */

static unsigned cols, rows, cx, cy, lines_since_input;
static int has_status;
static bool gpu_queued;                         /* GPU work queued since the last wait */

static void gpu_sync(void)
{
    if (gpu_queued) {
        blns_gpu_wait(&arv_blns);
        gpu_queued = false;
    }
}

static blns_surf_t row_surf(unsigned r)         /* a text row on the 1 bpp frame */
{
    const blns_surf_t s = { r * FONT_H * arv_frame_w, (uint16_t) arv_frame_w, BLNS_SWAP_NONE };
    return s;
}

static uint8_t *row_ptr(unsigned r)                  /* a text row's first byte */
{
    return arv_vram + r * FONT_H * (arv_frame_w / 8u);
}

static void glyph(unsigned c, unsigned r, unsigned ch, int invert)
{
    const unsigned pitch = arv_frame_w / 8u;
    uint8_t *p = row_ptr(r) + c;
    if (ch < 32u || ch > 126u)
        ch = 32u;
    const uint8_t *g = &font_mono[(ch - 32u) * FONT_H];
    gpu_sync();
    for (unsigned y = 0; y < FONT_H; y++, p += pitch)
        *p = invert ? (uint8_t) ~g[y] : g[y];
}

static void clear_rows(unsigned r0, unsigned n)
{
    const blns_surf_t d = row_surf(r0);
    if (n && blns_gpu_fill(&arv_blns, &d, (uint16_t) arv_frame_w, (uint16_t) (n * FONT_H), 0u,
                           BLNS_ROP_S) == BLNS_OK) {
        gpu_queued = true;
        return;
    }
    gpu_sync();
    memset(row_ptr(r0), 0, n * FONT_H * (arv_frame_w / 8u));
}

void con_clear(void)
{
    clear_rows(has_status, rows - has_status);
    cx = 0;
    cy = has_status;
    lines_since_input = 0;
}

void con_init(void)
{
    cols = arv_frame_w / FONT_W;
    rows = arv_frame_h / FONT_H;
    gpu_sync();
    memset(arv_vram, 0, arv_frame_w * arv_frame_h / 8u);
    has_status = 0;
    cx = cy = 0;
}

static int wait_key(void);

static void newline(void)
{
    cx = 0;
    if (cy + 1u < rows) {
        cy++;
    } else {
        /* the rows under the status line up by one, in one copy: its
         * destination is above its source, so the forward walk reads every
         * line before overwriting it (a copy behaves as one done pixel by
         * pixel, programming guide: copy speeds) */
        const unsigned n = rows - 1u - has_status;
        if (n) {
            const blns_surf_t   s  = row_surf(has_status + 1u), d = row_surf(has_status);
            const blns_status_t st = blns_gpu_copy(&arv_blns, &s, &d, (uint16_t) arv_frame_w,
                                                   (uint16_t) (n * FONT_H), BLNS_ROP_S);
            if (st == BLNS_OK) {
                gpu_queued = true;
            } else {                            /* not expected: the CPU, the whole scroll */
                if (st == BLNS_ERR_GPU || st == BLNS_ERR_STATE) {
                    blns_gpu_recover(&arv_blns);
                    gpu_queued = false;
                }
                gpu_sync();
                memmove(row_ptr(has_status), row_ptr(has_status + 1u), n * FONT_H * (arv_frame_w / 8u));
            }
        }
        clear_rows(rows - 1u, 1u);
    }
    if (++lines_since_input + 1u >= rows - has_status) {
        static const char more[] = "[MORE]";
        for (unsigned i = 0; more[i]; i++)
            glyph(i, cy, (unsigned char) more[i], 1);
        (void) wait_key();
        for (unsigned i = 0; more[i]; i++)
            glyph(i, cy, ' ', 0);
        lines_since_input = 0;
    }
}

static void put(unsigned ch)
{
    if (ch == '\n') {
        newline();
        return;
    }
    if (cx >= cols)
        newline();
    glyph(cx++, cy, ch, 0);
}

/* Output with word wrap: a word that would cross the edge starts a new line */
void con_write(const char *s, size_t n)
{
    size_t i = 0;
    arv_board_poll();
#ifdef SIMULATION                               /* the simulation log shows the story */
    for (size_t k = 0; k < n; k++)
        arv_putc(s[k]);
#endif
    while (i < n) {
        if (s[i] == '\n' || s[i] == ' ') {
            if (s[i] == ' ' && cx >= cols)
                newline();
            else
                put((unsigned char) s[i]);
            i++;
            continue;
        }
        size_t w = 0;
        while (i + w < n && s[i + w] != ' ' && s[i + w] != '\n')
            w++;
        if (cx + w > cols && cx > 0 && w <= cols)
            newline();
        for (size_t k = 0; k < w; k++)
            put((unsigned char) s[i + k]);
        i += w;
    }
}

void con_status(const char *text)
{
    if (!text) {
        has_status = 0;
        return;
    }
    if (!has_status) {                          /* make room: the top line is the status */
        has_status = 1;
        if (cy == 0)
            newline();
    }
    for (unsigned c = 0; c < cols; c++)
        glyph(c, 0, *text ? (unsigned char) *text++ : ' ', 1);
}

/*------------------------------- input --------------------------------------*/

/* HID usage -> ASCII (shift: the upper row) */
static int hid_ascii(uint8_t u, int shift)
{
    static const char low[] = "1234567890\n\x1b\b\t -=[]\\#;'`,./";
    static const char up[]  = "!@#$%^&*()\n\x1b\b\t _+{}|~:\"~<>?";
    if (u >= 0x04 && u <= 0x1D)
        return (shift ? 'A' : 'a') + (u - 0x04);
    if (u >= 0x1E && u <= 0x38)
        return (shift ? up : low)[u - 0x1E];
    return 0;
}

static uint32_t pad_q;

/* A key (ASCII), or a gamepad command (-1 - index of pad_cmd) */
static const char *const pad_cmd[] = { "north", "south", "west", "east", "look", "inventory",
                                       "take all", "wait", "", "up", "down" };

static int poll_key(void)
{
    uint8_t u, mods;
    bool down;
    arv_input_poll();
    while (arv_key_event(&u, &mods, &down))
        if (down) {
            const int a = hid_ascii(u, (mods & 0x22u) != 0);
            if (a)
                return a;
        }
    {
        const uint32_t p = arv_pad(), d = p & ~pad_q;
        pad_q = p;
        if (p & PAD_SELECT) {
            if (d & PAD_UP)    return -1 - 9;
            if (d & PAD_DOWN)  return -1 - 10;
            return 0;
        }
        static const uint32_t order[] = { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B,
                                          PAD_X, PAD_Y, PAD_START };
        for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); i++)
            if (d & order[i])
                return -1 - (int) i;
    }
    return 0;
}

static int wait_key(void)
{
    int k;
    while ((k = poll_key()) == 0)
        arv_board_poll();
    return k;
}

static void cursor(int on)
{
    if (cx < cols)
        glyph(cx, cy, on ? '_' : ' ', 0);
}

int con_read_line(char *buf, int size)
{
    int n = 0;
    lines_since_input = 0;
    for (;;) {
        cursor(1);
        const int k = wait_key();
        cursor(0);
        if (k < 0) {                            /* a gamepad command: typed, then Enter */
            const char *c = pad_cmd[-1 - k];
#ifdef SIMULATION
            arv_printf("\nzork: command %s\n", c);   /* after the echoed prompt */
#endif
            for (; *c && n < size - 1; n++, c++)
                put((unsigned char) (buf[n] = *c));
            buf[n] = '\0';
            newline();
            lines_since_input = 0;
            return n;
        }
        if (k == '\n' || k == '\r') {
            buf[n] = '\0';
#ifdef SIMULATION
            arv_printf("%s\n", buf);                /* the typed line, after the echoed prompt */
#endif
            newline();
            lines_since_input = 0;
            return n;
        }
        if (k == '\b') {
            if (n > 0 && cx > 0) {
                n--;
                cx--;
                glyph(cx, cy, ' ', 0);
            }
            continue;
        }
        if (k >= 32 && k < 127 && n < size - 1 && cx + 1u < cols)
            put((unsigned) (buf[n++] = (char) k));
    }
}

/* A choice from a list: d-pad and A / Start, or the item's number */
int con_menu(const char *const *items, int n)
{
    int sel = 0;
    const unsigned top = cy;
    for (;;) {
        for (int i = 0; i < n; i++) {
            const char *t = items[i];
            unsigned c = 2;
            glyph(0, top + (unsigned) i, '1' + (unsigned) i, i == sel);
            for (; *t && c < cols; t++)
                glyph(c++, top + (unsigned) i, (unsigned char) *t, i == sel);
        }
        const int k = wait_key();
        if (k == -1 - 0 && sel > 0) sel--;                          /* up    */
        else if (k == -1 - 1 && sel < n - 1) sel++;                 /* down  */
        else if (k == -1 - 4 || k == -1 - 8 || k == '\n') break;    /* A, Start, Enter */
        else if (k >= '1' && k < '1' + n) { sel = k - '1'; break; }
    }
    cy = top + (unsigned) n;
    newline();
    return sel;
}
