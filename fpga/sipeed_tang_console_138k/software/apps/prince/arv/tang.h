/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      tang.h (prince)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The Tang platform layer of the Prince of Persia port: what its parts share.
 *
 * VRAM (272 KB), at 8 bits per pixel:
 *   0x00000  displayed frame 0, 320 x 200 (64000 bytes)
 *   0x0FA00  displayed frame 1: the two are shown in turn, the GPU copying
 *            the game's frame into the one not shown and the refresh flipping
 *            to it at its next frame (psdl_tang.c; ARV_PRESENT_COPY: frame 0
 *            only, the copy into it timed against the scan as before)
 *   0x1F400  the frame the game draws in
 *   0x2EE00  the digitised sound that plays (16 KB)
 *   0x32E00  the music ring (8 KB)
 *   0x34E00  the font's glyphs (5 KB: 4111 bytes used)
 *   0x36200  picosdl's surface arena (8 KB: the peels, so that the GPU draws
 *            them too; psdl_tang.c)
 *   0x38200  the image cache (pak.c), to the end (47.5 KB)
 *----------------------------------------------------------------------------*/
#ifndef TANG_H
#define TANG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VRAM_BYTES      (272u * 1024u)
#define FRAME_BYTES     64000u                 /* FRAME_W x FRAME_H */
#define VRAM_FRAME      0x00000u               /* displayed frame 0 */
#define VRAM_FRAME1     (VRAM_FRAME + FRAME_BYTES)   /* displayed frame 1 */
#define VRAM_BACK       (VRAM_FRAME1 + FRAME_BYTES)
#define VRAM_SOUND      (VRAM_BACK + FRAME_BYTES)
#define VRAM_SOUND_SIZE 0x04000u
#define VRAM_MUSIC      (VRAM_SOUND + VRAM_SOUND_SIZE)
#define VRAM_MUSIC_SIZE 0x02000u
#define VRAM_FONT       (VRAM_MUSIC + VRAM_MUSIC_SIZE)
#define VRAM_FONT_SIZE  0x01400u               /* 4111 bytes used (font_pixels_used) */
#define VRAM_ARENA      (VRAM_FONT + VRAM_FONT_SIZE)
#define VRAM_ARENA_SIZE 0x02000u               /* PSDL_ARENA_BYTES (Makefile) */
#define VRAM_CACHE      (VRAM_ARENA + VRAM_ARENA_SIZE)
#define VRAM_CACHE_SIZE (VRAM_BYTES - VRAM_CACHE)
_Static_assert(VRAM_CACHE == 0x38200u, "the VRAM map of the comment above");
#define FRAME_W         320u
#define FRAME_H         200u

extern uint8_t *const arv_vram;

/* board.c (or the simulator's stand-in) */
int      arv_printf(const char *fmt, ...);
void     arv_putc(char c);
void     arv_puts(const char *s);
uint32_t arv_mtime(void);
uint32_t arv_time_hz(void);
void     arv_board_start(void);
void     arv_board_poll(void);                 /* once a frame                         */
void     arv_halt(void) __attribute__((noreturn));
int      arv_chdir(const char *path);
void     arv_palette_set(unsigned first, unsigned n, const uint32_t *rgb);

#define PAD_UP      0x001u
#define PAD_DOWN    0x002u
#define PAD_LEFT    0x004u
#define PAD_RIGHT   0x008u
#define PAD_A       0x010u
#define PAD_B       0x020u
#define PAD_X       0x040u
#define PAD_Y       0x080u
#define PAD_START   0x100u
#define PAD_SELECT  0x200u
extern volatile uint32_t arv_pad_inject;
void     arv_input_poll(void);
uint32_t arv_pad(void);
bool     arv_key_event(uint8_t *usage, uint8_t *mods, bool *down);
uint8_t  arv_key_mods(void);

/* psdl_tang.c: wait until the GPU has done every command queued (it may draw
 * picosdl's blits and fills) */
void     arv_gpu_sync(void);

/* audio.c: the I2S output of picosdl's mixer, the music stream */
void arv_audio_open(unsigned freq);
void arv_audio_pause(int pause_on);
void arv_audio_lock(void);
void arv_audio_unlock(void);
void arv_music_pump(void);
void arv_irq_handler(void (*h)(void));

/* Diagnostics */
void arv_stack_paint(void);
void arv_mem_report(const char *where);
char *arv_heap_brk(void);

#endif
