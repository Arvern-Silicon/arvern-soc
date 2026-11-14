/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      render_music.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Keen 4's AdLib songs as IMA ADPCM files for the Tang port (audio.c): each
 * song's register writes (AUDIO.CK4, Huffman chunks; 4-byte events, register,
 * value, delay in 1/560 s) played once through Omnispeak's OPL2 emulator
 * (omnispeak/src/opl/dbopl.c, GPL-2.0, the DOSBox team's) at the port's
 * 22.04 kHz, then encoded.
 *
 *   render_music DATA_DIR OUT_DIR      -> OUT_DIR/MUSnn.ADP
 *
 * MUSnn.ADP: 'KADP', u32 rate, u32 samples, u32 0, then IMA ADPCM (mono, the
 * low nibble first; predictor and index start at 0).
 *----------------------------------------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbopl.h"

#define RATE     22040u                         /* the port's mixing rate        */
#define TICK_HZ  560u                           /* the music's event clock       */

static uint8_t *load(const char *dir, const char *name, long *size)
{
    char path[512];
    FILE *f;
    uint8_t *b;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (!(f = fopen(path, "rb"))) { fprintf(stderr, "render_music: no %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t) *size);
    if (!b || fread(b, 1, (size_t) *size, f) != (size_t) *size) { fprintf(stderr, "render_music: %s\n", path); exit(1); }
    fclose(f);
    return b;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }

/* The game's Huffman expansion (CAL_HuffExpand): node 254 is the root */
static void huff(const uint8_t *src, long srclen, uint8_t *dst, uint32_t explen, const uint8_t *dict)
{
    unsigned node = 254, bit = 1;
    uint32_t n = 0;
    long i = 0;
    while (n < explen && i < srclen) {
        const unsigned b = src[i];
        const unsigned v = dict[4 * node + 2 * ((b & bit) != 0)] | dict[4 * node + 2 * ((b & bit) != 0) + 1] << 8;
        if (v < 256) {
            dst[n++] = (uint8_t) v;
            node = 254;
        } else {
            node = v - 256;
        }
        if ((bit <<= 1) == 256) { bit = 1; i++; }
    }
}

static const int16_t ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767 };
static const int8_t ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

/* One sample to a nibble, the predictor following the decoder's */
static unsigned ima_encode(int32_t s, int32_t *pred, int32_t *idx)
{
    const int32_t step = ima_step[*idx];
    int32_t diff = s - *pred, d = step >> 3;
    unsigned code = 0;
    if (diff < 0) { code = 8; diff = -diff; }
    if (diff >= step) { code |= 4; diff -= step; d += step; }
    if (diff >= step >> 1) { code |= 2; diff -= step >> 1; d += step >> 1; }
    if (diff >= step >> 2) { code |= 1; d += step >> 2; }
    *pred += (code & 8) ? -d : d;
    if (*pred > 32767) *pred = 32767;
    if (*pred < -32768) *pred = -32768;
    *idx += ima_index[code];
    if (*idx < 0) *idx = 0;
    if (*idx > 88) *idx = 88;
    return code;
}

int main(int argc, char *argv[])
{
    long n_info, n_head, n_dict, n_audio;
    uint8_t *info, *head, *dict, *audio;
    unsigned songs, start;
    if (argc < 3) { fprintf(stderr, "usage: render_music DATA_DIR OUT_DIR\n"); return 1; }
    info  = load(argv[1], "AUDINFOE.CK4", &n_info);
    head  = load(argv[1], "AUDIOHHD.CK4", &n_head);
    dict  = load(argv[1], "AUDIODCT.CK4", &n_dict);
    audio = load(argv[1], "AUDIO.CK4", &n_audio);
    songs = info[0] | info[1] << 8;
    start = info[12] | info[13] << 8;           /* startMusic */
    DBOPL_InitTables();
    for (unsigned s = 0; s < songs; s++) {
        const uint32_t pos = rd32(head + 4 * (start + s)), end = rd32(head + 4 * (start + s + 1));
        const uint32_t explen = rd32(audio + pos);
        uint8_t *track = calloc(explen + 4u, 1);
        uint32_t len, i, samples = 0;
        int32_t pred = 0, idx = 0, acc = 0;
        uint8_t byte = 0;
        int nib = 0;
        Chip chip;
        char path[512];
        FILE *out;
        huff(audio + pos + 4, (long) (end - pos - 4u), track, explen, dict);
        len = track[0] | track[1] << 8;         /* SD_MusicTrack: length, then events */
        if (len > explen - 2u)
            len = explen - 2u;
        snprintf(path, sizeof(path), "%s/MUS%02u.ADP", argv[2], s);
        if (!(out = fopen(path, "wb"))) { fprintf(stderr, "render_music: %s\n", path); return 1; }
        fwrite("KADP\0\0\0\0\0\0\0\0\0\0\0\0", 1, 16, out);
        Chip__Chip(&chip);
        Chip__Setup(&chip, RATE);
        Chip__WriteReg(&chip, 1, 0x20);         /* waveform select on, as the game's reset */
        for (i = 0; i + 4u <= len; i += 4u) {
            const uint8_t *e = track + 2 + i;
            const uint32_t delay = e[2] | e[3] << 8;
            Chip__WriteReg(&chip, e[0], e[1]);
            for (uint32_t t = 0; t < delay; t++) {
                /* RATE / TICK_HZ samples per tick (39.36), the fraction carried */
                Bit32s buf[64];
                uint32_t k = (RATE + (uint32_t) acc) / TICK_HZ;
                acc = (int32_t) ((RATE + (uint32_t) acc) % TICK_HZ);
                while (k) {
                    const uint32_t m = k > 64u ? 64u : k;
                    Chip__GenerateBlock2(&chip, m, buf);
                    for (uint32_t j = 0; j < m; j++) {
                        int32_t v = 2 * buf[j];
                        v = v > 16383 ? 16383 : v < -16384 ? -16384 : v;
                        const unsigned c = ima_encode(v, &pred, &idx);
                        if (nib) { fputc(byte | c << 4, out); nib = 0; } else { byte = (uint8_t) c; nib = 1; }
                        samples++;
                    }
                    k -= m;
                }
            }
        }
        if (nib)
            fputc(byte, out);
        fseek(out, 4, SEEK_SET);
        const uint8_t hdr[8] = { RATE & 255u, (RATE >> 8) & 255u, RATE >> 16, 0,
                                 samples & 255u, (samples >> 8) & 255u, (samples >> 16) & 255u, samples >> 24 };
        fwrite(hdr, 1, 8, out);
        fclose(out);
        printf("MUS%02u.ADP: %u samples (%u s)\n", s, samples, samples / RATE);
        free(track);
    }
    return 0;
}
