//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K boot menu)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Boot menu: lists the programs of the microSD card (/APPS/<NAME>/APP.BIN,
// with an optional APP.TXT: a title line and a description line, APP.IMG: a
// preview, APP.CRC: the image's CRC-32), lets the user pick one and starts
// it. A folder of /APPS without an APP.BIN that holds programs is a folder
// of programs (/APPS/DEMOS/LOGO/APP.BIN), listed after the programs and
// opened like one, its programs under a way back. /APPS/LAST.TXT keeps the
// program started last ("DEMOS/LOGO" in a folder) and the sound settings:
// music, volume and the sound output (board I/O AUDIO_OUT: HDMI, the onboard
// speaker or a Pmod I2S2), which the programs started from the menu keep.
//
// An APP.BIN is the program memory image of an app from 0x20000000, as the
// J-Link would load it. It replaces the menu in the program memory: the
// loader block at the top of the program memory (loader.c) reads it, the
// launch routine above it (stub.S, never reached by an image) starts the app
// at the reset address. The app then owns the board: KEY0 restarts it, the
// mode button (SW2, an FPGA reload from flash) brings the menu back when the
// menu's bitstream is the one in flash.
//
// The card: without one (or without a FAT file system) the menu says so and
// tries again every 2 s. While the list is shown it reads the card's first
// sector every 2 s: a removed card fails the read (it loses its power, and a
// card put back answers only after a new initialisation), and the menu goes
// back to mounting, so a card put back or another card is read again.
//
// Controls: a USB gamepad (up / down, A or Start; B: out of a folder;
// Select: system information; Y: music; X: sound output; L / R: volume), a
// USB keyboard (arrows, Enter or Space, letters and digits: jump; Esc, F1,
// F2, F3, - and =) or KEY1 alone (a press: the next program; held 0.6 s:
// start it); the help line shows those of what is plugged in.
//
// LED register: 0x80 | the selection in the menu, 0x40 loading, 0x20 | the
// error code on an error (ERR_*); the status block 'menu' holds the same for
// a debugger.
//----------------------------------------------------------------------------
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "tang_board.h"
#include "demo_kit.h"
#include "usb_hid.h"
#include "ff.h"
#include "diskio.h"
#include "font_mono.h"
#include "logo_data.h"
#include "gfx.h"
#include "intro.h"
#include "audio.h"
#include "loader.h"
#include "sdlink.h"

#define APPS_DIR    "/APPS"
#define LAST_FILE   APPS_DIR "/LAST.TXT"    // the program started last
#define APPS_MAX    32u
#define TITLE_MAX   44u
#define DESC_MAX    58u
#define LONG_MS     600u                    // KEY1 held this long: start
#define REPEAT_MS   400u                    // gamepad up / down: auto-repeat after
#define RATE_MS     90u                     //   and then every
#define PROBE_MS    2000u                   // card check interval in the menu
#define SFX_MS      300u                    // the confirm sound plays out, at most this long
#define PREVIEW_MS  250u                    // the selection still this long: its preview
#define WARP_MS     700u                    // the jump to hyperspace before the fade (not in simulation)
#define HDMI_LOCK_MS 2000u                  // HDMI: black and silent first, while the monitor locks on


static const char *const err_text[] = {
    "", "No SD card (or no FAT file system on it)", "No " APPS_DIR " folder on the card",
    "No program on the card (" APPS_DIR "/<NAME>/APP.BIN)", "Read error", "Program too large",
    "Corrupt program (CRC mismatch)",
};

menu_status_t menu = { .magic = 0x554E4D42u };

// An entry: a program, a folder of programs, or the way back out of one
enum { K_APP, K_FOLDER, K_BACK };

typedef struct {
    char     dir[13];
    char     title[TITLE_MAX + 1u];
    char     desc[DESC_MAX + 1u];
    uint32_t size;
    uint8_t  kind;
    bool     img;                           // has an APP.IMG
    bool     crc;                           // has an APP.CRC
} app_t;

static app_t    apps[APPS_MAX];
static char     cwd[13];                    // the folder shown ("": /APPS itself)
static unsigned napps, sel, top, img_sel;
static bool     music_on = true;            // LAST.TXT
static unsigned volume = 6u;
static lks_audio_t aout;                    // the sound output (from reset: the build's default)
static char     aout_other;                 // the other display build's output in LAST.TXT ('0'-'2', 0: none)
static uint32_t sel_ms;
static FATFS    fs;
static usb_hid_t usb[2];
static uint32_t probe_buf[128];

// Linker symbols (link.ld): the largest image, the loader block
extern char _app_max[], _loader_start[], _loader_end[];

//==========================================================
// TEXT AND DRAWING (CPU, 8 bpp, x a multiple of 4)
//==========================================================

// The logo (64 x 45) and the wordmark of ../demo_arvern_logo in the title band
#define HEAD_H      52u
#define LOGO_X      12u
#define LOGO_Y      ((HEAD_H - SPRITE_H) / 2u)
#define WORD_X      (LOGO_X + SPRITE_W + 12u)
#define WORD_Y      ((HEAD_H - WORDMARK_H) / 2u)
#define TITLE_Y     (WORD_Y + WORDMARK_H - 13u) // text baseline on the wordmark's

// Glass panels (gfx.h) over the stars: the list and the preview right of
// it, the description, the help line; the messages, the information page
// and the loading screen in one large panel over the first three. A panel
// reaches PAD pixels past what it holds.
#define PAD         6u
#define LIST_X      16u
#define LIST_Y      (HEAD_H + 10u)
#define ROW_H       18u
#define LIST_ROWS   8u
#define DESC_Y      (LIST_Y + LIST_ROWS * ROW_H + 14u)
#define DESC_SHOW   ((SCR_W - 2u * LIST_X - 2u * PAD) / FONT_W)
#define HELP_Y      (SCR_H - 22u)
#define MSG_Y       (LIST_Y + 3u * ROW_H)    // messages: two lines
#define BAR_Y       (LIST_Y + 6u * ROW_H)    // the loader's progress bar
// The preview of the selection (APP.IMG, at most PREV_W x PREV_H) right of
// the list
#define PREV_W      128u
#define PREV_H      96u
#define PREV_X      (SCR_W - LIST_X - PREV_W)
#define PREV_Y      LIST_Y
#define ROW_W       (PREV_X - 2u * PAD - 8u - LIST_X)
#define ROW_TITLE   ((ROW_W - 16u) / FONT_W - 8u)
#define BIG_X       (LIST_X - PAD)
#define BIG_Y       (LIST_Y - PAD)
#define BIG_W       (SCR_W - 2u * BIG_X)
#define BIG_H       (DESC_Y + FONT_H + 4u - BIG_Y)
// The loading screen: a small panel in the middle, the stars around it
#define LOAD_W      320u
#define LOAD_X      ((SCR_W - LOAD_W) / 2u)
#define LOAD_TX     (LOAD_X + 16u)
#define LOAD_BAR_W  (LOAD_W - 32u)
#define NO_IMG      0xFFFFFFFFu
_Static_assert(DESC_Y + FONT_H + 4u + PAD <= HELP_Y - 4u, "the list does not fit above the help line");

