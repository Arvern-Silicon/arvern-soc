/* Silent SDL_mixer for the host reference build: accepts everything, plays nothing */
#include <SDL.h>
#include "SDL_mixer.h"
int Mix_OpenAudio(int f, Uint16 fmt, int c, int s) { (void)f; (void)fmt; (void)c; (void)s; return 0; }
int Mix_OpenAudioDevice(int f, Uint16 fmt, int c, int s, const char *d, int a) { (void)f; (void)fmt; (void)c; (void)s; (void)d; (void)a; return 0; }
void Mix_CloseAudio(void) {}
int Mix_QuerySpec(int *f, Uint16 *fmt, int *c) { if (f) *f = 44100; if (fmt) *fmt = AUDIO_S16LSB; if (c) *c = 2; return 1; }
int Mix_AllocateChannels(int n) { return n; }
int Mix_ReserveChannels(int n) { return n; }
int Mix_GroupChannels(int a, int b, int t) { (void)a; (void)b; (void)t; return 0; }
int Mix_GroupAvailable(int t) { (void)t; return 0; }
int Mix_GroupOldest(int t) { (void)t; return -1; }
int Mix_PlayChannel(int c, Mix_Chunk *k, int l) { (void)k; (void)l; return c < 0 ? 0 : c; }
int Mix_HaltChannel(int c) { (void)c; return 0; }
int Mix_Playing(int c) { (void)c; return 0; }
int Mix_SetPanning(int c, Uint8 l, Uint8 r) { (void)c; (void)l; (void)r; return 0; }
int Mix_SetPosition(int c, Sint16 a, Uint8 d) { (void)c; (void)a; (void)d; return 0; }
Mix_Chunk *Mix_QuickLoad_WAV(Uint8 *m) { (void)m; return NULL; }
Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *s, int f) { if (s && f) SDL_RWclose(s); return SDL_calloc(1, sizeof(Mix_Chunk)); }
void Mix_FreeChunk(Mix_Chunk *k) { SDL_free(k); }
void Mix_SetPostMix(void (*m)(void *, Uint8 *, int), void *a) { (void)m; (void)a; }
void Mix_HookMusic(void (*m)(void *, Uint8 *, int), void *a) { (void)m; (void)a; }
void Mix_ChannelFinished(void (*f)(int)) { (void)f; }
int Mix_RegisterEffect(int c, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *a) { (void)c; (void)f; (void)d; (void)a; return 0; }
const char *Mix_GetError(void) { return "no audio (host stub)"; }
int Mix_Volume(int c, int v) { (void)c; (void)v; return 0; }

/* Messages to stderr instead of a dialog (headless runs) */
#include <stdio.h>
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *window)
{
    (void)flags; (void)window;
    fprintf(stderr, "%s: %s\n", title, message);
    return 0;
}

/* Headless frame capture: with WOLF_DUMP set, the screen surface is saved as
 * frames/fNNN.bmp about once a second (replaces the presentation, which the
 * dummy video driver does not need) */
#include <stdlib.h>
extern SDL_Surface *screen;
void SDL_RenderPresent(SDL_Renderer *r)
{
    static Uint32 last; static int n;
    (void)r;
    static Uint32 last_t;
    if (getenv("WOLF_PM_TRACE") && SDL_GetTicks() - last_t >= 1000) {
        static uint32_t m0;
        last_t = SDL_GetTicks();
        extern uint32_t PMMisses;
        fprintf(stderr, "t=%us misses/s=%u\n", SDL_GetTicks() / 1000, PMMisses - m0);
        m0 = PMMisses;
    }
    if (getenv("WOLF_DUMP") && screen && SDL_GetTicks() - last >= 1000) {
        char name[64];
        last = SDL_GetTicks();
        snprintf(name, sizeof(name), "frames/f%03d.bmp", n++);
        SDL_SaveBMP(screen, name);
    }
}

/* Page-cache arena for WOLF_PAGE_CACHE: WOLF_PM_SLOTS slots of 4 KB (default
 * 36); the hit / miss counts are printed at exit, and every second with
 * WOLF_PM_TRACE set */
#include <stdint.h>
typedef uint8_t byte;
extern uint32_t PMMisses, PMHits;
static void pm_report(void) { fprintf(stderr, "page cache: %u misses, %u hits\n", PMMisses, PMHits); }
byte *PM_CacheArena(int *nslots)
{
    const char *e = getenv("WOLF_PM_SLOTS");
    *nslots = e ? atoi(e) : 36;
    atexit(pm_report);
    return malloc((size_t) *nslots * 4096);
}

/* Heap accounting (host: -Dfree=wolf_free -Dmalloc=wolf_malloc on the game
 * sources): current and peak bytes, printed at exit */
#include <malloc/malloc.h>
#undef malloc
#undef free
static size_t heap_cur, heap_peak, heap_big;
void *wolf_malloc(size_t n)
{
    static long cap = -1;
    void *p;
    if (cap < 0) { const char *e = getenv("WOLF_HEAP_CAP"); cap = e ? atol(e) : 0; }
    if (cap && heap_cur + n > (size_t) cap) {
        fprintf(stderr, "heap cap: %zu bytes refused (in use %zu)\n", n, heap_cur);
        return NULL;
    }
    p = malloc(n);
    if (p) { heap_cur += malloc_size(p); if (heap_cur > heap_peak) heap_peak = heap_cur; if (n > heap_big) heap_big = n; }
    if (getenv("WOLF_HEAP_LOG") && n >= 2048) fprintf(stderr, "alloc %zu from %p cur %zu\n", n, __builtin_return_address(1), heap_cur);
    return p;
}
void wolf_free(void *p) { if (p) { heap_cur -= malloc_size(p); free(p); } }
static void heap_report(void) { fprintf(stderr, "heap: peak %zu bytes, largest block %zu, at exit %zu\n", heap_peak, heap_big, heap_cur); }
extern int wolf_heap_anchor(void);
__attribute__((constructor)) static void heap_init(void) { atexit(heap_report); if (getenv("WOLF_HEAP_LOG")) fprintf(stderr, "anchor %p\n", (void *) heap_report); }

/* Big-picture slot of WOLF_GR_CACHE (VRAM on the target) */
byte *CA_GrBigSlot(int32_t *size) { static byte *slot; if (!slot) slot = malloc(64000); *size = 64000; return slot; }

/* Map planes of WOLF_PLATFORM_MAPS (VRAM on the target) */
typedef uint16_t word;
word *CA_MapPlaneMem(int plane) { static word planes[2][64 * 64]; return planes[plane]; }
