/* Tang platform pieces of the Wolf4SDL port */
#include <stdint.h>
#include "tang_board.h"

typedef uint8_t byte;

/* VRAM layout (272 KB): the displayed frame A and the drawn frame B
 * (320 x 200, 64 KB apart), the page cache (16 slots of 4 KB; with
 * WOLF_STREAM_MUSIC the last two are the audio buffers, arv/audio.c), the
 * big-picture slot (64000 bytes), the two map planes (2 x 8 KB) */
#define VRAM_FRAME_A  0x00000u
#define VRAM_FRAME_B  0x10000u
#define VRAM_CACHE    0x20000u
#ifdef WOLF_STREAM_MUSIC
#define CACHE_SLOTS   14
#define VRAM_MUSIC    (VRAM_CACHE + CACHE_SLOTS * 4096u)
#else
#define CACHE_SLOTS   16
#endif
#define VRAM_BIGPIC   (VRAM_CACHE + 16u * 4096u)
#define VRAM_MAPS     (VRAM_BIGPIC + 64000u)

byte *PM_CacheArena(int *nslots)
{
    *nslots = CACHE_SLOTS;
    return (byte *) (BOARD_VRAM_BASE + VRAM_CACHE);
}

#ifdef WOLF_STREAM_MUSIC
uint8_t *arv_audio_buffers(void)
{
    return (uint8_t *) (BOARD_VRAM_BASE + VRAM_MUSIC);
}
#endif

byte *CA_GrBigSlot(int32_t *size)
{
    *size = 64000;
    return (byte *) (BOARD_VRAM_BASE + VRAM_BIGPIC);
}

uint16_t *CA_MapPlaneMem(int plane)
{
    return (uint16_t *) (BOARD_VRAM_BASE + VRAM_MAPS + (uint32_t) plane * 64u * 64u * 2u);
}

/* Unbuffered stdio: FatFs has its own sector buffer (no 1 KB heap buffer per file) */
#include <stdio.h>
#undef fopen
FILE *arv_fopen(const char *name, const char *mode)
{
    FILE *f = fopen(name, mode);
    if (f)
        setvbuf(f, NULL, _IONBF, 0);
    return f;
}

/* fread on those files: one read() of the whole request (newlib serves an
 * unbuffered stream one byte per read() call, each after a walk flushing every
 * stream) */
#include <unistd.h>
#undef fread
size_t arv_fread(void *ptr, size_t size, size_t n, FILE *f)
{
    int r;
    if (fileno(f) < 3)
        return fread(ptr, size, n, f);
    if (!size || !n)
        return 0;
    r = read(fileno(f), ptr, size * n);
    return (r > 0) ? (size_t) r / size : 0;
}

/* memset in words (newlib-nano's stores one byte per iteration; the 3D view
 * clears the 4 KB spotvis every frame). The attribute keeps GCC from turning
 * the loops back into a memset call. */
#include <stddef.h>
__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memset(void *dst, int c, size_t n)
{
    uint8_t *p = dst;
    const uint32_t w = (uint8_t) c * 0x01010101u;
    while (n && ((uintptr_t) p & 3u)) { *p++ = (uint8_t) c; n--; }
    for (; n >= 16u; n -= 16u, p += 16u) {
        ((uint32_t *) p)[0] = w; ((uint32_t *) p)[1] = w;
        ((uint32_t *) p)[2] = w; ((uint32_t *) p)[3] = w;
    }
    for (; n >= 4u; n -= 4u, p += 4u)
        *(uint32_t *) p = w;
    while (n--)
        *p++ = (uint8_t) c;
    return dst;
}