static void path_of(char *p, const char *dir, const char *file)
{
    strcpy(p, APPS_DIR "/");
    if (cwd[0]) {
        strcat(p, cwd);
        strcat(p, "/");
    }
    strcat(p, dir);
    strcat(p, "/");
    strcat(p, file);
}

static void puts_tty(const char *s)
{
    while (*s)
        lks_tty_putc(&lks, *s++);
}

// USB ports (J15, J16): 16 x 12 icons in the title band, left of the volume
#define SOUND_CH    6u                      // the sound field right of them: "HDMI 6"
#define ICON_X(p)   (SCR_W - 12u - SOUND_CH * FONT_W - 12u - (2u - (p)) * 20u)
#define ICON_Y      (TITLE_Y + 2u)
enum { DEV_NONE, DEV_BUSY, DEV_KEYBOARD, DEV_MOUSE, DEV_PAD, DEV_UNSUPPORTED };

static const uint16_t icon_bits[][12] = {
    { 0x0000, 0x0000, 0x0FF0, 0x0810, 0x0BD0, 0x0BD0, 0x0810, 0x0FF0, 0x0000, 0x0000, 0x0000, 0x0000 },  // socket
    { 0x0000, 0x0000, 0x0FF0, 0x0810, 0x0BD0, 0x0BD0, 0x0810, 0x0FF0, 0x0000, 0x0000, 0x0000, 0x0000 },
    { 0x0000, 0xFFFF, 0x8001, 0xAAA9, 0x8001, 0x9555, 0x8001, 0xAAA9, 0x8001, 0x9FF9, 0x8001, 0xFFFF },  // keyboard
    { 0x0100, 0x0100, 0x0FE0, 0x1110, 0x1110, 0x1FF0, 0x1010, 0x1010, 0x1010, 0x1010, 0x0820, 0x07C0 },  // mouse
    { 0x0000, 0x0000, 0x3FFC, 0x4002, 0x4812, 0x9C09, 0x8825, 0x8009, 0x8001, 0x47E2, 0x2004, 0x1FF8 },  // gamepad
    { 0x8001, 0x4002, 0x2004, 0x1008, 0x0810, 0x0420, 0x0420, 0x0810, 0x1008, 0x2004, 0x4002, 0x8001 },  // not supported
};
static const uint8_t icon_colour[] = { C_FRAME, C_DIM, C_ICON, C_ICON, C_ICON, C_WARN };
static unsigned port_dev[2];

static unsigned port_state(unsigned p)
{
    if (!usb[p].hid.host)
        return DEV_NONE;
    switch (apb_usb_host_state(&usb[p].host)) {
    case APB_USB_HOST_STATE_OFF:
    case APB_USB_HOST_STATE_DETACHED:    return DEV_NONE;
    case APB_USB_HOST_STATE_FAILED:
    case APB_USB_HOST_STATE_UNSUPPORTED:
    case APB_USB_HOST_STATE_FS_DETECTED: return DEV_UNSUPPORTED;
    case APB_USB_HOST_STATE_RUN:
        switch (usb_hid_type(&usb[p])) {
        case USB_HID_KEYBOARD: return DEV_KEYBOARD;
        case USB_HID_MOUSE:    return DEV_MOUSE;
        case USB_HID_GAMEPAD:  return DEV_PAD;
        default:               return DEV_BUSY;
        }
    default:                             return DEV_BUSY;
    }
}

static void draw_port(unsigned p)
{
    const unsigned d = port_dev[p];
    uint8_t *v = vram + ICON_Y * SCR_W + ICON_X(p);
    gfx_sync();
    for (unsigned y = 0; y < 12u; y++, v += SCR_W)
        for (unsigned x = 0; x < 16u; x++)
            v[x] = (icon_bits[d][y] & (0x8000u >> x)) ? icon_colour[d] : C_BAND;
}

// A device plugged in or out: the icon, and a sound (a chime in, a chime
// out, the error tone once for a device the port cannot use)
static bool port_err[2];

static void ports_poll(void)
{
    for (unsigned p = 0; p < 2u; p++) {
        const unsigned d = port_state(p), was = port_dev[p];
        const bool dev_now = d >= DEV_KEYBOARD && d <= DEV_PAD, dev_was = was >= DEV_KEYBOARD && was <= DEV_PAD;
        if (d == was)
            continue;
        if (dev_now && !dev_was)
            synth_sfx(SFX_PLUG);
        else if (dev_was && !dev_now)
            synth_sfx(SFX_UNPLUG);
        else if (d == DEV_UNSUPPORTED && !port_err[p])
            synth_sfx(SFX_ERROR);
        if (d == DEV_UNSUPPORTED)
            port_err[p] = true;
        else if (d == DEV_NONE)
            port_err[p] = false;
        port_dev[p] = d;
        draw_port(p);
    }
}

// The help line, centred: the controls of what is plugged in (a gamepad, a
// keyboard, both: their lines in turn every four seconds), KEY1's without
static const char *const help_pad[] = {
    "Up/Down: select  A: run  Y: music  X: output  L/R: vol",
    "Select: system info  B: back  KEY1: next, held: run",
};
static const char *const help_kbd[] = {
    "Arrows: select  Enter: run  F2: music  F3: output",
    "F1: info  -/=: volume  a-z/0-9: jump  Esc: back",
};
static const char *const help_key1 = "KEY1: next program  KEY1 held: run";
static const char *help_shown;

static const char *help_now(void)
{
    bool pad = false, kbd = false;
    for (unsigned p = 0; p < 2u; p++) {
        pad |= port_dev[p] == DEV_PAD;
        kbd |= port_dev[p] == DEV_KEYBOARD;
    }
    const unsigned n = (pad ? 2u : 0u) + (kbd ? 2u : 0u);
    if (!n)
        return help_key1;
    const unsigned k = (gfx_frame_nr() / 240u) % n;
    return (pad && k < 2u) ? help_pad[k] : help_kbd[k & 1u];
}

static void draw_help(void)
{
    help_shown = help_now();
    const unsigned n = (unsigned) strlen(help_shown), x = ((SCR_W - n * FONT_W) / 2u) & ~3u;
    gfx_glass_off(GL_HELP);
    gfx_glass(GL_HELP, x - PAD - 4u, HELP_Y - 4u, n * FONT_W + 2u * PAD + 8u, FONT_H + 8u);
    text(x, HELP_Y, help_shown, C_DIM, C_PANEL, n);
}

// Input polling: once a frame on the board (the frame wait also runs the HDMI
// hot-plug watch of demo_kit); in simulation, where a frame is long, at a
// fixed short interval. The palette and the USB icons follow, and the SD
// link: a session over the debugger (sdlink.c) runs from here, after which
// the card is read again (link_done)
static bool link_done;

static void poll(void)
{
#ifdef SIMULATION
    wait_ms(20u);
#else
    wait_frames(1u);
#endif
    (void) gfx_frame();
    if (sdlink_poll())
        link_done = true;
    ports_poll();
    if (help_now() != help_shown)
        draw_help();
}

