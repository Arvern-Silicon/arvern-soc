/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      pak.c (prince)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The game's resources from the packs on the card (resources.h):
 *   tables   an open pack's table (16 bytes a resource) in the SRAM heap
 *   images   SDL surfaces of 4 bits per pixel whose pixels are read into the
 *            VRAM cache the first time a blit needs them (PSDL_LazyFetch);
 *            when the cache is full it is emptied whole (a new generation)
 *            and fills again with what is drawn next
 *   raw      read from the card into the caller's buffer
 * The last pack file read stays open. The GPU draws the images from the cache
 * (psdl_tang.c): emptying the cache waits for it first.
 *----------------------------------------------------------------------------*/
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "tang.h"
#include "resources.h"

#define MAX_IMAGES 460

typedef struct {
	uint8_t  file;                  /* pack file */
	uint8_t  used;
	uint16_t gen;                   /* cache generation of s->pixels */
	uint32_t off;
	uint16_t len;
} ref_t;

static SDL_Surface img[MAX_IMAGES];
static ref_t       ref[MAX_IMAGES];
static unsigned    img_free_hint;

static char   pack_names[24][13];
static unsigned pack_count;
static FIL    cur;
static int    cur_file = -1;

static uint32_t cache_top;
static uint16_t cache_gen = 1;
uint32_t pak_fetches, pak_flushes, pak_fetch_bytes, pak_peak;

static int pack_file(const char *dat)
{
	char n[13];
	unsigned i = 0;
	for (; dat[i] && dat[i] != '.' && i < 8; i++)
		n[i] = dat[i];
	memcpy(n + i, ".PAK", 5);
	for (unsigned k = 0; k < pack_count; k++)
		if (!strcmp(pack_names[k], n))
			return (int) k;
	if (pack_count == sizeof(pack_names) / sizeof(pack_names[0]))
		return -1;
	strcpy(pack_names[pack_count], n);
	return (int) pack_count++;
}

static int seek_file(int file, uint32_t off)
{
	if (file != cur_file) {
		if (cur_file >= 0)
			f_close(&cur);
		cur_file = -1;
		if (f_open(&cur, pack_names[file], FA_READ) != FR_OK)
			return -1;
		cur_file = file;
	}
	return f_lseek(&cur, off) == FR_OK ? 0 : -1;
}

static int read_file(int file, uint32_t off, void *buf, uint32_t len)
{
	UINT n;
	if (seek_file(file, off) != 0 || f_read(&cur, buf, len, &n) != FR_OK)
		return -1;
	return (int) n;
}

struct datfile_s *pak_open(const char *dat_name)
{
	uint8_t hdr[8];
	struct datfile_s *df;
	const int file = pack_file(dat_name);
	if (file < 0 || read_file(file, 0, hdr, 8) != 8 || memcmp(hdr, "PPAK", 4) != 0)
		return NULL;
	df = malloc(sizeof(*df));
	if (!df)
		return NULL;
	strncpy(df->filename, dat_name, sizeof(df->filename) - 1);
	df->filename[sizeof(df->filename) - 1] = '\0';
	df->file = (uint8_t) file;
	df->count = (uint16_t) (hdr[4] | hdr[5] << 8);
	df->entries = malloc(df->count * sizeof(struct pak_entry));
	if (!df->entries || read_file(file, 8, df->entries, df->count * 16u) != (int) (df->count * 16u)) {
		free(df->entries);
		free(df);
		return NULL;
	}
	return df;
}

void pak_close(struct datfile_s *df)
{
	if (!df)
		return;
	free(df->entries);
	free(df);
}

const struct pak_entry *pak_find(const struct datfile_s *df, int rid)
{
	for (unsigned i = 0; i < df->count; i++)
		if (df->entries[i].rid == rid)
			return &df->entries[i];
	return NULL;
}

int pak_read_at(int file, uint32_t off, void *buf, uint32_t len)
{
    return read_file(file, off, buf, len);
}

int pak_read(const struct datfile_s *df, const struct pak_entry *e, void *buf, int len)
{
	return read_file(df->file, e->off, buf, (uint32_t) len < e->len ? (uint32_t) len : e->len);
}

/*------------------------------- images -------------------------------------*/

SDL_Surface *pak_image(const struct datfile_s *df, const struct pak_entry *e, int row)
{
	for (unsigned k = 0; k < MAX_IMAGES; k++) {
		const unsigned i = (img_free_hint + k) % MAX_IMAGES;
		if (ref[i].used)
			continue;
		img_free_hint = i + 1;
		ref[i].used = 1;
		ref[i].file = df->file;
		ref[i].off = e->off;
		ref[i].len = (uint16_t) e->len;
		ref[i].gen = 0;
		SDL_Surface *s = &img[i];
		memset(s, 0, sizeof(*s));
		s->flags = PSDL_SURF_EXTERN | PSDL_SURF_NIBBLE | PSDL_SURF_LAZY;
		s->format = &psdl_pixel_format_index8;
		s->w = e->w;
		s->h = e->h;
		s->pitch = (e->w + 1) / 2;
		s->clip_rect.w = e->w;
		s->clip_rect.h = e->h;
		s->refcount = 1;
		s->colorkey = (Uint32) (row * 16);
		s->has_colorkey = SDL_TRUE;
		s->alpha_mod = SDL_ALPHA_OPAQUE;
		return s;
	}
	arv_puts("prince: out of image headers\n");
	return NULL;
}

void pak_free_image(const SDL_Surface *s)
{
	if (s >= img && s < img + MAX_IMAGES)
		ref[s - img].used = 0;
}

void PSDL_LazyFetch(SDL_Surface *s)
{
	ref_t *r;
	if (s < img || s >= img + MAX_IMAGES)
		return;
	r = &ref[s - img];
	if (r->gen == cache_gen)
		return;
	const uint32_t len = (r->len + 3u) & ~3u;
	if (cache_top + len > VRAM_CACHE_SIZE) {
		if (++cache_gen == 0)
			cache_gen = 1;
		cache_top = 0;
		pak_flushes++;
		/* the GPU may still read images of the generation being overwritten
		 * (within a generation each image goes past cache_top, where no queued
		 * command reads) */
		arv_gpu_sync();
	}
	uint8_t *dst = arv_vram + VRAM_CACHE + cache_top;
	if (read_file(r->file, r->off, dst, r->len) != (int) r->len)
		memset(dst, 0, r->len);
	cache_top += len;
	if (cache_top > pak_peak)
		pak_peak = cache_top;
	s->pixels = dst;
	r->gen = cache_gen;
	pak_fetches++;
	pak_fetch_bytes += r->len;
}

void pak_cache_report(void)
{
	arv_printf("pak: %u fetches, %u KB, %u flushes, cache %u KB used, peak %u of %u bytes\n",
	           (unsigned) pak_fetches, (unsigned) (pak_fetch_bytes >> 10), (unsigned) pak_flushes,
	           (unsigned) (cache_top >> 10), (unsigned) pak_peak, (unsigned) VRAM_CACHE_SIZE);
}
