/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      gfxcache.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The cache of the level graphics: the 16 x 16 tiles, the masked tiles and
 * the sprites, at 4 bits per pixel in the VRAM where the GPU draws from. A
 * record is read from GFX4BPP.CK4 (tools/make_gfx.py) the first time it is
 * drawn, into the free VRAM above the heap, from the end down; its address
 * goes into ca_graphChunks[] like any loaded chunk. When the space runs out
 * (or the heap needs it) the cache is flushed whole and fills again with what
 * is drawn next: a flush costs one card read per record drawn afterwards.
 * The score box, whose digits the game draws into the sprite, has its own
 * block in the heap and is never flushed.
 *
 * The large planar pictures (vl_tang.c) go there too, converted to 4 bpp
 * the first time they are drawn: a picture only when it fits without a
 * flush and leaves PIC_RESERVE free for the heap; it is forgotten with the
 * tiles and sprites at a flush (which the room it holds can bring on
 * sooner, when the heap grows). A
 * picture is the game's data, never changed, so its chunk number names
 * it: drawn again, even after the game purged and reloaded it, it is the
 * same picture.
 *----------------------------------------------------------------------------*/
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "id_ca.h"
#include "id_vh.h"
#include "ck_act.h"
#include "id_us.h"
#include "tang.h"

#define REC16   128u
#define REC16M  160u
#define GC_PICS 8u                              /* pictures cached at most              */
#define PIC_RESERVE (16u * 1024u)               /* free VRAM a picture leaves the heap  */

static FIL       gf;
static uint32_t  off16, off16m;
static uint32_t *spr_off;                       /* file offset of each sprite (SRAM)    */
static uint8_t  *have;                          /* presence: tiles16, then tiles16m     */
static uint32_t  lo = VRAM_BYTES;               /* the cache: [lo, VRAM_BYTES)          */
static int       scorebox = -1;
static int16_t   pic_chunk[GC_PICS];            /* the cached pictures: chunk, size, address */
static uint32_t  pic_size[GC_PICS], pic_at[GC_PICS];
static unsigned  pics;
static uint8_t  *scorebox_rec;
uint32_t gc_reads, gc_flushes;

uint32_t gc_low(void) { return lo; }

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }

void gc_open(void)
{
    uint8_t h[32];
    UINT n;
    uint32_t offdir, offhave, nh;
    if (f_open(&gf, "GFX4BPP.CK4", FA_READ) != FR_OK || f_read(&gf, h, 32, &n) != FR_OK || n != 32 ||
        memcmp(h, "K4BP", 4) != 0 ||
        (h[6] | h[7] << 8) != ca_gfxInfoE.numTiles16 || (h[8] | h[9] << 8) != ca_gfxInfoE.numTiles16m ||
        (h[10] | h[11] << 8) != ca_gfxInfoE.numSprites)
        Quit("GFX4BPP.CK4 missing or not made from this EGAGRAPH (tools/make_gfx.py)");
    off16 = rd32(h + 12);
    off16m = rd32(h + 16);
    offdir = rd32(h + 20);
    offhave = rd32(h + 24);
    nh = (ca_gfxInfoE.numTiles16 + 7u) / 8u + (ca_gfxInfoE.numTiles16m + 7u) / 8u;
    spr_off = (uint32_t *) malloc(4u * ca_gfxInfoE.numSprites);
    have = (uint8_t *) malloc(nh);
    if (!spr_off || !have)
        Quit("gc_open: out of SRAM");
    f_lseek(&gf, offdir);
    f_read(&gf, spr_off, 4u * ca_gfxInfoE.numSprites, &n);
    f_lseek(&gf, offhave);
    f_read(&gf, have, nh, &n);
    scorebox = CK_CHUNKNUM(SPR_SCOREBOX) - ca_gfxInfoE.offSprites;
}

bool gc_cached(int chunk)
{
    return (chunk >= ca_gfxInfoE.offTiles16 && chunk < ca_gfxInfoE.offTiles16 + ca_gfxInfoE.numTiles16) ||
           (chunk >= ca_gfxInfoE.offTiles16m && chunk < ca_gfxInfoE.offTiles16m + ca_gfxInfoE.numTiles16m) ||
           (chunk >= ca_gfxInfoE.offSprites && chunk < ca_gfxInfoE.offSprites + ca_gfxInfoE.numSprites);
}

static void forget(int first, int n)
{
    const uint8_t *c = arv_vram + lo;
    for (int i = first; i < first + n; i++)
        if ((const uint8_t *) ca_graphChunks[i] >= c && (const uint8_t *) ca_graphChunks[i] < arv_vram + VRAM_BYTES)
            ca_graphChunks[i] = 0;
}