// Sound output and volume, in the title band: "HDMI 6", "Spkr 0", "Pmod -"
// (music off)

static void draw_volume(void)
{
    static const char out[4][5] = { "HDMI", "Spkr", "Pmod", "Off " };
    char v[SOUND_CH + 1u];
    memcpy(v, out[aout & 3u], 4u);
    v[4] = ' ';
    v[5] = music_on ? (char) ('0' + volume) : '-';
    v[6] = '\0';
    text(SCR_W - 12u - SOUND_CH * FONT_W, TITLE_Y, v, C_DIM, C_BAND, SOUND_CH);
}

static void draw_frame(void)
{
    rect(0, 0, SCR_W, SCR_H, C_BGA);
    rect(0, 0, SCR_W, HEAD_H - 1u, C_BAND);
    rect(0, HEAD_H - 1u, SCR_W, 1u, C_HI);
    gfx_keep_out(GL_HEAD, 0, 0, SCR_W, HEAD_H);
    draw_logo(LOGO_X, LOGO_Y);
    draw_wordmark(WORD_X, WORD_Y, PAL_WORD, WORDMARK_W);
    text((WORD_X + WORDMARK_W + 15u) & ~3u, TITLE_Y, "boot menu", C_ACCENT, C_BAND, 9u);
    text(SCR_W - 12u - 17u * FONT_W, 6u, "Tang Console 138K", C_DIM, C_BAND, 17u);
    draw_volume();
    draw_port(0);
    draw_port(1);
}


static void draw_row(unsigned i)
{
    const unsigned y = LIST_Y + (i - top) * ROW_H;
    const uint8_t  bg = (i == sel) ? C_BAR : C_PANEL;
    char kb[12];
    unsigned k = (apps[i].size + 1023u) / 1024u, n = 0;

    rect(LIST_X, y, ROW_W, ROW_H, bg);
    text(LIST_X + 8u, y + 1u, apps[i].title, (apps[i].kind == K_APP) ? C_TEXT : C_ACCENT, bg, ROW_TITLE);
    if (apps[i].kind == K_FOLDER)
        text(LIST_X + ROW_W - 8u - 2u * FONT_W, y + 1u, ">>", C_ACCENT, bg, 2u);
    if (apps[i].kind != K_APP)
        return;
    // size, right-aligned: "148 KB"
    char d[6];
    do { d[n++] = (char) ('0' + k % 10u); k /= 10u; } while (k && n < sizeof(d));
    for (unsigned j = 0; j < n; j++)
        kb[j] = d[n - 1u - j];
    memcpy(kb + n, " KB", 4u);
    text(LIST_X + ROW_W - 8u - (n + 3u) * FONT_W, y + 1u, kb, C_DIM, bg, 10u);
}

// The description, on a panel as wide as it
static void draw_desc(void)
{
    unsigned n = (unsigned) strlen(apps[sel].desc);
    if (n > DESC_SHOW)
        n = DESC_SHOW;
    gfx_glass_off(GL_DESC);
    if (n) {
        gfx_glass(GL_DESC, LIST_X - PAD, DESC_Y - 4u, n * FONT_W + 2u * PAD, FONT_H + 8u);
        text(LIST_X, DESC_Y, apps[sel].desc, C_ACCENT, C_PANEL, n);
    }
}

// The preview panel: the entry's APP.IMG, or blobs in a colour of its own
// (the hue and the pattern from its folder's name)
static uint32_t name_seed(const char *s)
{
    uint32_t h = 2166136261u;               // FNV-1a
    while (*s)
        h = (h ^ (uint8_t) *s++) * 16777619u;
    return h;
}

// The preview area's blob picture: whole (blob_shown: whose it is), or a few
// rows at a time from the menu loop (draw_preview): the whole one is 12 k
// CPU-computed pixels, about 11 ms at 33.8 MHz, which in the simulation
// (500 times faster time) is long enough for a key press to come and go
// unseen
#define BLOB_ROWS   2u                      // rows per menu iteration (about 0.1 ms in simulation)
#define BLOB_PAL    16u                     // palette entries per iteration, before the rows
#define BLOB_PAL_STEPS ((PAL_IMG_NR + BLOB_PAL - 1u) / BLOB_PAL)
static unsigned blob_shown = NO_IMG, blob_i = NO_IMG, blob_y;

static void draw_blob_preview(unsigned i)
{
    draw_blobs(PREV_X, PREV_Y, PREV_W, PREV_H, name_seed(apps[i].dir));
    blob_shown = i;
    blob_i     = NO_IMG;
}

static void draw_panel(void)
{
    gfx_glass(GL_PREV, PREV_X - PAD, PREV_Y - PAD, PREV_W + 2u * PAD, PREV_H + 2u * PAD);
    if (sel < napps)
        draw_blob_preview(sel);
}

static void draw_preview(unsigned i)
{
    static uint8_t row[PREV_W];
    uint8_t hdr[12];
    char    p[40];
    FIL     f;
    UINT    n;
    bool    ok = false;

    if (!apps[i].img) {
        if (blob_shown == i) {              // already there (draw_panel)
            img_sel = i;
            return;
        }
        if (blob_i != i) {                  // the palette first, then the rows
            blob_i     = i;
            blob_y     = 0u;
            blob_shown = NO_IMG;
        }
        if (blob_y < BLOB_PAL_STEPS) {      // blob_y: the palette steps, then the rows
            draw_blobs_pal(name_seed(apps[i].dir), blob_y * BLOB_PAL, BLOB_PAL);
            blob_y++;
            return;
        }
        const unsigned y0 = blob_y - BLOB_PAL_STEPS;
        const unsigned yb = (y0 + BLOB_ROWS < PREV_H) ? y0 + BLOB_ROWS : PREV_H;
        draw_blobs_rows(PREV_X, PREV_Y, PREV_W, name_seed(apps[i].dir), y0, yb);
        blob_y = yb + BLOB_PAL_STEPS;
        if (yb >= PREV_H) {                 // done: the selection's preview is shown
            img_sel    = i;
            blob_shown = i;
            blob_i     = NO_IMG;
        }
        return;
    }
    img_sel    = i;
    blob_shown = NO_IMG;
    blob_i     = NO_IMG;
    rect(PREV_X, PREV_Y, PREV_W, PREV_H, C_PANEL);
    path_of(p, apps[i].dir, "APP.IMG");
    if (apps[i].img && f_open(&f, p, FA_READ) == FR_OK) {
        if (f_read(&f, hdr, sizeof(hdr), &n) != FR_OK || n != sizeof(hdr))
            memset(hdr, 0, sizeof(hdr));
        const unsigned w = hdr[4] | (unsigned) hdr[5] << 8, h = hdr[6] | (unsigned) hdr[7] << 8, nc = hdr[8];
        ok = !memcmp(hdr, "AIMG", 4u) && w && w <= PREV_W && h && h <= PREV_H && nc && nc <= PAL_IMG_NR;
        for (unsigned k = 0; ok && k < nc; k++) {
            ok = f_read(&f, row, 3u, &n) == FR_OK && n == 3u;
            pal[PAL_IMG + k] = (uint32_t) row[0] << 16 | (uint32_t) row[1] << 8 | row[2];
        }
        uint8_t *v = vram + (PREV_Y + (PREV_H - h) / 2u) * SCR_W + PREV_X + (PREV_W - w) / 2u;
        gfx_sync();
        for (unsigned y = 0; ok && y < h; y++, v += SCR_W) {
            ok = f_read(&f, row, w, &n) == FR_OK && n == w;
            for (unsigned x = 0; ok && x < w; x++)
                v[x] = (uint8_t) (PAL_IMG + ((row[x] < nc) ? row[x] : 0u));
        }
        f_close(&f);
    }
    if (ok)
        gfx_pal_changed();
    else
        draw_blob_preview(i);
}

