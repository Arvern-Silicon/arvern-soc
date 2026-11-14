//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      main.c  (Tang Console 138K picture viewer)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// picture_viewer -- shows the pictures of the microSD card's
// /MEDIA/PICTURES folder in name order: JPEG (baseline and progressive),
// PNG, GIF (animated too), BMP and QOI, each fitted to the 480 x 272 screen
// (RGB565, 16 bits a pixel), centred on black.
//
// Controls (the boot menu's): right / left the next / previous picture, A
// (Space) the slideshow on or off (a picture every 5 s), B (I) the
// information bar (the name, the format and size, the decoding time, the
// picture's number) on or off; KEY[1] short the next picture, held the
// slideshow. An animated GIF plays while it is shown. The LEDs show the
// picture's number ([6:0]) once it is drawn, [7] when it could not be.
//
// Memory: the decoders take their working memory from a block of the SRAM
// and from the program memory after the program image (below the boot
// menu's launch routine); the information bar saves the pixels it covers
// in the VRAM after the frame.
//
// The Belenos GPU clears the frame (a fill), saves and restores the pixels
// under the bar (copies) and draws the text (colour expansions of the font,
// pushed behind the command from the program memory): the same pixels as
// the CPU's. The CPU's own writes into the frame (the decoders' output, the
// bar's darkening) wait for it first (fb_sync).
//----------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "tang_board.h"
#include "led_key_sw.h"
#include "ff.h"
#include "demo_kit.h"
#include "font_mono.h"
#include "usb_hid.h"
#include "img.h"
#include "scale.h"
#include "decoders.h"

#define PIC_DIR      "/MEDIA/PICTURES"
#define MAX_PICS     128u
#define POOL_N       (96u * 1024u)
#define ROM_TOP      (BOARD_ROM_BASE + BOARD_ROM_SIZE - 256u)
#define FB_PX        (SCR_W * SCR_H)
#define BAR_H        18u                    // the information bar's rows (its saved pixels fit the VRAM's spare)
#define BAR_Y        (SCR_H - BAR_H)
#define SLIDE_MS     5000u                  // (the simulation's clock runs 500 times faster)
#define LONG_MS      750u

// Read over a J-Link: the picture shown and its decoding
typedef struct {
    uint32_t magic;                         // "PVWR"
    uint32_t index, count;                  // the picture (from 0) of count
    int32_t  status;                        // its decoder's (img_status_t)
    uint32_t width, height;                 // the file's picture
    uint32_t out_w, out_h;                  // the decoder's rows (a JPEG's reduced IDCT)
    uint32_t decode_ms;
    uint32_t frames;                        // GIF frames drawn
    uint32_t shown;                         // pictures shown since reset
} viewer_t;

volatile viewer_t viewer = { .magic = 0x52575650u };

static FATFS    fs;
static FIL      file;
static char     names[MAX_PICS][13];
static unsigned n_pics;
static uint8_t  pool[POOL_N] __attribute__((aligned(4)));
static fb_t    *fb;
static volatile uint16_t *bar_save;         // the bar's pixels, in the VRAM's spare
static bool     bar_on, slideshow;
extern uint8_t  __rom_end[];

static void say(const char *s)
{
    while (*s)
        lks_tty_putc(&lks, *s++);
}

static void say_num(uint32_t v)
{
    char d[11];
    unsigned n = 0u;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        lks_tty_putc(&lks, d[--n]);
}

static char *put_num(char *p, uint32_t v)
{
    char d[11];
    unsigned n = 0u;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *p++ = d[--n];
    *p = 0;
    return p;
}

//----------------------------------------------------------------------------
// Screen: the frame, text, the information bar
//----------------------------------------------------------------------------

static bool gpu_queued;                     // GPU work queued since the last wait

// Before the CPU writes into the frame (or reads it): the GPU's work done
static void fb_sync(void)
{
    if (gpu_queued) {
        blns_gpu_wait(&blns);
        gpu_queued = false;
    }
}

