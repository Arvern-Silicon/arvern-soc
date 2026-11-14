/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      mm_tang.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The game's memory manager (Omnispeak's ID_MM interface) on the Tang: its
 * blocks in the VRAM heap, the rest of the VRAM after the displayed frame and
 * the sound buffers, which also holds the surfaces. A block starts with an
 * 8-byte header (size and flags, the owner's pointer and the purge level), so
 * freeing, purging and locking find it at once. Blocks go to a 30 KB heap in
 * the SRAM first (the CPU reads it faster), then to the VRAM's. A heap is a list of blocks
 * in address order, allocated first fit; above its last block the tile and
 * sprite cache grows down from the end of the VRAM, and gives its space up
 * (it is flushed) when the heap needs it. When nothing fits, the purgeable
 * blocks go, the highest purge level first, as the original's did.
 * Arenas (the configuration) are in the SRAM.
 *----------------------------------------------------------------------------*/
#include <string.h>
#include <unistd.h>
#include "id_mm.h"
#include "id_us.h"
#include "tang.h"

#define USED    1u
#define LOCKED  2u
#define SZ(h)   ((h)[0] & ~7u)
#define SRAM_HEAP (30u * 1024u)                 /* the game's blocks in the SRAM first */

typedef uint32_t hdr_t;                         /* [0] size | flags, [1] owner | purge */

typedef struct {
    hdr_t   *lo, *end;                          /* the list: [lo, end)                 */
    uint8_t *limit;                             /* the SRAM's end; the VRAM's: the cache */
} heap_t;

static heap_t  sram, vram;
static int     mm_blocks, mm_purgeable;
static int32_t mm_used;
mm_ptr_t buffer;

static inline hdr_t *next_of(hdr_t *h) { return (hdr_t *) ((uint8_t *) h + SZ(h)); }

uint32_t vheap_top(void) { return (uint32_t) ((uint8_t *) vram.end - arv_vram); }

static uint8_t *limit_of(const heap_t *hp)
{
    return hp == &vram ? arv_vram + gc_low() : hp->limit;
}

/* A block of n bytes (header included, a multiple of 8): first fit, merging
 * free neighbours on the way; else at the end (in the VRAM: below the cache,
 * which is flushed when the heap needs its room) */
static hdr_t *heap_get(heap_t *hp, uint32_t n)
{
    hdr_t *h, *nx;
    for (h = hp->lo; h < hp->end; h = next_of(h)) {
        if (h[0] & USED)
            continue;
        while ((nx = next_of(h)) < hp->end && !(nx[0] & USED))
            h[0] = SZ(h) + SZ(nx);
        if (next_of(h) == hp->end) {            /* the last block: give it back */
            hp->end = h;
            break;
        }
        if (SZ(h) >= n) {
            if (SZ(h) - n >= 16u) {
                nx = (hdr_t *) ((uint8_t *) h + n);
                nx[0] = SZ(h) - n;
                nx[1] = 0;
                h[0] = n;
            }
            h[0] |= USED;
            return h;
        }
    }
    if ((uint8_t *) hp->end + n > limit_of(hp) && hp == &vram)
        gc_flush();
    if ((uint8_t *) hp->end + n > limit_of(hp))
        return NULL;
    h = hp->end;
    h[0] = n | USED;
    hp->end = next_of(h);
    return h;
}

static heap_t *heap_of(const void *p)
{
    if ((const uint8_t *) p >= (const uint8_t *) sram.lo && (const uint8_t *) p < (const uint8_t *) sram.end)
        return &sram;
    if ((const uint8_t *) p >= (const uint8_t *) vram.lo && (const uint8_t *) p < (const uint8_t *) vram.end)
        return &vram;
    return NULL;                                /* not a heap block (a cached tile) */
}

static void heap_put(hdr_t *h)
{
    heap_t *hp = heap_of(h);
    if (hp == &vram)
        gfx_wait();                             /* the GPU may still read it */
    h[0] &= ~(USED | LOCKED);
    h[1] = 0;
    if (next_of(h) == hp->end) {
        hp->end = h;
        while (hp->end > hp->lo) {              /* free blocks before it go too */
            hdr_t *p = hp->lo, *last = NULL;
            for (; p < hp->end; p = next_of(p))
                last = p;
            if (!last || (last[0] & USED))
                break;
            hp->end = last;
        }
    }
}

static hdr_t *hdr_of(const void *p)
{
    return heap_of(p) ? (hdr_t *) p - 2 : NULL;
}

/* A memory manager's block (an owner): not a surface, the score box or a
 * cached tile */
static hdr_t *mm_hdr_of(const void *p)
{
    hdr_t *h = hdr_of(p);
    return (h && (h[1] & ~3u)) ? h : NULL;
}

static bool purge_one(void);

/* Surfaces and the score box: the VRAM only (the GPU draws them), purgeable
 * blocks going if needed; not the memory manager's (no owner) */
void *vheap_alloc(uint32_t size)
{
    hdr_t *h;
    while (!(h = heap_get(&vram, (size + 8u + 7u) & ~7u)))
        if (!purge_one())
            return NULL;
    h[1] = 0;
    return h + 2;
}

void vheap_free(void *p)
{
    hdr_t *h = hdr_of(p);
    if (h)
        heap_put(h);
}

static uint32_t free_in(heap_t *hp)
{
    uint32_t n = (uint32_t) (limit_of(hp) - (uint8_t *) hp->end);
    for (hdr_t *h = hp->lo; h < hp->end; h = next_of(h))
        if (!(h[0] & USED))
            n += SZ(h);
    return n;
}

uint32_t vheap_free_bytes(void) { return free_in(&vram); }

/*------------------------------- ID_MM --------------------------------------*/

void MM_Startup(void)
{
    vram.lo = vram.end = (hdr_t *) (arv_vram + VRAM_HEAP);
    sram.lo = sram.end = (hdr_t *) sbrk(SRAM_HEAP);
    if ((void *) sram.lo == (void *) -1)
        Quit("MM_Startup: out of SRAM");
    sram.limit = (uint8_t *) sram.lo + SRAM_HEAP;
    MM_GetPtr(&buffer, BUFFERSIZE);
}

void MM_Shutdown(void) {}

/* The unlocked purgeable block of the highest level, freed; false: none */
static bool purge_one(void)
{
    hdr_t *best = NULL;
    for (heap_t *hp = &sram; hp; hp = (hp == &sram) ? &vram : NULL)
        for (hdr_t *h = hp->lo; h < hp->end; h = next_of(h))
            if ((h[0] & USED) && !(h[0] & LOCKED) && (h[1] & 3u) && (!best || (h[1] & 3u) > (best[1] & 3u)))
                best = h;
    if (!best)
        return false;
    MM_FreePtr((mm_ptr_t *) (best[1] & ~3u));
    return true;
}

void MM_GetPtr(mm_ptr_t *ptr, unsigned long size)
{
    const uint32_t n = ((uint32_t) size + 8u + 7u) & ~7u;
    hdr_t *h;
    while (!(h = heap_get(&sram, n)) && !(h = heap_get(&vram, n)))
        if (!purge_one()) {
            arv_printf("MM_GetPtr: %u bytes for %x\n", (unsigned) size, (unsigned) (uintptr_t) ptr);
            arv_mem_report("out of memory");
            Quit("MM_GetPtr: Out of Memory!");
        }
    h[1] = (uint32_t) ptr;
    mm_blocks++;
    mm_used += (int32_t) size;
    *ptr = h + 2;
}

void MM_FreePtr(mm_ptr_t *ptr)
{
    hdr_t *h = mm_hdr_of(*ptr);
    *ptr = 0;
    if (!h)
        return;
    if (h[1] & 3u)
        mm_purgeable--;
    mm_blocks--;
    mm_used -= (int32_t) (SZ(h) - 8u);
    heap_put(h);
}

void MM_SetPurge(mm_ptr_t *ptr, int level)
{
    hdr_t *h = mm_hdr_of(*ptr);
    if (!h)
        return;
    if (!(h[1] & 3u) && level)
        mm_purgeable++;
    else if ((h[1] & 3u) && !level)
        mm_purgeable--;
    h[1] = (h[1] & ~3u) | ((uint32_t) level & 3u);
}

void MM_SetLock(mm_ptr_t *ptr, bool lock)
{
    hdr_t *h = mm_hdr_of(*ptr);
    if (h)
        h[0] = lock ? (h[0] | LOCKED) : (h[0] & ~LOCKED);
}

int  MM_UsedMemory(void)      { return mm_used; }
int  MM_UsedBlocks(void)      { return mm_blocks; }
int  MM_PurgableBlocks(void)  { return mm_purgeable; }
long MM_UnusedMemory(void)    { return (long) (free_in(&sram) + free_in(&vram)); }
long MM_TotalFree(void)       { return MM_UnusedMemory(); }
void MM_ShowMemory(void)      { arv_mem_report("MM_ShowMemory"); }
void MM_BombOnError(bool bomb) { (void) bomb; }

void MM_SortMem(void)
{
    for (heap_t *hp = &sram; hp; hp = (hp == &sram) ? &vram : NULL)
        for (hdr_t *h = hp->lo; h < hp->end; ) {
            hdr_t *nx = next_of(h);
            if ((h[0] & USED) && !(h[0] & LOCKED) && (h[1] & 3u)) {
                MM_FreePtr((mm_ptr_t *) (h[1] & ~3u));
                h = hp->lo;                     /* the list may have shrunk */
                continue;
            }
            h = nx;
        }
}

/*------------------------------- arenas (SRAM) ------------------------------*/

struct ID_MM_Arena {
    size_t size;
    size_t currentOffset;
};

/* Taken straight from the break (not malloc) so that MM_ArenaTrim can give
 * the unused end back; at most what the SRAM has left but 4 KB (the script
 * variables ask for 64 KB, more than there is, and use less) */
extern char _heap_end[];

ID_MM_Arena *MM_ArenaCreate(size_t size)
{
    ID_MM_Arena *a;
    const size_t room = (size_t) (_heap_end - arv_heap_brk());
    if (size + 4096u > room)
        size = room > 8192u ? room - 4096u : room;
    size = (size + 7u) & ~7u;
    a = (ID_MM_Arena *) sbrk((ptrdiff_t) size);
    if (a == (void *) -1)
        Quit("MM_ArenaCreate: out of SRAM");
    a->size = size;
    a->currentOffset = sizeof(ID_MM_Arena);
    return a;
}

void MM_ArenaTrim(ID_MM_Arena *a)
{
    const size_t used = (a->currentOffset + 7u) & ~7u;
    if ((char *) a + a->size == (char *) sbrk(0)) {
        sbrk(-(ptrdiff_t) (a->size - used));
        a->size = used;
    }
}

void *MM_ArenaAlloc(ID_MM_Arena *a, size_t size)
{
    uint8_t *p;
    if (a->currentOffset + size >= a->size)
        Quit("Arena is full!");
    p = (uint8_t *) a + a->currentOffset;
    a->currentOffset += size;
    return p;
}

void *MM_ArenaAllocAligned(ID_MM_Arena *a, size_t size, size_t alignment)
{
    a->currentOffset = (a->currentOffset + alignment - 1u) & ~(alignment - 1u);
    return MM_ArenaAlloc(a, size);
}

char *MM_ArenaStrDup(ID_MM_Arena *a, const char *str)
{
    size_t len = strlen(str) + 1u;
    char *s = (char *) MM_ArenaAlloc(a, len);
    memcpy(s, str, len);
    return s;
}

void MM_ArenaReset(ID_MM_Arena *a)   { a->currentOffset = sizeof(ID_MM_Arena); }
void MM_ArenaDestroy(ID_MM_Arena *a) { a->currentOffset = 0; a->size = 0; }

/*------------------------------- report --------------------------------------*/

extern char _heap_start[], _bss_end[], _stack_end[];
#define STACK_MARK 0x57AC57ACu

/* The stack (between the BSS and _stack_end) filled with a mark, below the
 * current stack pointer, so the reports can tell how deep it went; never
 * inlined (an inlined copy's frame address would be its caller's, whose
 * locals lie below it) */
__attribute__((noinline)) void arv_stack_paint(void)
{
    uint32_t *p = (uint32_t *) (((uintptr_t) _bss_end + 3u) & ~3u);
    uint32_t *sp;
    __asm__ volatile ("mv %0, sp" : "=r" (sp));
    sp -= 64;
    while (p < sp)
        *p++ = STACK_MARK;
}

static unsigned stack_used(void)
{
    const uint32_t *p = (const uint32_t *) (((uintptr_t) _bss_end + 3u) & ~3u);
    while (p < (const uint32_t *) _stack_end && *p == STACK_MARK)
        p++;
    return (unsigned) ((const char *) _stack_end - (const char *) p);
}

void arv_mem_report(const char *where)
{
    arv_printf("mem %s at %u ms: stack %u, sram heap %u of %u (blocks free %u), vram heap %u blocks %u, cache from %u, free %u\n",
               where, (unsigned) (arv_mtime() / (arv_time_hz() / 1000u)), stack_used(), (unsigned) (arv_heap_brk() - _heap_start), (unsigned) (_heap_end - _heap_start),
               (unsigned) free_in(&sram), (unsigned) (vheap_top() - VRAM_HEAP), (unsigned) mm_blocks, (unsigned) gc_low(),
               (unsigned) vheap_free_bytes());
#ifdef ARV_MEM_BLOCKS
    for (hdr_t *h = vram.lo; h < vram.end; h = next_of(h))
        if (SZ(h) >= ARV_MEM_BLOCKS)
            arv_printf("  block %x: %u bytes%s%s purge %u, owner %x\n", (unsigned) (uintptr_t) h, (unsigned) SZ(h),
                       (h[0] & USED) ? "" : " free", (h[0] & LOCKED) ? " locked" : "", (unsigned) (h[1] & 3u),
                       (unsigned) (h[1] & ~3u));
#endif
}