static void draw_list(void)
{
    gfx_glass_off(GL_BIG);
    gfx_glass(GL_LIST, LIST_X - PAD, LIST_Y - PAD, ROW_W + 2u * PAD, LIST_ROWS * ROW_H + 2u * PAD);
    for (unsigned i = top; i < napps && i < top + LIST_ROWS; i++)
        draw_row(i);
    draw_desc();
}

// The large panel over the list, the preview and the description
static void draw_big(void)
{
    gfx_glass_off(GL_LIST);
    gfx_glass_off(GL_PREV);
    gfx_glass_off(GL_DESC);
    gfx_glass(GL_BIG, BIG_X, BIG_Y, BIG_W, BIG_H);
}

// The loading screen: the title and the empty progress bar, in the colours
// the launch fade keeps
static void draw_loading(const char *title)
{
    gfx_glass_off(GL_BIG);
    gfx_glass_off(GL_LIST);
    gfx_glass_off(GL_PREV);
    gfx_glass_off(GL_DESC);
    gfx_glass(GL_BIG, LOAD_X, MSG_Y - 14u, LOAD_W, BAR_Y + 10u + 14u - (MSG_Y - 14u));
    text(LOAD_TX, MSG_Y, "Loading", C_LOAD_TEXT, C_PANEL, 36u);
    text(LOAD_TX, MSG_Y + ROW_H, title, C_LOAD_TEXT, C_PANEL, 36u);
    rect(LOAD_TX, BAR_Y, LOAD_BAR_W, 10u, C_LOAD_TODO);
}

// A message in the large panel (lines of at most 56 characters)
static void draw_message(const char *l1, const char *l2, uint8_t colour)
{
    draw_big();
    text(24u, MSG_Y, l1, colour, C_PANEL, 56u);
    if (l2)
        text(24u, MSG_Y + ROW_H, l2, C_DIM, C_PANEL, 56u);
}

//==========================================================
// THE CARD
//==========================================================

// Copy one line of s (up to \r or \n) into d (at most n characters); returns
// the start of the next line
static const char *line(char *d, const char *s, unsigned n)
{
    unsigned i = 0;
    while (*s && *s != '\r' && *s != '\n') {
        if (i < n)
            d[i++] = *s;
        s++;
    }
    d[i] = '\0';
    while (*s == '\r' || *s == '\n')
        s++;
    return s;
}

static void read_txt(app_t *a)
{
    char    p[40], buf[160];
    FIL     f;
    UINT    n = 0;

    path_of(p, a->dir, "APP.TXT");
    if (f_open(&f, p, FA_READ) != FR_OK)
        return;
    if (f_read(&f, buf, sizeof(buf) - 1u, &n) != FR_OK)
        n = 0;
    f_close(&f);
    buf[n] = '\0';
    char t[TITLE_MAX + 1u];
    const char *s = line(t, buf, TITLE_MAX);
    if (t[0])
        strcpy(a->title, t);
    line(a->desc, s, DESC_MAX);
}

// A folder of /APPS that holds programs (a folder with an APP.BIN)
static bool has_programs(const char *dir)
{
    DIR     d;
    FILINFO fi, bi;
    char    p[40];
    bool    found = false;

    path_of(p, dir, "");
    p[strlen(p) - 1u] = '\0';
    if (f_opendir(&d, p) != FR_OK)
        return false;
    const unsigned n = (unsigned) strlen(p);
    while (!found && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (!(fi.fattrib & AM_DIR) || fi.fname[0] == '.')
            continue;
        p[n] = '/';
        strcpy(p + n + 1u, fi.fname);
        strcat(p, "/APP.BIN");
        found = f_stat(p, &bi) == FR_OK && !(bi.fattrib & AM_DIR);
    }
    f_closedir(&d);
    return found;
}

// The entries of the folder shown: in a folder the way back first; then
// the programs in folder name order; at the top the folders of programs
// last. A folder gone (another card) brings the top back.
static int scan(FRESULT *fr)
{
    DIR     d;
    FILINFO fi, bi;
    char    p[40];

    napps = 0;
    strcpy(p, APPS_DIR);
    if (cwd[0]) {
        strcat(p, "/");
        strcat(p, cwd);
    }
    if ((*fr = f_opendir(&d, p)) != FR_OK) {
        if (!cwd[0])
            return ERR_NO_DIR;
        cwd[0] = '\0';
        return scan(fr);
    }
    if (cwd[0]) {
        app_t *a = &apps[napps++];
        memset(a, 0, sizeof(*a));
        strcpy(a->dir, "..");
        strcpy(a->title, "<< Back");
        strcpy(a->desc, "Back to the main list");
        a->kind = K_BACK;
    }
    const unsigned first = napps;
    while (napps < APPS_MAX && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (!(fi.fattrib & AM_DIR) || fi.fname[0] == '.')
            continue;
        path_of(p, fi.fname, "APP.BIN");
        uint8_t kind = K_APP;
        if (f_stat(p, &bi) != FR_OK || (bi.fattrib & AM_DIR)) {
            if (cwd[0] || !has_programs(fi.fname))
                continue;
            kind = K_FOLDER;
            bi.fsize = 0;
        }
        unsigned i = napps++;
        while (i > first && (apps[i - 1u].kind > kind ||
                             (apps[i - 1u].kind == kind && strcmp(apps[i - 1u].dir, fi.fname) > 0))) {
            apps[i] = apps[i - 1u];
            i--;
        }
        app_t *a = &apps[i];
        strcpy(a->dir, fi.fname);
        strcpy(a->title, fi.fname);
        a->desc[0] = '\0';
        a->size = (uint32_t) bi.fsize;
        a->kind = kind;
    }
    f_closedir(&d);
    for (unsigned i = first; i < napps; i++) {
        read_txt(&apps[i]);
        path_of(p, apps[i].dir, "APP.IMG");
        apps[i].img = f_stat(p, &bi) == FR_OK;
        path_of(p, apps[i].dir, "APP.CRC");
        apps[i].crc = apps[i].kind == K_APP && f_stat(p, &bi) == FR_OK;
    }
    return (napps || cwd[0]) ? ERR_NONE : ERR_NO_APPS;
}

// The entry named dir, else the first one after the way back
static void select_dir(const char *dir)
{
    sel = (cwd[0] && napps > 1u) ? 1u : 0u;
    for (unsigned i = 0; i < napps; i++)
        if (!strcmp(apps[i].dir, dir))
            sel = i;
}

