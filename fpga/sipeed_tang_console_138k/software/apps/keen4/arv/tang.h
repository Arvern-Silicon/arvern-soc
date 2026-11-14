/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      tang.h (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The Tang platform layer of the Keen 4 port: what its parts share.
 *
 * VRAM (272 KB), at 4 bits per pixel:
 *   0x00000  the displayed frame, 320 x 200 (32000 bytes)
 *   0x08000  the sound buffers (8 KB)
 *   0x0A000  the GPU command lists behind a present (4 KB, gfx.c)
 *   0x0B000  the staging ring (2 KB, vl_tang.c): the 1-bit pictures (text)
 *            and the planar ones (the 8 x 8 tiles, the pictures in bands)
 *            in the GPU's form
 *   0x0B800  the VRAM heap, to the end: the game's memory blocks (MM_GetPtr),
 *            its surfaces (the screen, the tile buffer, ...), and above
 *            them, down from the end, the cache of tiles and sprites and of
 *            the large pictures converted to 4 bpp (gfxcache.c)
 *----------------------------------------------------------------------------*/
#ifndef TANG_H
#define TANG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VRAM_BYTES      (272u * 1024u)
#define VRAM_FRAME      0x00000u
#define VRAM_SOUND      0x08000u
#define VRAM_SOUND_SIZE 0x02000u
#define VRAM_LISTS      0x0A000u
#define VRAM_LISTS_SIZE 0x01000u
#define VRAM_STAGE      0x0B000u
#define VRAM_STAGE_SIZE 0x00800u
#define VRAM_HEAP       0x0B800u
#define FRAME_W         320u
#define FRAME_H         200u

extern uint8_t *const arv_vram;                /* the VRAM window (CPU view) */

/* Pixel addresses at 4 bpp (the GPU's) and at 1 bpp, of a CPU pointer */
static inline uint32_t arv_pa4(const void *p) { return (uint32_t) ((const uint8_t *) p - arv_vram) * 2u; }
static inline bool     arv_in_vram(const void *p)
{
    return (const uint8_t *) p >= arv_vram && (const uint8_t *) p < arv_vram + VRAM_BYTES;
}

/* board.c / the simulator's platform: TTY, time, start-up */
int      arv_printf(const char *fmt, ...);     /* to the TTY (libc_tang.c)            */
void     arv_putc(char c);
void     arv_puts(const char *s);
uint32_t arv_mtime(void);                      /* the machine timer, BOARD_MTIME_HZ    */
uint32_t arv_time_hz(void);                    /* the game clock's timer rate          */
void     arv_board_start(void);
void     arv_board_poll(void);                 /* HDMI hot plug, between frames        */
void     arv_halt(void) __attribute__((noreturn));   /* the program stops          */
int      arv_chdir(const char *path);

/* Controls: the gamepads' buttons (PAD_*, both ports ORed, plus those a
 * debugger or the simulation sets in arv_pad_inject), the keyboards' key
 * events (HID usage, released or pressed) and their modifiers */
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

/* Drawing primitives (gfx.c): the GPU, or the CPU (ARV_SOFT_GFX). Addresses
 * are pixel addresses (4 bpp; 1 bpp for a mask), pitches in pixels. The GPU
 * runs behind the CPU: gfx_wait() before the CPU touches what it draws or
 * reads. */
#define GFX_ROP_S   0
#define GFX_ROP_AND 3
#define GFX_ROP_OR  4
#define GFX_BACK_X  1u                         /* walk right to left                   */
#define GFX_BACK_Y  2u                         /* walk bottom to top                   */
void gfx_init(void);
void gfx_copy(uint32_t src, unsigned spitch, uint32_t dst, unsigned dpitch,
              unsigned w, unsigned h, int rop, unsigned back);
void gfx_fill(uint32_t dst, unsigned dpitch, unsigned w, unsigned h, unsigned colour);
void gfx_fill_rop(uint32_t dst, unsigned dpitch, unsigned w, unsigned h, unsigned colour, int rop);
void gfx_expand(uint32_t src1, unsigned spitch, uint32_t dst, unsigned dpitch,
                unsigned w, unsigned h, unsigned colour);
/* A 1 bpp picture: GFX_EXP_SET the colour where set (as gfx_expand),
 * GFX_EXP_OPAQUE the colour where set and 0 where clear, GFX_EXP_XOR the
 * destination XOR the colour where set */
#define GFX_EXP_SET     0
#define GFX_EXP_OPAQUE  1
#define GFX_EXP_XOR     2
void gfx_expand1(uint32_t src1, unsigned spitch, uint32_t dst, unsigned dpitch,
                 unsigned w, unsigned h, unsigned colour, int mode);
/* A masked draw (EGA: the mask clears the shape, the picture is ORed in):
 * src the 4 bpp picture, mask its 1 bpp mask at the same pitch (bit 1: the
 * picture is drawn) */
void gfx_masked(uint32_t src, uint32_t mask, unsigned spitch, uint32_t dst, unsigned dpitch,
                unsigned w, unsigned h);
void gfx_wait(void);
void gfx_palette(const uint32_t rgb[16], uint32_t border);
void gfx_present(uint32_t src, unsigned spitch);  /* 320 x 200 to the displayed frame */

/* The VRAM heap (mm_tang.c) */
void *vheap_alloc(uint32_t size);              /* NULL when it does not fit            */
void  vheap_free(void *p);
uint32_t vheap_top(void);                      /* end of the highest block in use      */
uint32_t vheap_free_bytes(void);

/* The tile and sprite cache (gfxcache.c) */
void  gc_open(void);
void *gc_tile16(int tile);                     /* NULL: no such tile                   */
void *gc_tile16m(int tile);
void *gc_sprite(int sprite);                   /* mask, then pixels                    */
bool  gc_cached(int chunk);                    /* a chunk the cache serves             */
void  gc_flush(void);
uint32_t gc_low(void);                         /* lowest VRAM byte the cache holds     */
uint8_t *gc_picture(int chunk, uint32_t size, bool *fresh);   /* a converted picture */
void  gc_scorebox_tile8(int sprite, const uint8_t *tile8, int col, int row);
extern uint32_t gc_reads, gc_flushes;

/* Video helpers for the patched engine (vl_tang.c) */
void VL_Tang_DrawSprite(int x, int y, int sprite, int colour);   /* colour < 0: the sprite */
void VL_Tang_MaskedTile(void *src, void *surface, int x, int y);
void VL_Tang_Tile(void *src, void *surface, int x, int y);

/* Sound and time (sd_tang.c, audio.c) */
void SD_Tang_Poll(void);
void arv_audio_start(void);
void arv_audio_tone(unsigned divisor);         /* the PC speaker's timer divisor, 0: silent */
void arv_audio_rate(uint32_t hz);              /* the game's timer service rate    */
bool arv_audio_clock(void);                    /* the I2S interrupt runs the service */
void arv_audio_lock(void);
void arv_audio_unlock(void);
void arv_audio_pump(void);
void arv_music_start(int song);
void arv_music_stop(void);
bool arv_music_playing(void);
void arv_music_pause(bool pause);

/* Diagnostics: SRAM heap high-water, VRAM heap use */
void arv_mem_report(const char *where);
void arv_stack_paint(void);
char *arv_heap_brk(void);

#endif
