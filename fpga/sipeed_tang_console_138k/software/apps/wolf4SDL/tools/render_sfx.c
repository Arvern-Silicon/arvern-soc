/* Host tool: the sound effects as one bank for the card, built offline.
 *
 *     render_sfx VSWAP AUDIOHED AUDIOT OUT.SFX
 *
 * Each of the 87 sounds of the shareware build (LASTSOUND of audiowl6.h) is
 * its digitized version when wolfdigimap (wl_main.c, UPLOAD build) maps one,
 * read from VSWAP as SDL_SetupDigi finds it, else its AdLib effect rendered by
 * Wolf4SDL's OPL emulator (mame/fmopl.c) as SDL_ALPlaySound plays it: the
 * instrument (SDL_AlSetFXInst), then one byte per 140 Hz tick (a frequency
 * with the block's key-on, or a rest), plus a short release tail.
 *
 * OUT.SFX: "WSFX", the number of sounds, the sample rate (7042), a reserved
 * word, then per sound { offset, length, priority (16 bits), source (16 bits:
 * 0 none, 1 digitized, 2 AdLib) } (all little-endian), then the samples: 8-bit
 * unsigned, mono, 7042 Hz (the digitized sounds' own format). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fmopl.h"

#define NUMSOUNDS   87
#define RATE        7042
#define TICK_HZ     140
#define TAIL_MS     60
#define OPL_CLOCK   3579545

/* wolfdigimap of the UPLOAD build: sound number -> digitized index */
static const int digimap[][2] = {
    { 21, 0 }, { 41, 1 }, { 19, 2 }, { 18, 3 }, { 26, 4 }, { 24, 5 }, { 11, 6 }, { 51, 7 }, { 55, 8 },
    { 50, 9 }, { 59, 10 }, { 60, 11 }, { 29, 12 }, { 22, 13 }, { 25, 13 }, { 16, 14 }, { 46, 15 },
    { 56, 20 }, { 58, 21 }, { 61, 22 }, { 72, 32 } };