// LAST.TXT: the folder of the program started last ("WOLF3D", or
// "DEMOS/LOGO" inside a folder of programs), shown and preselected at
// power-up, then the music, volume and sound output settings; the HDMI and
// the panel builds keep their own output (output_hdmi=, output_panel=)
static char last[96];                       // the file as read or last written

static void last_load(void)
{
    FIL  f;
    UINT n = 0;
    char d[26];

    if (f_open(&f, LAST_FILE, FA_READ) != FR_OK)
        return;
    if (f_read(&f, last, sizeof(last) - 1u, &n) != FR_OK)
        n = 0;
    f_close(&f);
    last[n] = '\0';
    const char *s = line(d, last, 25u);
    char *name = strchr(d, '/');
    if (name && name - d <= 12) {
        FRESULT fr;
        *name++ = '\0';
        strcpy(cwd, d);
        if (scan(&fr) != ERR_NONE) {
            cwd[0] = '\0';
            (void) scan(&fr);
        }
    } else {
        name = d;
    }
    name[12] = '\0';
    for (unsigned i = 0; i < napps; i++)
        if (!strcmp(apps[i].dir, name))
            sel = i;
    // then "music=0|1", "volume=0..8", "output_hdmi=0..2" and
    // "output_panel=1..2" lines: this build's output is applied (one it does
    // not have is ignored), the other build's is kept for last_save
    const char *own = BOARD_HDMI() ? "output_hdmi=" : "output_panel=";
    const char *oth = BOARD_HDMI() ? "output_panel=" : "output_hdmi=";
    char l[16];
    while (*s) {
        s = line(l, s, sizeof(l) - 1u);
        const size_t no = strlen(own), nt = strlen(oth);
        if (!memcmp(l, "music=", 6u) && (l[6] == '0' || l[6] == '1'))
            music_on = l[6] == '1';
        if (!memcmp(l, "volume=", 7u) && l[7] >= '0' && l[7] <= '0' + (int) SYNTH_VOL_MAX)
            volume = (unsigned) (l[7] - '0');
        if (!memcmp(l, own, no) && l[no] >= '0' && l[no] <= '2' && (l[no] != '0' || BOARD_HDMI()))
            aout = (lks_audio_t) (l[no] - '0');
        if (!memcmp(l, oth, nt) && l[nt] >= '0' && l[nt] <= '2')
            aout_other = l[nt];
    }
    synth_volume(volume);
    synth_music(music_on);
    lks_audio_output(&lks, aout);
    draw_volume();                          // the header drew the defaults
}

