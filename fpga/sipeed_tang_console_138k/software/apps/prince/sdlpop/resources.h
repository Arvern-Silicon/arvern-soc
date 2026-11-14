/*
 * SPDX-License-Identifier: GPL-3.0-or-later (the Prince of Persia port, see COPYING)
 *
 * The game's resources on the Tang: read from the packs on the card
 * (tools/make_pak.py, one X.PAK per X.DAT) by arv/pak.c, in place of the
 * resources picopop's converter compiled into flash.
 *
 * Opening a DAT reads its pack's table. A sprite set becomes a chtab of 4-bit
 * images whose pixels are fetched into a VRAM cache when first drawn
 * (PSDL_SURF_NIBBLE | PSDL_SURF_LAZY); any other resource is read from the
 * card when the game asks for it.
 */
#ifndef _RESOURCES_H_
#define _RESOURCES_H_

#include <stdint.h>
#include "SDL2/SDL.h"

struct pak_entry {
	uint16_t rid;
	uint8_t  kind;                 /* 0 raw, 1 image */
	uint8_t  row;                  /* palette row of an image */
	uint32_t off, len;
	uint16_t w, h;
};

/* An open pack (what a dat_type points to) */
struct datfile_s {
	char filename[16];             /* the DAT's name, as the game says it */
	uint8_t file;                  /* index in the pack file table */
	uint16_t count;
	struct pak_entry *entries;
};

struct datfile_s *pak_open(const char *dat_name);
void pak_close(struct datfile_s *df);
/* The entry of a resource in one pack, or NULL */
const struct pak_entry *pak_find(const struct datfile_s *df, int rid);
/* Read a raw resource's bytes (at most len) into buf: the number read */
int pak_read(const struct datfile_s *df, const struct pak_entry *e, void *buf, int len);
/* A lazy 4-bit surface for an image entry (freed with pak_free_image) */
SDL_Surface *pak_image(const struct datfile_s *df, const struct pak_entry *e, int row);
void pak_free_image(const SDL_Surface *s);
/* Read len bytes at off of pack file 'file' (an entry's df->file, e->off) */
int pak_read_at(int file, uint32_t off, void *buf, uint32_t len);
void pak_cache_report(void);

#endif /* _RESOURCES_H_ */