static bool gpu_done(blns_status_t st)
{
    if (st != BLNS_OK)
        return false;
    gpu_queued = true;
    return true;
}

static void clear(void)
{
    const blns_surf_t d = { blns_vram_pa(&blns, fb), SCR_W, BLNS_SWAP_NONE };
    if (gpu_done(blns_gpu_fill(&blns, &d, SCR_W, SCR_H, 0u, BLNS_ROP_S)))
        return;
    fb_sync();
    for (uint32_t i = 0; i < FB_PX; i++)
        fb[i] = 0;
}

// The GPU's copy of n rows from src to dst (the bar and its saved pixels)
static bool copy_rows(volatile const uint16_t *src, volatile uint16_t *dst, unsigned n)
{
    const blns_surf_t s = { blns_vram_pa(&blns, src), SCR_W, BLNS_SWAP_NONE };
    const blns_surf_t d = { blns_vram_pa(&blns, dst), SCR_W, BLNS_SWAP_NONE };
    return gpu_done(blns_gpu_copy(&blns, &s, &d, SCR_W, (uint16_t) n, BLNS_ROP_S));
}

static const uint8_t *glyph(char ch)
{
    const unsigned c = (unsigned char) ch;
    return font_mono + ((c >= 32u && c <= 126u) ? c - 32u : '?' - 32u) * FONT_H;
}

// The GPU's text: k glyphs side by side as one 1-bit image (a glyph row is a
// byte, its first pixel the least significant bit, as the GPU reads it), h
// rows of them, expanded transparent in colour c
static bool text_gpu(unsigned x, unsigned y, const char *s, uint16_t c, unsigned k, unsigned h)
{
    static uint32_t img[SCR_W / FONT_W * FONT_H / 4u];
    uint8_t *b = (uint8_t *) img;
    const blns_expand_t e = { .width = (uint16_t) (k * FONT_W), .height = (uint16_t) h, .src_pitch = 0u,
                              .depth = BLNS_EXP_1BPP, .opaque = false, .mask = false,
                              .swap = BLNS_SWAP_NONE, .rop = BLNS_ROP_S };
    const blns_pa_t d = blns_vram_pa(&blns, fb + y * SCR_W + x);
    for (unsigned r = 0; r < h; r++)
        for (unsigned i = 0; i < k; i++)
            b[r * k + i] = glyph(s[i])[r];
    return blns_gpu_expand_begin(&blns, &e, d, SCR_W, c, 0u) == BLNS_OK &&
           gpu_done(blns_gpu_expand_imm(&blns, d, img));
}

// A line of text at (x, y), colour c (RGB565), no background
static void text(unsigned x, unsigned y, const char *s, uint16_t c)
{
    unsigned k = 0;
    while (s[k] && x + (k + 1u) * FONT_W <= SCR_W)
        k++;
    if (!k || y >= SCR_H || text_gpu(x, y, s, c, k, (SCR_H - y < FONT_H) ? SCR_H - y : FONT_H))
        return;
    fb_sync();
    for (; *s && x + FONT_W <= SCR_W; s++, x += FONT_W) {
        const unsigned ch = ((unsigned char) *s >= 32u && (unsigned char) *s <= 126u) ? (unsigned char) *s - 32u : '?' - 32u;
        const uint8_t *g = font_mono + ch * FONT_H;
        for (unsigned r = 0; r < FONT_H && y + r < SCR_H; r++) {
            fb_t *d = fb + (y + r) * SCR_W + x;
            for (unsigned b = 0; b < FONT_W; b++)
                if (g[r] & (1u << b))
                    d[b] = c;
        }
    }
}