static uint8_t *load(const char *name, long *size)
{
    FILE *f = fopen(name, "rb");
    uint8_t *b;
    if (!f) { perror(name); exit(1); }
    fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(*size);
    if (fread(b, 1, *size, f) != (size_t) *size) { perror(name); exit(1); }
    fclose(f);
    return b;
}
static unsigned u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
static void put16(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

/* VSWAP: header, page offsets and lengths (as PM_Startup / PM_GetPageSize) */
static uint8_t *vswap;
static long     vsize;
static unsigned chunks, sprstart, sndstart;
static uint32_t page_off(unsigned i) { return u32(vswap + 6 + 4 * i); }
static uint32_t page_size(unsigned i)
{
    if (!page_off(i)) return 0;
    if (i + 1 >= chunks || !page_off(i + 1)) return i + 1 >= chunks ? (uint32_t) (vsize - page_off(i)) : u16(vswap + 6 + 4 * chunks + 2 * i);
    return page_off(i + 1) - page_off(i);
}

/* Digitized sound d: its bytes (SDL_SetupDigi), copied to *out */
static uint32_t digi_sound(unsigned d, uint8_t **out)
{
    const uint8_t *info = vswap + page_off(chunks - 1);
    unsigned ndigi = page_size(chunks - 1) / 4, i, page, last;
    for (i = 0; i < ndigi; i++)
        if (u16(info + 4 * i) >= chunks - 1) { ndigi = i; break; }
    if (d >= ndigi) return 0;
    if (d < ndigi - 1) {
        last = u16(info + 4 * (d + 1));
        last = (last == 0 || last + sndstart > chunks - 1) ? chunks - 1 : last + sndstart;
    } else
        last = chunks - 1;
    uint32_t size = 0, lo = u16(info + 4 * d + 2), n = 0;
    for (page = sndstart + u16(info + 4 * d); page < last; page++)
        size += page_size(page);
    if ((size & 0xFFFF0000u) && (size & 0xFFFFu) < lo) size -= 0x10000u;
    size = (size & 0xFFFF0000u) | lo;
    *out = malloc(size);
    for (page = sndstart + u16(info + 4 * d); page < last && n < size; page++) {
        uint32_t k = page_size(page);
        if (k > size - n) k = size - n;
        memcpy(*out + n, vswap + page_off(page), k);
        n += k;
    }
    return size;
}

/* AdLib effect: header (length, priority, instrument, block) then bytes */
static uint32_t adlib_sound(const uint8_t *chunk, uint32_t clen, uint8_t **out, unsigned *prio)
{
    uint32_t len, i, n = 0, cap, t;
    const uint8_t *in, *data;
    unsigned block;
    if (clen < 23) return 0;
    len = u32(chunk); *prio = u16(chunk + 4);
    in = chunk + 6;                                   /* mChar cChar mScale cScale mAttack cAttack mSus cSus mWave cWave ... */
    block = ((chunk[22] & 7) << 2) | 0x20;
    data = chunk + 23;
    if (len > clen - 23) len = clen - 23;
    if (!(in[6] | in[7])) return 0;                   /* no sustain: SDL_ALPlaySound refuses it */

    if (YM3812Init(1, OPL_CLOCK, RATE)) { fprintf(stderr, "OPL init\n"); exit(1); }
    for (i = 1; i < 0xF6; i++) YM3812Write(0, (int) i, 0);
    YM3812Write(0, 1, 0x20);
    YM3812Write(0, 0xB0, 0);                          /* SDL_ALStopSound */
    YM3812Write(0, 0x20, in[0]); YM3812Write(0, 0x40, in[2]); YM3812Write(0, 0x60, in[4]);
    YM3812Write(0, 0x80, in[6]); YM3812Write(0, 0xE0, in[8]);
    YM3812Write(0, 0x23, in[1]); YM3812Write(0, 0x43, in[3]); YM3812Write(0, 0x63, in[5]);
    YM3812Write(0, 0x83, in[7]); YM3812Write(0, 0xE3, in[9]);
    YM3812Write(0, 0xC0, 0);

    cap = (uint32_t) ((uint64_t) (len + 1) * RATE / TICK_HZ) + RATE * TAIL_MS / 1000 + 64;
    *out = malloc(cap);
    for (t = 0; t <= len; t++) {
        uint32_t k, ns;
        if (t < len) {
            if (data[t]) { YM3812Write(0, 0xA0, data[t]); YM3812Write(0, 0xB0, (int) block); }
            else YM3812Write(0, 0xB0, 0);
        } else
            YM3812Write(0, 0xB0, 0);                  /* the end: key off, then the tail */
        ns = (t < len) ? (uint32_t) (((uint64_t) (t + 1) * RATE) / TICK_HZ - ((uint64_t) t * RATE) / TICK_HZ)
                       : RATE * TAIL_MS / 1000;
        while (ns) {
            int16_t st[2 * 64];
            uint32_t m = ns > 64 ? 64 : ns;
            YM3812UpdateOne(0, st, (int) m);
            for (k = 0; k < m; k++) {
                int v = st[2 * k] / 160 + 128;        /* about the digitized sounds' level */
                (*out)[n++] = (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : v);
            }
            ns -= m;
        }
    }
    YM3812Shutdown();
    return n;
}

int main(int argc, char **argv)
{
    long hsize, tsize;
    uint8_t *hed, *aud, *data[NUMSOUNDS] = { 0 };
    uint32_t len[NUMSOUNDS] = { 0 }, off;
    unsigned prio[NUMSOUNDS] = { 0 }, src[NUMSOUNDS] = { 0 }, s, k, ndig = 0, nadl = 0;
    FILE *f;

    if (argc < 5) { fprintf(stderr, "usage: render_sfx VSWAP AUDIOHED AUDIOT OUT.SFX\n"); return 1; }
    vswap = load(argv[1], &vsize);
    chunks = u16(vswap); sprstart = u16(vswap + 2); sndstart = u16(vswap + 4);
    hed = load(argv[2], &hsize);
    aud = load(argv[3], &tsize);

    for (s = 0; s < NUMSOUNDS; s++) {
        uint32_t a0 = u32(hed + 4 * (NUMSOUNDS + s)), a1 = u32(hed + 4 * (NUMSOUNDS + s + 1));
        uint8_t *tmp;
        if (a1 > a0 + 6) prio[s] = u16(aud + a0 + 4);
        for (k = 0; k < sizeof(digimap) / sizeof(digimap[0]); k++)
            if (digimap[k][0] == (int) s && (len[s] = digi_sound((unsigned) digimap[k][1], &data[s])) != 0) {
                src[s] = 1; ndig++;
                break;
            }
        if (!src[s] && a1 > a0 && (len[s] = adlib_sound(aud + a0, a1 - a0, &tmp, &prio[s])) != 0) {
            data[s] = tmp; src[s] = 2; nadl++;
        }
    }

    f = fopen(argv[4], "wb");
    if (!f) { perror(argv[4]); return 1; }
    fwrite("WSFX", 1, 4, f); put32(f, NUMSOUNDS); put32(f, RATE); put32(f, 0);
    off = 16 + NUMSOUNDS * 12;
    for (s = 0; s < NUMSOUNDS; s++) {
        put32(f, src[s] ? off : 0); put32(f, len[s]); put16(f, prio[s]); put16(f, src[s]);
        off += len[s];
    }
    for (s = 0; s < NUMSOUNDS; s++)
        if (src[s]) fwrite(data[s], 1, len[s], f);
    fclose(f);
    printf("%s: %u digitized, %u AdLib, %u without sound, %u bytes\n", argv[4], ndig, nadl,
           NUMSOUNDS - ndig - nadl, off);
    return 0;
}
