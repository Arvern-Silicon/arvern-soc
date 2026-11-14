/* Host tool: the AdLib music of AUDIOHED/AUDIOT rendered once, offline, by
 * Wolf4SDL's own OPL emulator (mame/fmopl.c) and Wolf4SDL's IMF player timing
 * (SDL_IMFMusicPlayer: register writes at 700 ticks per second), one loop of
 * each song, at the board's I2S rate.
 *
 *     render_music AUDIOHED AUDIOT OUTDIR [RATE]
 *
 * writes OUTDIR/MUSnn.ADP (the card's music, nn = the song number of
 * audiowl6.h) and OUTDIR/MUSnn.WAV (16-bit PCM, to listen to on the host);
 * a song without a note (only in the full game's data) is not written.
 *
 * MUSnn.ADP: "WADP", the sample rate, the number of samples (one loop), a
 * reserved word (all 32-bit little-endian), then IMA ADPCM, mono, 4 bits
 * per sample, the first sample in the low nibble, decoded from predictor 0
 * and step index 0 (also where a loop starts again). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fmopl.h"

#define NUMMUSIC   27                      /* LASTMUSIC of audiowl6.h        */
#define TICK_HZ    700                     /* IMF register-write rate        */
#define OPL_CLOCK  3579545

static const int ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767 };
static const int ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

/* One sample to a nibble; the encoder tracks the decoder's state exactly */
static int ima_encode(int sample, int *pred, int *index)
{
    int step = ima_step[*index], diff = sample - *pred, code = 0, delta = step >> 3;
    if (diff < 0) { code = 8; diff = -diff; }
    if (diff >= step) { code |= 4; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 2; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 1; delta += step; }
    *pred += (code & 8) ? -delta : delta;
    if (*pred > 32767) *pred = 32767;
    if (*pred < -32768) *pred = -32768;
    *index += ima_index[code];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;
    return code;
}

static void put32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v, f); fputc(v >> 8, f); }

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

int main(int argc, char **argv)
{
    long hsize, tsize;
    int rate, m;
    uint8_t *hed, *aud;
    uint32_t *off;
    int nchunks;

    if (argc < 4) { fprintf(stderr, "usage: render_music AUDIOHED AUDIOT OUTDIR [RATE]\n"); return 1; }
    rate = argc > 4 ? atoi(argv[4]) : 21701;
    hed = load(argv[1], &hsize);
    aud = load(argv[2], &tsize);
    off = (uint32_t *) hed;
    nchunks = (int) (hsize / 4) - 1;

    for (m = 0; m < NUMMUSIC; m++) {
        int chunk = nchunks - NUMMUSIC + m;
        uint32_t start = off[chunk], end = off[chunk + 1];
        const uint8_t *p;
        uint32_t len, i, nsamples = 0, cap = 1u << 20, t = 0, next = 0;
        int16_t *pcm;
        char name[512];
        FILE *f;
        int pred = 0, index = 0;

        if (end <= start + 2)
            continue;                                  /* not in this data set */
        p = aud + start;
        len = p[0] | (p[1] << 8);                      /* the song's length in bytes */
        if (len == 0) len = end - start;
        else p += 2;

        if (YM3812Init(1, OPL_CLOCK, rate)) { fprintf(stderr, "OPL init\n"); return 1; }
        for (i = 1; i < 0xF6; i++) YM3812Write(0, (int) i, 0);   /* as SD_Startup (Wolf4SDL's YM3812Write: register, value) */
        YM3812Write(0, 1, 0x20);                       /* WSE = 1 */
        pcm = malloc(cap * sizeof(int16_t));

        /* Wolf4SDL: a command runs when its time is reached; then rate / 700
           samples per tick (the remainder carried, so the length is exact) */
        for (i = 0; i + 4 <= len; ) {
            while (i + 4 <= len && next <= t) {
                YM3812Write(0, p[i], p[i + 1]);
                next = t + (p[i + 2] | (p[i + 3] << 8));
                i += 4;
            }
            {
                uint32_t n = (uint32_t) (((uint64_t) (t + 1) * rate) / TICK_HZ - ((uint64_t) t * rate) / TICK_HZ);
                if (nsamples + n > cap) { cap *= 2; pcm = realloc(pcm, cap * sizeof(int16_t)); }
                int16_t st[2 * 64];                    /* Wolf4SDL's emulator writes stereo */
                uint32_t k;
                YM3812UpdateOne(0, st, (int) n);
                for (k = 0; k < n; k++)
                    pcm[nsamples + k] = st[2 * k];
                nsamples += n;
            }
            t++;
        }
        YM3812Shutdown();
        for (i = 0; i < nsamples && !pcm[i]; i++)
            ;
        if (i == nsamples) {                           /* no note: a song of the full game only */
            printf("MUS%02d: silent, not written\n", m);
            free(pcm);
            continue;
        }

        snprintf(name, sizeof(name), "%s/MUS%02d.ADP", argv[3], m);
        f = fopen(name, "wb");
        if (!f) { perror(name); return 1; }
        fwrite("WADP", 1, 4, f); put32(f, (uint32_t) rate); put32(f, nsamples); put32(f, 0);
        for (i = 0; i < nsamples; i += 2) {
            int lo = ima_encode(pcm[i], &pred, &index);
            int hi = (i + 1 < nsamples) ? ima_encode(pcm[i + 1], &pred, &index) : 0;
            fputc(lo | (hi << 4), f);
        }
        fclose(f);

        snprintf(name, sizeof(name), "%s/MUS%02d.WAV", argv[3], m);
        f = fopen(name, "wb");
        fwrite("RIFF", 1, 4, f); put32(f, 36 + nsamples * 2); fwrite("WAVEfmt ", 1, 8, f);
        put32(f, 16); put16(f, 1); put16(f, 1); put32(f, (uint32_t) rate); put32(f, (uint32_t) rate * 2);
        put16(f, 2); put16(f, 16); fwrite("data", 1, 4, f); put32(f, nsamples * 2);
        for (i = 0; i < nsamples; i++) put16(f, (uint16_t) pcm[i]);
        fclose(f);
        printf("MUS%02d: %u ticks, %.1f s, %u bytes ADPCM\n", m, t, (double) nsamples / rate, 16 + (nsamples + 1) / 2);
        free(pcm);
    }
    return 0;
}
