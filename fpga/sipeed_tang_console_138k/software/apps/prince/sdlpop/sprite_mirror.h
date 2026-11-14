/*
 * Mirroring a sprite horizontally.
 *
 * Every character sprite in the game is drawn facing left; facing right is the
 * same sprite mirrored (draw_midtable_entry's blit_flip path). This is the one
 * operation in the pixel pipeline that needs a temporary surface, because the
 * source is const in flash and the blitters read it forwards.
 *
 * It lives in a header so picosdl/test/mirror_check.c can exercise the code
 * hflip() actually runs, rather than a restatement of it. That mattered here:
 * the bug this replaced was not in the mirroring arithmetic but in how the old
 * implementation used the API - it called SDL_SetColorKey on the *source* to
 * make its column blits opaque, which is a no-op on a const flash surface, so
 * the unmirrored copy underneath showed through every transparent pixel.
 */
#ifndef PICOPOP_SPRITE_MIRROR_H
#define PICOPOP_SPRITE_MIRROR_H

#include "SDL2/SDL.h"

/*
 * Fill `dst` with the horizontal mirror of `src`. Both must be the same size.
 *
 * PSDL_BlitMirrored honours the source's colour key, so `dst` is filled with
 * that key first: what comes out is then an exact opaque mirror rather than a
 * mirror composited over whatever `dst` happened to hold.
 */
static inline int sprite_mirror_into(SDL_Surface *src, SDL_Surface *dst)
{
	if (src == NULL || dst == NULL) return -1;
	if (src->w != dst->w || src->h != dst->h) return -1;

	Uint32 key = src->has_colorkey ? src->colorkey : 0;
	SDL_FillRect(dst, NULL, key);
	if (PSDL_BlitMirrored(src, NULL, dst, NULL) != 0)
		return -1;
	SDL_SetColorKey(dst, src->has_colorkey ? SDL_TRUE : SDL_FALSE, key);
	return 0;
}

#endif /* PICOPOP_SPRITE_MIRROR_H */