// The bar: the picture's pixels under it saved (a GPU copy), then darkened
// (the CPU, once the copy is done), then text
static void bar_show(const char *left, const char *right)
{
    fb_t *p = fb + BAR_Y * SCR_W;
    const bool saved = copy_rows(p, bar_save, BAR_H);
    fb_sync();
    for (uint32_t i = 0; i < BAR_H * SCR_W; i++) {
        if (!saved)
            bar_save[i] = p[i];
        p[i] = (uint16_t) ((p[i] >> 2) & 0x39E7u);          // a quarter of each channel
    }
    text(6, BAR_Y + 1u, left, 0xFFFFu);
    text(SCR_W - 6u - (unsigned) strlen(right) * FONT_W, BAR_Y + 1u, right, 0xBDF7u);
}

static void bar_hide(void)
{
    fb_t *p = fb + BAR_Y * SCR_W;
    if (copy_rows(bar_save, p, BAR_H))
        return;
    fb_sync();
    for (uint32_t i = 0; i < BAR_H * SCR_W; i++)
        p[i] = bar_save[i];
}

static void message(const char *a, const char *b)
{
    clear();
    text((SCR_W - (unsigned) strlen(a) * FONT_W) / 2u, SCR_H / 2u - FONT_H, a, 0xFFFFu);
    text((SCR_W - (unsigned) strlen(b) * FONT_W) / 2u, SCR_H / 2u + 4u, b, 0xBDF7u);
}

//----------------------------------------------------------------------------
// Input: the gamepad and keyboard (the boot menu's mapping), KEY[1]
//----------------------------------------------------------------------------

static usb_hid_t usb[2];
static uint32_t  pad_q;

#define PAD_NEXT    USB_BTN_RIGHT
#define PAD_PREV    USB_BTN_LEFT
#define PAD_SLIDE   (USB_BTN_A | USB_BTN_START)
#define PAD_INFO    USB_BTN_B

static uint32_t pad_press(void)
{
    uint32_t b = 0;
    for (unsigned i = 0; i < 2u; i++) {
        (void) usb_hid_service(&usb[i]);
        const usb_hid_type_t t = usb_hid_type(&usb[i]);
        if (t == USB_HID_GAMEPAD) {
            b |= usb_hid_buttons(&usb[i]);
        } else if (t == USB_HID_KEYBOARD) {
            uint8_t k[USB_HID_KEYS];
            usb_hid_keys(&usb[i], k);
            for (unsigned j = 0; j < USB_HID_KEYS; j++) {
                if (k[j] == 0x4Fu) b |= PAD_NEXT;                       // right arrow
                if (k[j] == 0x50u) b |= PAD_PREV;                       // left arrow
                if (k[j] == 0x2Cu || k[j] == 0x28u) b |= USB_BTN_A;     // Space, Enter
                if (k[j] == 0x0Cu) b |= PAD_INFO;                       // I
            }
        }
    }
    const uint32_t press = b & ~pad_q;
    pad_q = b;
    return press;
}

typedef enum { KEY_NONE, KEY_SHORT, KEY_LONG } press_t;
static uint32_t key_t0;
static bool     key_down, key_long;

// A press that begins and ends between two polls (during a decode or a GIF
// frame's drawing) is caught by the key's latched falling edge
static press_t key_poll(void)
{
    const bool edge = (lks_irq_pending(&lks) & LKS_KEY(1)) != 0u;
    if (edge)
        lks_irq_clear(&lks, LKS_KEY(1));
    const bool down = lks_key_pressed(&lks, 1u);
    if (!key_down && (down || edge)) {
        key_t0 = ms_now();
        key_long = false;
        key_down = down;
        return down ? KEY_NONE : KEY_SHORT;
    }
    if (down && !key_long && ms_now() - key_t0 >= LONG_MS) {
        key_long = true;
        return KEY_LONG;
    }
    if (!down && key_down) {
        key_down = false;
        return key_long ? KEY_NONE : KEY_SHORT;
    }
    return KEY_NONE;
}

//----------------------------------------------------------------------------
// The pictures
//----------------------------------------------------------------------------