// Written only when it changes; a card that refuses it (read-only, full) is
// left as it is
static void last_save(const char *dir)
{
    char b[sizeof(last)];
    FIL  f;
    UINT n;

    strcpy(b, cwd);
    strcat(b, cwd[0] ? "/" : "");
    strcat(b, dir);
    strcat(b, music_on ? "\r\nmusic=1\r\nvolume=0\r\n" : "\r\nmusic=0\r\nvolume=0\r\n");
    b[strlen(b) - 3u] = (char) ('0' + volume);
    // this build's output, then the other build's as read
    const char *own = BOARD_HDMI() ? "output_hdmi=0\r\n" : "output_panel=0\r\n";
    strcat(b, own);
    b[strlen(b) - 3u] = (char) ('0' + (aout & 3u));
    if (aout_other) {
        strcat(b, BOARD_HDMI() ? "output_panel=0\r\n" : "output_hdmi=0\r\n");
        b[strlen(b) - 3u] = aout_other;
    }
    if (!strcmp(b, last))
        return;
    if (f_open(&f, LAST_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        return;
    if (f_write(&f, b, strlen(b), &n) == FR_OK && n == strlen(b))
        strcpy(last, b);
    f_close(&f);
}

// APP.CRC: the CRC-32 of APP.BIN, eight hex digits; false without a valid one
static bool crc_read(const app_t *a, uint32_t *crc)
{
    char p[40], b[8];
    FIL  f;
    UINT n = 0;

    path_of(p, a->dir, "APP.CRC");
    if (!a->crc || f_open(&f, p, FA_READ) != FR_OK)
        return false;
    if (f_read(&f, b, sizeof(b), &n) != FR_OK)
        n = 0;
    f_close(&f);
    *crc = 0;
    for (unsigned i = 0; i < n; i++) {
        const char c = b[i];
        const unsigned v = (c >= '0' && c <= '9') ? (unsigned) (c - '0') : (c >= 'A' && c <= 'F') ? (unsigned) (c - 'A' + 10)
                         : (c >= 'a' && c <= 'f') ? (unsigned) (c - 'a' + 10) : 16u;
        if (v > 15u)
            return false;
        *crc = (*crc << 4) | v;
    }
    return n == sizeof(b);
}

// Open a program and hand it to the loader; returns only on an error found
// before the load starts (the menu is still intact then)
static int launch(const app_t *a, FRESULT *fr)
{
    const uint32_t app_max = (uint32_t) _app_max;
    char    p[40];
    FIL     f;

    *fr = FR_OK;
    if (a->size > app_max || a->size < 8u)
        return ERR_SIZE;
    last_save(a->dir);
    path_of(p, a->dir, "APP.BIN");
    if ((*fr = f_open(&f, p, FA_READ)) != FR_OK)
        return ERR_READ;
    const uint32_t size = (uint32_t) f_size(&f);
    if (size > app_max || size < 8u) {
        f_close(&f);
        return ERR_SIZE;
    }
    menu.size = size;

    // The CRC mismatch message, drawn in F1 for the loader to copy
    loader_check_t check = { 0 };
    check.on = crc_read(a, &check.expected);
    menu.crc_expected = check.expected;
    if (check.on) {
        uint8_t *const f0 = vram;
        vram = f0 + FRAME_PX;
        rect(0, MSG_Y, SCR_W, 2u * ROW_H, C_BG);
        text(24u, MSG_Y, err_text[ERR_CRC], C_LOAD_ERR, C_BG, 56u);
        text(24u, MSG_Y + ROW_H, "Not started: reload the menu (mode button)", C_LOAD_ERR, C_BG, 56u);
        vram = f0;
        check.msg   = f0 + FRAME_PX + MSG_Y * SCR_W;
        check.at    = f0 + MSG_Y * SCR_W;
        check.bytes = 2u * ROW_H * SCR_W;
    }

    puts_tty("boot_menu: start ");
    puts_tty(a->dir);
    puts_tty("\n");
    // A jump to hyperspace, then the screen fades out with the confirm
    // sound, but for the title and the empty progress bar (draw_loading);
    // the stars erased, then a quiet board: the audio stopped, the GPU idle,
    // interrupts off; the app sets up the rest
    uint32_t t0;
#ifndef SIMULATION
    gfx_warp(true);
    t0 = ms_now();
    while (ms_now() - t0 < WARP_MS)
        (void) gfx_frame();
#endif
    gfx_fade(0u, FRAMES(14u), true);
    t0 = ms_now();
    while ((synth_sfx_busy() || gfx_fading()) && ms_now() - t0 < SFX_MS)
        (void) gfx_frame();
    gfx_stars(false);
    audio_stop();
    blns_gpu_wait(&blns);
    __asm__ volatile ("csrci mstatus, 8\n\tcsrw mie, x0" ::: "memory");

    const loader_bar_t bar = { vram + BAR_Y * SCR_W + LOAD_TX, LOAD_BAR_W, 10u, C_LOAD_DONE, C_LOAD_TODO };
    loader_run(&f, size, &bar, &check);
}

//==========================================================
// INPUT
//==========================================================

enum { IN_NONE, IN_UP, IN_DOWN, IN_NEXT, IN_RUN, IN_JUMP, IN_INFO, IN_BACK, IN_MUSIC, IN_OUTPUT, IN_VOL_DOWN, IN_VOL_UP };

// Gamepad buttons; the keyboard keys map onto them
#define PAD_UP     USB_BTN_UP
#define PAD_DOWN   USB_BTN_DOWN
#define PAD_RUN    (USB_BTN_A | USB_BTN_START)
#define PAD_MUSIC  USB_BTN_Y
#define PAD_OUTPUT USB_BTN_X
#define PAD_VOL_DN USB_BTN_L
#define PAD_VOL_UP USB_BTN_R
#define PAD_INFO   USB_BTN_SELECT
#define PAD_BACK   USB_BTN_B
#define PAD_ALL    (PAD_UP | PAD_DOWN | PAD_RUN | PAD_MUSIC | PAD_OUTPUT | PAD_VOL_DN | PAD_VOL_UP | PAD_INFO | PAD_BACK)

static uint32_t pad_q, rep_t, key1_t0;
static bool     key1_q, key1_used;
static uint64_t ch_down, ch_q;              // keyboard letters and digits down: A..Z, 1..9, 0
static char     jump_ch;                    // IN_JUMP: the letter or digit pressed

static uint32_t pad_read(void)
{
    uint32_t b = 0;
    ch_down = 0;
    for (unsigned i = 0; i < 2u; i++) {
        (void) usb_hid_service(&usb[i]);
        usb_hid_type_t t = usb_hid_type(&usb[i]);
        if (t == USB_HID_GAMEPAD) {
            b |= usb_hid_buttons(&usb[i]) & PAD_ALL;
        } else if (t == USB_HID_KEYBOARD) {
            uint8_t k[USB_HID_KEYS];
            usb_hid_keys(&usb[i], k);
            for (unsigned j = 0; j < USB_HID_KEYS; j++) {
                if (k[j] == 0x52u) b |= PAD_UP;                         // up arrow
                if (k[j] == 0x51u) b |= PAD_DOWN;                       // down arrow
                if (k[j] == 0x28u || k[j] == 0x58u || k[j] == 0x2Cu)   // Enter, keypad Enter, Space
                    b |= USB_BTN_A;
                if (k[j] == 0x3Au) b |= PAD_INFO;                       // F1
                if (k[j] == 0x29u) b |= PAD_BACK;                       // Esc
                if (k[j] == 0x3Bu) b |= PAD_MUSIC;                      // F2
                if (k[j] == 0x3Cu) b |= PAD_OUTPUT;                     // F3
                if (k[j] == 0x2Du || k[j] == 0x56u) b |= PAD_VOL_DN;    // -, keypad -
                if (k[j] == 0x2Eu || k[j] == 0x57u) b |= PAD_VOL_UP;    // =, keypad +
                if (k[j] >= 0x04u && k[j] <= 0x27u)                     // a..z, 1..9, 0
                    ch_down |= 1ull << (k[j] - 0x04u);
            }
        }
    }
    return b;
}

// Any key, button or KEY1 down (skips the boot animation)
static bool any_input(void)
{
    if (pad_read() || lks_key_pressed(&lks, 1))
        return true;
    for (unsigned i = 0; i < 2u; i++)
        if (usb_hid_type(&usb[i]) == USB_HID_KEYBOARD) {
            uint8_t k[USB_HID_KEYS];
            usb_hid_keys(&usb[i], k);
            for (unsigned j = 0; j < USB_HID_KEYS; j++)
                if (k[j])
                    return true;
        }
    return false;
}

// Take the inputs as they are as the reference (after a time without
// polling): a key or button already down counts only once released
static void input_sync(void)
{
    pad_q     = pad_read();
    ch_q      = ch_down;
    key1_q    = lks_key_pressed(&lks, 1) != 0;
    key1_used = key1_q;
}

static int input(void)
{
    const uint32_t now = ms_now(), b = pad_read(), press = b & ~pad_q;
    int r = IN_NONE;

    if (press & PAD_RUN) {
        r = IN_RUN;
    } else if (press & (PAD_INFO | PAD_BACK)) {
        r = (press & PAD_INFO) ? IN_INFO : IN_BACK;
    } else if (press & (PAD_MUSIC | PAD_OUTPUT | PAD_VOL_DN | PAD_VOL_UP)) {
        r = (press & PAD_MUSIC) ? IN_MUSIC : (press & PAD_OUTPUT) ? IN_OUTPUT : (press & PAD_VOL_DN) ? IN_VOL_DOWN : IN_VOL_UP;
    } else if (press & (PAD_UP | PAD_DOWN)) {
        r = (press & PAD_UP) ? IN_UP : IN_DOWN;
        rep_t = now + REPEAT_MS;
    } else if ((b & (PAD_UP | PAD_DOWN)) && (int32_t) (now - rep_t) >= 0) {
        r = (b & PAD_UP) ? IN_UP : IN_DOWN;
        rep_t = now + RATE_MS;
    }
    pad_q = b;

    uint64_t chs = ch_down & ~ch_q;
    ch_q = ch_down;
    if (r == IN_NONE && chs) {
        unsigned n = 0;
        while (!(chs & 1u)) {
            chs >>= 1;
            n++;
        }
        jump_ch = (char) ((n < 26u) ? 'A' + n : (n < 35u) ? '1' + (n - 26u) : '0');
        r = IN_JUMP;
    }

    const bool k1 = lks_key_pressed(&lks, 1) != 0;
    if (k1 && !key1_q) {
        key1_t0 = now;
        key1_used = false;
    }
    if (k1 && !key1_used && now - key1_t0 >= LONG_MS) {
        key1_used = true;
        r = IN_RUN;
    }
    if (!k1 && key1_q && !key1_used)
        r = IN_NEXT;
    key1_q = k1;
    return r;
}

//==========================================================
// MAIN
//==========================================================

static void set_state(uint32_t state, uint32_t led)
{
    menu.state = state;
    lks_led_set(&lks, led);
}

// An error for 2 s, then the caller retries
static void error_wait(int err, FRESULT fr)
{
    char code[24] = "FatFs result ";
    menu.error = (uint32_t) err;
    menu.fresult = (uint32_t) fr;
    set_state(4, 0x20u | (uint32_t) err);
    puts_tty("boot_menu: ");
    puts_tty(err_text[err]);
    puts_tty("\n");
    if (fr != FR_OK) {
        code[13] = (char) ('0' + fr / 10);
        code[14] = (char) ('0' + fr % 10);
        code[15] = '\0';
    }
    draw_message(err_text[err], (fr != FR_OK) ? code : "Retrying...", C_ERROR);
    const uint32_t t0 = ms_now();
    while (ms_now() - t0 < 2000u)
        poll();
}

static void choose(unsigned i)
{
    const unsigned old = sel;
    synth_sfx(SFX_MOVE);
    sel_ms = ms_now();
    sel = i;
    menu.sel = i;
    if (sel < top || sel >= top + LIST_ROWS) {
        top = (sel < top) ? sel : sel + 1u - LIST_ROWS;
        draw_list();
    } else {
        draw_row(old);
        draw_row(sel);
        draw_desc();
    }
    lks_led_set(&lks, 0x80u | (i & 0x3Fu));         // once drawn
}

// Music on / off, the sound output (HDMI, speaker, Pmod; no HDMI in the
// panel build) and the volume: a blip at the new setting
static void settings(int in)
{
    if (in == IN_MUSIC) {
        music_on = !music_on;
        synth_music(music_on);
    } else if (in == IN_OUTPUT) {
        aout = (aout == LKS_AUDIO_SPEAKER) ? LKS_AUDIO_PMOD
             : (aout == LKS_AUDIO_PMOD && BOARD_HDMI()) ? LKS_AUDIO_HDMI : LKS_AUDIO_SPEAKER;
        lks_audio_output(&lks, aout);
    } else if (in == IN_VOL_DOWN && volume > 0u) {
        volume--;
    } else if (in == IN_VOL_UP && volume < SYNTH_VOL_MAX) {
        volume++;
    }
    synth_volume(volume);
    if (in != IN_MUSIC)
        synth_sfx(SFX_MOVE);
    draw_volume();
}

//==========================================================
// SYSTEM INFORMATION PAGE
//==========================================================

static char *add(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = '\0';
    return d;
}

static char *add_num(char *d, uint32_t v)
{
    char t[10];
    unsigned n = 0;
    do { t[n++] = (char) ('0' + v % 10u); v /= 10u; } while (v);
    while (n)
        *d++ = t[--n];
    *d = '\0';
    return d;
}

static char *add_hex(char *d, uint32_t v, unsigned digits)
{
    while (digits--)
        *d++ = "0123456789ABCDEF"[(v >> (4u * digits)) & 0xFu];
    *d = '\0';
    return d;
}

// "33.333": v / 1000 with three decimals
static char *add_milli(char *d, uint32_t v)
{
    d = add_num(d, v / 1000u);
    *d++ = '.';
    v %= 1000u;
    *d++ = (char) ('0' + v / 100u);
    *d++ = (char) ('0' + v / 10u % 10u);
    *d++ = (char) ('0' + v % 10u);
    *d = '\0';
    return d;
}

#define CSR_READER(fn, csr) static uint32_t fn(void) { uint32_t v; __asm__ volatile ("csrr %0, " #csr : "=r" (v)); return v; }
CSR_READER(csr_misa, misa)
CSR_READER(csr_marchid, marchid)
CSR_READER(csr_mimpid, mimpid)
CSR_READER(csr_marv_cfg, 0xFFF)

static void info_line(unsigned row, const char *label, const char *value)
{
    _Static_assert(LIST_Y - 2u + 10u * FONT_H <= DESC_Y, "the information page needs ten lines");
    const unsigned y = LIST_Y - 2u + row * FONT_H;
    text(16u, y, label, C_ACCENT, C_PANEL, 9u);
    text(16u + 9u * FONT_W, y, value, C_TEXT, C_PANEL, 47u);
}

// The core (misa, marchid, mimpid, marv_cfg), clocks, memories, the build,
// the card and the USB devices, in the list area until a key closes it
static void info_page(void)
{
    static const char *const c_ext[] = { "", "Zca", "Zca Zcb", "Zca Zcb Zcmp", "Zca Zcb Zcmp Zcmt" };
    static const char *const b_ext[] = { "", "Zbb", "Zbb Zba", "Zbb Zba Zbs", "Zbb Zba Zbs Zbc" };
    static const char *const mul_t[] = { "none", "1-cycle", "4-cycle", "16-cycle" };
    static const char *const div_t[] = { "none", "12-cycle", "17-cycle", "33-cycle" };
    static const char *const dev_t[] = { "nothing", "enumerating", "keyboard", "mouse", "gamepad", "not supported" };
    const uint32_t misa = csr_misa(), arch = csr_marchid(), imp = csr_mimpid();
    char l[64], *d;

    draw_big();
    d = add(l, (arch == 0x36u) ? "aRVern " : "marchid 0x");
    d = (arch == 0x36u) ? add_num(d, imp >> 24) : add_hex(d, arch, 8u);
    if (arch == 0x36u) {
        d = add(d, ".");
        d = add_num(d, (imp >> 16) & 0xFFu);
        d = add(d, ".");
        d = add_num(d, (imp >> 8) & 0xFFu);
    }
    d = add(d, ", RV32");
    for (const char *e = "IEMACB"; *e; e++)
        if (misa & (1u << (*e - 'A'))) {
            *d++ = *e;
            *d = '\0';
        }
    d = add(d, (misa & (1u << ('S' - 'A'))) ? ", M S U modes" : ", M mode");
    info_line(0, "Core", l);
    if (arch == 0x36u) {
        const uint32_t cfg = csr_marv_cfg();
        const unsigned cl = (cfg >> 24) & 7u, bl = (cfg >> 20) & 7u, pmp = (cfg >> 6) & 3u;
        d = add(l, (cl < 5u) ? c_ext[cl] : "C?");
        d = add(d, (cl && bl) ? ", " : "");
        add(d, (bl < 5u) ? b_ext[bl] : "B?");
        info_line(1, "", l);
        d = add(l, "mul ");
        d = add(d, mul_t[(cfg >> 18) & 3u]);
        d = add(d, ", div ");
        d = add(d, div_t[(cfg >> 16) & 3u]);
        d = add(d, ", PMP ");
        add_num(d, pmp ? 2u << pmp : 0u);
        info_line(2, "", l);
        d = add(l, "HPM ");
        d = add_num(d, (cfg >> 12) & 15u);
        d = add(d, (cfg & 8u) ? ", Zicntr" : "");
        d = add(d, (cfg & 2u) ? ", debug, triggers " : "");
        if (cfg & 2u)
            d = add_num(d, (cfg >> 8) & 15u);
        add(d, (cfg & 1u) ? ", custom CSRs" : "");
        info_line(3, "", l);
    }
    d = add(l, "core and bus ");
    d = add_milli(d, BOARD_HCLK_HZ / 1000u);
    d = add(d, " MHz, mtime ");
    d = add_milli(d, BOARD_MTIME_HZ / 1000u);
    add(d, " MHz");
    info_line(4, "Clock", l);
    d = add(l, "program ");
    d = add_num(d, BOARD_ROM_SIZE / 1024u);
    d = add(d, " KB, data ");
    d = add_num(d, BOARD_SRAM_SIZE / 1024u);
    d = add(d, " KB, video ");
    d = add_num(d, BOARD_VRAM_WORDS / 256u);
    add(d, " KB");
    info_line(5, "Memory", l);
    add(add(l, BOARD_HDMI() ? "HDMI 720 x 480, " : "RGB panel 480 x 272, "), "8 bpp");
    info_line(6, "Display", l);
    d = add(l, __DATE__ " " __TIME__ ", loader ");
    d = add_num(d, (uint32_t) (_loader_end - _loader_start));
    add(d, " bytes");
    info_line(7, "Menu", l);
    d = add_num(l, (fs.n_fatent - 2u) * fs.csize / 2048u);
    d = add(d, (fs.fs_type == FS_FAT32) ? " MB, FAT32, " : (fs.fs_type == FS_FAT16) ? " MB, FAT16, " : " MB, FAT12, ");
    d = add_num(d, fs.csize / 2u);
    add(d, " KB clusters");
    info_line(8, "SD card", l);
    d = l;
    for (unsigned p = 0; p < 2u; p++) {
        apb_usb_host_device_t dv;
        d = add(d, p ? ", J16 " : "J15 ");
        d = add(d, dev_t[port_dev[p]]);
        if (port_dev[p] >= DEV_KEYBOARD && port_dev[p] <= DEV_PAD &&
            apb_usb_host_device(&usb[p].host, &dv) == APB_USB_HOST_OK) {
            d = add(d, " ");
            d = add_hex(d, dv.vid, 4u);
            d = add(d, ":");
            d = add_hex(d, dv.pid, 4u);
        }
    }
    info_line(9, "USB", l);
    text(16u, DESC_Y, "B, Esc or Select: back", C_DIM, C_PANEL, 40u);

    for (;;) {
        poll();
        const int in = input();
        if (link_done || in == IN_INFO || in == IN_BACK || in == IN_RUN || in == IN_NEXT)
            break;
        if (in >= IN_MUSIC)
            settings(in);
    }
}

// Into a folder of programs (dir), or back to the top (""), the entry named
// pick selected; a folder gone shows the top
static void open_folder(const char *dir, const char *pick)
{
    FRESULT fr;
    strcpy(cwd, dir);
    (void) scan(&fr);
    select_dir(pick);
    menu.apps = napps;
    menu.sel  = sel;
    top = (sel >= LIST_ROWS) ? sel + 1u - LIST_ROWS : 0u;
    draw_list();
    draw_panel();
    img_sel = NO_IMG;
    sel_ms  = ms_now() - PREVIEW_MS;
    input_sync();                           // the press that opened it counts no further
    lks_led_set(&lks, 0x80u | (sel & 0x3Fu));
}

// The next program (after the selection, wrapping) whose title starts with c
static void jump(char c)
{
    for (unsigned k = 1; k <= napps; k++) {
        const unsigned i = (sel + k) % napps;
        char t = apps[i].title[0];
        if (t >= 'a' && t <= 'z')
            t = (char) (t - 'a' + 'A');
        if (t == c) {
            if (i != sel)
                choose(i);
            return;
        }
    }
}

int main(void)
{
    gfx_pal_init();
    demo_init();
    sdlink_init();
    aout = lks_audio_output_get(&lks);
    gfx_init();
    (void) audio_init();
    synth_volume(volume);
    usb_hid_init(&usb[0], BOARD_USB0_BASE);
    usb_hid_init(&usb[1], BOARD_USB1_BASE);
    blns_gpu_wait(&blns);
#ifndef SIMULATION
    // A monitor shows the picture first and plays the sound a few seconds
    // later: the link and a silent stream run before the intro starts
    if (BOARD_HDMI()) {
        const uint32_t t0 = ms_now();
        while (ms_now() - t0 < HDMI_LOCK_MS)
            wait_frames(1u);
    }
#endif
    intro(any_input);
    synth_music(music_on);
    gfx_pal_init();
    draw_frame();
    draw_help();
    gfx_stars(true);
    gfx_fade(256u, FRAMES(30u), false);

    for (;;) {
        FRESULT fr;
        int     err;

        link_done = false;
        set_state(0, 0u);
        draw_message("Reading the SD card...", 0, C_TEXT);
        if ((fr = f_mount(&fs, "", 1)) != FR_OK) {
            error_wait(ERR_MOUNT, fr);
            continue;
        }
        set_state(1, 0u);
        if ((err = scan(&fr)) != ERR_NONE) {
            error_wait(err, fr);
            continue;
        }
        menu.apps = napps;
        puts_tty("boot_menu: ");
        for (unsigned i = 0; i < napps; i++) {
            puts_tty(apps[i].dir);
            puts_tty(i + 1u < napps ? " " : "\n");
        }

        synth_music(music_on);
        static bool restored;
        if (!restored) {
            restored = true;
            last_load();
        }
        if (sel >= napps)
            sel = 0;
        top = (sel >= LIST_ROWS) ? sel + 1u - LIST_ROWS : 0u;
        draw_list();
        draw_panel();
        img_sel = NO_IMG;
        sel_ms  = ms_now() - PREVIEW_MS;
        input_sync();
        set_state(2, 0x80u | (sel & 0x3Fu));         // once drawn
        bool     gone = false;
        uint32_t t_probe = ms_now();
        for (;;) {
            poll();
            if (link_done)
                break;
            if (ms_now() - t_probe >= PROBE_MS) {
                t_probe = ms_now();
                if (disk_read(0, (BYTE *) probe_buf, 0, 1) != RES_OK) {
                    gone = true;
                    break;
                }
            }
            // the preview while no key is down (a held KEY1 is timed by
            // the polls, which a preview step would space out)
            if (img_sel != sel && ms_now() - sel_ms >= PREVIEW_MS && !lks_key_pressed(&lks, 1))
                draw_preview(sel);
            int in = input();
            if (in == IN_UP && sel > 0u)
                choose(sel - 1u);
            else if (in == IN_DOWN && sel + 1u < napps)
                choose(sel + 1u);
            else if (in == IN_NEXT)
                choose((sel + 1u < napps) ? sel + 1u : 0u);
            else if (in == IN_JUMP)
                jump(jump_ch);
            else if (in >= IN_MUSIC)
                settings(in);
            else if (in == IN_INFO) {
                info_page();
                draw_list();
                draw_panel();
                img_sel = NO_IMG;
            }
            else if ((in == IN_RUN && apps[sel].kind == K_BACK) || (in == IN_BACK && cwd[0])) {
                char was[13];
                strcpy(was, cwd);
                synth_sfx(SFX_UNPLUG);
                open_folder("", was);
            } else if (in == IN_RUN && apps[sel].kind == K_FOLDER) {
                synth_sfx(SFX_PLUG);
                open_folder(apps[sel].dir, "");
            } else if (in == IN_RUN)
                break;
        }
        if (gone) {
            synth_sfx(SFX_ERROR);
            puts_tty("boot_menu: the card is gone\n");
            continue;
        }
        if (link_done)                      // the card changed over the SD link: read it again
            continue;

        set_state(3, 0x40u);
        synth_music(false);
        synth_sfx(SFX_RUN);
        draw_loading(apps[sel].title);
        err = launch(&apps[sel], &fr);
        synth_sfx(SFX_ERROR);
        error_wait(err, fr);
    }
}