void gc_flush(void)
{
    if (lo == VRAM_BYTES)
        return;
    gfx_wait();                                 /* the GPU may still draw from it */
    forget(ca_gfxInfoE.offTiles16, ca_gfxInfoE.numTiles16);
    forget(ca_gfxInfoE.offTiles16m, ca_gfxInfoE.numTiles16m);
    forget(ca_gfxInfoE.offSprites, ca_gfxInfoE.numSprites);
    lo = VRAM_BYTES;
    pics = 0u;
    gc_flushes++;
}

static void *load(int chunk, uint32_t off, uint32_t n)
{
    UINT got;
    uint8_t *p;
    n = (n + 3u) & ~3u;
    if (lo < vheap_top() + n) {
        gc_flush();
        if (lo < vheap_top() + n)
            Quit("gfxcache: no VRAM left");
    }
    lo -= n;
    p = arv_vram + lo;
    if (f_lseek(&gf, off) != FR_OK || f_read(&gf, p, n, &got) != FR_OK)
        Quit("gfxcache: GFX4BPP.CK4 read error");
    gc_reads++;
    ca_graphChunks[chunk] = p;
    return p;
}

void *gc_tile16(int t)
{
    const int c = ca_gfxInfoE.offTiles16 + t;
    if (ca_graphChunks[c])
        return ca_graphChunks[c];
    if (!(have[t >> 3] & (1u << (t & 7))))
        return NULL;
    return load(c, off16 + (uint32_t) t * REC16, REC16);
}

void *gc_tile16m(int t)
{
    const int c = ca_gfxInfoE.offTiles16m + t;
    const unsigned b = (ca_gfxInfoE.numTiles16 + 7u) / 8u;
    if (ca_graphChunks[c])
        return ca_graphChunks[c];
    if (!(have[b + (t >> 3)] & (1u << (t & 7))))
        return NULL;
    return load(c, off16m + (uint32_t) t * REC16M, REC16M);
}

void *gc_sprite(int s)
{
    const int c = ca_gfxInfoE.offSprites + s;
    VH_SpriteTableEntry e;
    uint32_t n;
    UINT got;
    if (ca_graphChunks[c])
        return ca_graphChunks[c];
    if (!spr_off[s])
        return NULL;
    e = VH_GetSpriteTableEntry(s);
    n = 5u * e.width * e.height;
    if (s != scorebox)
        return load(c, spr_off[s], n);
    if (!scorebox_rec) {                        /* its own block, never flushed */
        scorebox_rec = (uint8_t *) vheap_alloc(n);
        if (!scorebox_rec || f_lseek(&gf, spr_off[s]) != FR_OK || f_read(&gf, scorebox_rec, n, &got) != FR_OK)
            Quit("gfxcache: score box");
    }
    ca_graphChunks[c] = scorebox_rec;
    return scorebox_rec;
}

/* The converted picture of a chunk (n bytes): its address, or the room
 * taken for it (*fresh: the caller converts it there), or NULL when it does
 * not fit as the policy above wants */
uint8_t *gc_picture(int chunk, uint32_t n, bool *fresh)
{
    *fresh = false;
    n = (n + 3u) & ~3u;
    for (unsigned i = 0; i < pics; i++)
        if (pic_chunk[i] == chunk && pic_size[i] == n)
            return arv_vram + pic_at[i];
    if (pics == GC_PICS || lo < vheap_top() + n + PIC_RESERVE)
        return NULL;
    lo -= n;
    pic_chunk[pics] = (int16_t) chunk;
    pic_size[pics] = n;
    pic_at[pics++] = lo;
    *fresh = true;
    return arv_vram + lo;
}

/* An 8 x 8 tile (planar EGA, 8 bytes per plane) into the score box at byte
 * column col (8 pixels), line row */
void gc_scorebox_tile8(int s, const uint8_t *t8, int col, int row)
{
    const VH_SpriteTableEntry e = VH_GetSpriteTableEntry(s);
    uint8_t *pix = (uint8_t *) gc_sprite(s) + e.width * e.height;
    const unsigned pitch = 4u * e.width;        /* bytes per line at 4 bpp */
    gfx_wait();
    for (int y = 0; y < 8; y++) {
        uint8_t *d = pix + (unsigned) (row + y) * pitch + (unsigned) col * 4u;
        for (int x = 0; x < 8; x += 2) {
            unsigned v = 0;
            for (int p = 0; p < 4; p++) {
                const unsigned b = t8[p * 8 + y];
                v |= ((b >> (7 - x)) & 1u) << p;
                v |= ((b >> (6 - x)) & 1u) << (p + 4);
            }
            d[x >> 1] = (uint8_t) v;
        }
    }
}