// The file name's extension, any case: 'j' JPEG, 'p' PNG, 'g' GIF, 'b' BMP, 'q' QOI, else 0
static char kind(const char *n)
{
    const char *d = strrchr(n, '.');
    char e[6] = { 0 };
    if (!d || n[0] == '.' || strlen(d) > 5u)
        return 0;
    for (unsigned i = 0; d[i]; i++)
        e[i] = (char) (d[i] | 0x20);
    return (!strcmp(e, ".jpg") || !strcmp(e, ".jpeg") || !strcmp(e, ".jpe")) ? 'j' : !strcmp(e, ".png") ? 'p'
         : !strcmp(e, ".gif") ? 'g' : !strcmp(e, ".bmp") ? 'b' : !strcmp(e, ".qoi") ? 'q' : 0;
}

static FRESULT scan(void)
{
    DIR     dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, PIC_DIR);
    unsigned n = 0u;
    if (fr != FR_OK)
        return fr;
    while (n < MAX_PICS && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if (!(fno.fattrib & (AM_DIR | AM_HID | AM_SYS)) && kind(fno.fname)) {
            unsigned i = n;
            for (; i > 0u && strcmp(names[i - 1u], fno.fname) > 0; i--)
                memcpy(names[i], names[i - 1u], 13u);
            memcpy(names[i], fno.fname, 13u);
            n++;
        }
    }
    (void) f_closedir(&dir);
    n_pics = n;
    return FR_OK;
}

static unsigned file_read(uint8_t *d, unsigned n)
{
    UINT got;
    return (f_read(&file, d, n, &got) == FR_OK) ? got : 0u;
}

static int file_seek(uint32_t at)
{
    return f_lseek(&file, at) != FR_OK;
}

static img_info_t info;
static char       kind_q;                   // the picture shown's kind
static bool       animated;

static const char *status_text(int e)
{
    return (e == IMG_ERR_READ) ? "cannot read the file" : (e == IMG_ERR_FORMAT) ? "not a valid picture"
         : (e == IMG_ERR_UNSUPP) ? "a variant this viewer does not decode" : (e == IMG_ERR_MEMORY) ? "too large for the memory" : "";
}

// The information bar's two texts
static void bar_texts(char *left, char *right)
{
    char *p = left;
    strcpy(p, names[viewer.index]);
    p += strlen(p);
    strcpy(p, "  ");
    p = put_num(p + 2, info.width);
    *p++ = 'x';
    p = put_num(p, info.height);
    *p++ = ' ';
    strcpy(p, info.format);
    p = right;
    p = put_num(p, viewer.decode_ms);
    strcpy(p, " ms  ");
    p = put_num(p + 5, viewer.index + 1u);
    *p++ = '/';
    p = put_num(p, viewer.count);
}

static void show(unsigned i)
{
    char path[sizeof(PIC_DIR) + 13u] = PIC_DIR "/";
    strcat(path, names[i]);
    viewer.index = i;
    viewer.status = IMG_ERR_READ;
    viewer.frames = 0;
    animated = false;
    memset(&info, 0, sizeof(info));
    clear();
    say("show ");
    say(names[i]);
    if (f_open(&file, path, FA_READ) != FR_OK) {
        say(": cannot open\n");
        message(names[i], "cannot open the file");
        return;
    }
    const img_area_t areas[2] = {
        { pool, POOL_N },
        { (uint8_t *) (((uintptr_t) __rom_end + 3u) & ~(uintptr_t) 3u),
          ROM_TOP - (uint32_t) (((uintptr_t) __rom_end + 3u) & ~(uintptr_t) 3u) }
    };
    img_begin(file_read, file_seek, (uint32_t) f_size(&file), areas, 2u);
    kind_q = kind(names[i]);
    const uint32_t t0 = ms_now();
    unsigned delay = 0;
    const int e = (kind_q == 'j') ? jpeg_decode(&info) : (kind_q == 'p') ? png_decode(&info)
                : (kind_q == 'g') ? gif_open(&info, &delay) : (kind_q == 'b') ? bmp_decode(&info) : qoi_decode(&info);
    viewer.decode_ms = ms_now() - t0;
    viewer.status = e;
    viewer.width = info.width;
    viewer.height = info.height;
    viewer.out_w = info.out_w;
    viewer.out_h = info.out_h;
    viewer.frames = info.frames;
    viewer.shown++;
    say(": ");
    say(info.format);
    say(" ");
    say_num(info.width);
    say("x");
    say_num(info.height);
    say(", ");
    say_num(viewer.decode_ms);
    say(" ms");
    if (e) {
        say(", error ");
        say_num((uint32_t) -e);
        say("\n");
        message(names[i], status_text(e));
        (void) f_close(&file);
        lks_led_set(&lks, 0x80u | ((i + 1u) & 0x7Fu));     // [6:0] the picture, [7] an error
        return;
    }
    say("\n");
    lks_led_set(&lks, (i + 1u) & 0x7Fu);
    animated = (kind_q == 'g') && delay;
    if (!animated)
        (void) f_close(&file);
    if (bar_on) {
        char l[64], r[32];
        bar_texts(l, r);
        bar_show(l, r);
    }
}

int main(void)
{
    demo_init();                                    // Belenos, the ACLINT and the board I/O
    blns_set_mode(&blns, BLNS_BPP_16);
    fb = (fb_t *) (uintptr_t) blns_vram8(&blns);
    bar_save = (volatile uint16_t *) (uintptr_t) (blns_vram8(&blns) + 2u * FB_PX);
    out_target(fb, SCR_W, SCR_H);
    out_sync(fb_sync);
    lks_irq_config(&lks, LKS_KEY(1), LKS_EDGE_FALLING);    // latched, polled (no interrupt)
    usb_hid_init(&usb[0], BOARD_USB0_BASE);
    usb_hid_init(&usb[1], BOARD_USB1_BASE);
    say("picture_viewer\n");

    for (;;) {
        FRESULT fr;
        message("Reading the card...", "");
        if ((fr = f_mount(&fs, "", 1)) != FR_OK || (fr = scan()) != FR_OK || !n_pics) {
            message(fr ? "no card or no " PIC_DIR " folder" : "no picture in " PIC_DIR, "retrying...");
            say("picture_viewer: no pictures\n");
            wait_ms(2000);
            continue;
        }
        viewer.count = n_pics;
        unsigned i = 0;
        show(i);
        uint32_t t_slide = ms_now(), t_frame = ms_now();
        unsigned delay = 0;
        bool next = false, prev = false;
        for (;;) {
            const uint32_t b = pad_press();
            const press_t k = key_poll();
            if ((b & PAD_NEXT) || k == KEY_SHORT) next = true;
            if (b & PAD_PREV) prev = true;
            if ((b & PAD_SLIDE) || k == KEY_LONG) {
                slideshow = !slideshow;
                t_slide = ms_now();
            }
            if ((b & PAD_INFO) && viewer.status == IMG_OK) {
                bar_on = !bar_on;
                if (bar_on) {
                    char l[64], r[32];
                    bar_texts(l, r);
                    bar_show(l, r);
                } else {
                    bar_hide();
                }
            }
            if (slideshow && ms_now() - t_slide >= SLIDE_MS)
                next = true;
            if (next || prev) {
                if (animated) (void) f_close(&file);
                i = next ? (i + 1u) % n_pics : (i ? i - 1u : n_pics - 1u);
                next = prev = false;
                show(i);
                t_slide = t_frame = ms_now();
                delay = 0;
                continue;
            }
            if (animated && ms_now() - t_frame >= delay) {  // the GIF's next frame
                t_frame = ms_now();
                if (bar_on) bar_hide();
                const int e = gif_next(&info, &delay);
                if (bar_on) {
                    char l[64], r[32];
                    bar_texts(l, r);
                    bar_show(l, r);
                }
                viewer.frames = info.frames;
                if (e) {
                    animated = false;
                    (void) f_close(&file);
                }
            }
        }
    }
}
