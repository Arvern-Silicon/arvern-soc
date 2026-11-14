/*
 * The blitters. Everything is 8bpp indexed, so a blit is a byte copy with an
 * optional colour-key test - no format conversion, no per-pixel palette lookup.
 *
 * This is the only hot path in picosdl, so the inner loops live in RAM on the
 * Pico rather than being fetched over XIP.
 */
#include "psdl_internal.h"

#ifdef PICO_ON_DEVICE
#include "pico.h"
#define PSDL_HOT __not_in_flash_func
#else
#define PSDL_HOT(f) f
#endif

/* ------------------------------------------------------------- clipping */

/*
 * Resolve a blit request into the source and destination rectangles actually
 * touched, after clipping to both surfaces and to the destination's clip rect.
 * `orig_src` receives the source rectangle before clipping, which the mirrored
 * blit needs in order to work out which source column a clipped destination
 * column corresponds to.
 *
 * Returns 0 if the blit is entirely clipped away.
 */
static int clip_blit(SDL_Surface *src, const SDL_Rect *srcrect,
                     SDL_Surface *dst, const SDL_Rect *dstrect,
                     SDL_Rect *out_src, SDL_Rect *out_dst, SDL_Rect *orig_src)
{
	SDL_Rect sr;
	if (srcrect != NULL) {
		sr = *srcrect;
	} else {
		sr.x = 0; sr.y = 0; sr.w = src->w; sr.h = src->h;
	}

	int dx = dstrect ? dstrect->x : 0;
	int dy = dstrect ? dstrect->y : 0;

	/* Clamp the source rectangle to the source surface. */
	if (sr.x < 0) { sr.w += sr.x; dx -= sr.x; sr.x = 0; }
	if (sr.y < 0) { sr.h += sr.y; dy -= sr.y; sr.y = 0; }
	if (sr.x + sr.w > src->w) sr.w = src->w - sr.x;
	if (sr.y + sr.h > src->h) sr.h = src->h - sr.y;
	if (sr.w <= 0 || sr.h <= 0) return 0;

	*orig_src = sr;

	/* Clip against the destination's clip rect. */
	const SDL_Rect *c = &dst->clip_rect;
	int cl = c->x, ct = c->y, cr = c->x + c->w, cb = c->y + c->h;
	if (cl < 0) cl = 0;
	if (ct < 0) ct = 0;
	if (cr > dst->w) cr = dst->w;
	if (cb > dst->h) cb = dst->h;

	if (dx < cl)          { int d = cl - dx; sr.x += d; sr.w -= d; dx = cl; }
	if (dy < ct)          { int d = ct - dy; sr.y += d; sr.h -= d; dy = ct; }
	if (dx + sr.w > cr)   { sr.w = cr - dx; }
	if (dy + sr.h > cb)   { sr.h = cb - dy; }
	if (sr.w <= 0 || sr.h <= 0) return 0;

	*out_src = sr;
	out_dst->x = dx;
	out_dst->y = dy;
	out_dst->w = sr.w;
	out_dst->h = sr.h;
	return 1;
}

static inline Uint8 *row_of(SDL_Surface *s, int y, int x)
{
	return (Uint8 *)s->pixels + (size_t)y * (size_t)s->pitch + (size_t)x;
}

static inline const Uint8 *crow_of(const SDL_Surface *s, int y, int x)
{
	return (const Uint8 *)s->pixels + (size_t)y * (size_t)s->pitch + (size_t)x;
}


/* ------------------------------------------------- 4bpp sources (NIBBLE) */

#define NIB_COPY PSDL_NIB_COPY
#define NIB_MONO PSDL_NIB_MONO
#define NIB_XOR  PSDL_NIB_XOR

static inline const SDL_Surface *src_ready(SDL_Surface *src)
{
	if (src->flags & PSDL_SURF_LAZY)
		PSDL_LazyFetch(src);
	return src;
}

/* w x h pixels from source column sx (stepping dir = +1 or -1), row sy */
static void PSDL_HOT(blit_nib_cpu)(const SDL_Surface *src, int sx, int sy, int dir,
                               Uint8 *dp, int dpitch, int w, int h,
                               int mode, int keyed, Uint8 color)
{
	const Uint8 base = (Uint8)src->colorkey;
	const Uint8 *sp = (const Uint8 *)src->pixels + (size_t)sy * (size_t)src->pitch;
	for (int y = 0; y < h; ++y) {
		int x0 = sx;
		for (int x = 0; x < w; ++x, x0 += dir) {
			const Uint8 b = sp[x0 >> 1];
			const Uint8 v = (x0 & 1) ? (Uint8)(b >> 4) : (Uint8)(b & 15);
			if (mode == NIB_COPY) {
				if (v || !keyed)
					dp[x] = (Uint8)(base + v);
			} else if (mode == NIB_MONO) {
				if (v)
					dp[x] = color;
			} else {
				dp[x] ^= (Uint8)(base + v);
			}
		}
		sp += src->pitch;
		dp += dpitch;
	}
}

/* ------------------------------------------- the GPU, or the CPU loops */

/*
 * Each operation goes to the backend's GPU when it takes it (all its pixels in
 * GPU memory), else to the CPU loop after psdl_backend_cpu_touch() (the CPU
 * must not touch pixels the GPU may still be drawing or reading).
 *
 * PSDL_GPU_CHECK (a test build): every operation the GPU takes is also done by
 * the CPU loop on a copy of the destination (the backend's scratch, GPU memory
 * of at least 320 x 200 bytes the game does not use meanwhile), then compared
 * with what the GPU drew; psdl_gpu_check_n / _bad count the operations checked
 * and those that differ, per kind (0 nibble, 1 8 bpp blit, 2 fill).
 */
static void blit8_cpu(const Uint8 *sp, int spitch, int sdir, Uint8 *dp, int dpitch,
                      int w, int h, int mode, Uint8 key);

#ifdef PSDL_GPU_CHECK
Uint8 *psdl_backend_check_scratch(void);
void   psdl_backend_gpu_sync(void);
void   psdl_backend_log(const char *fmt, ...);
unsigned psdl_gpu_check_n[3], psdl_gpu_check_bad[3];
unsigned psdl_gpu_check_mode[3][8];          /* checked, per kind and mode (+4: mirrored) */

/* The destination rectangle into the scratch (pitch w): the reference */
static Uint8 *chk_pre(const Uint8 *dp, int dpitch, int w, int h)
{
	Uint8 *ref = psdl_backend_check_scratch();
	psdl_backend_gpu_sync();
	for (int y = 0; y < h; ++y)
		memcpy(ref + (size_t)y * (size_t)w, dp + (size_t)y * (size_t)dpitch, (size_t)w);
	return ref;
}

static void chk_post(int kind, const Uint8 *ref, const Uint8 *dp, int dpitch, int w, int h, int mode)
{
	psdl_backend_gpu_sync();
	psdl_gpu_check_n[kind]++;
	psdl_gpu_check_mode[kind][mode & 7]++;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (ref[(size_t)y * (size_t)w + (size_t)x] != dp[(size_t)y * (size_t)dpitch + (size_t)x]) {
				if (psdl_gpu_check_bad[kind]++ < 8)
					psdl_backend_log("prince: gpu check kind %d mode %d %dx%d differs at (%d,%d): cpu %02x gpu %02x\n",
					                 kind, mode, w, h, x, y, ref[(size_t)y * (size_t)w + (size_t)x],
					                 dp[(size_t)y * (size_t)dpitch + (size_t)x]);
				return;
			}
}
#endif

static void blit_nib(const SDL_Surface *src, int sx, int sy, int dir, Uint8 *dp, int dpitch,
                     int w, int h, int mode, int keyed, Uint8 color)
{
#ifdef PSDL_GPU_CHECK
	{
		Uint8 *ref = chk_pre(dp, dpitch, w, h);
		blit_nib_cpu(src, sx, sy, dir, ref, w, w, h, mode, keyed, color);
		if (psdl_backend_gpu_nib(src, sx, sy, dir, dp, dpitch, w, h, mode, keyed, color)) {
			chk_post(0, ref, dp, dpitch, w, h,
			         (mode == NIB_COPY ? (keyed ? 0 : 3) : mode) + 4 * (dir < 0));
			return;
		}
	}
#else
	if (psdl_backend_gpu_nib(src, sx, sy, dir, dp, dpitch, w, h, mode, keyed, color))
		return;
#endif
	psdl_backend_cpu_touch(src->pixels, dp);
	blit_nib_cpu(src, sx, sy, dir, dp, dpitch, w, h, mode, keyed, color);
}

/* An 8 bpp blit (mode PSDL_G_*): sp is the first source pixel read, sdir the
 * source step along a row (-1: mirrored) */
static void blit8(const Uint8 *sp, int spitch, int sdir, Uint8 *dp, int dpitch,
                  int w, int h, int mode, Uint8 key)
{
#ifdef PSDL_GPU_CHECK
	{
		Uint8 *ref = chk_pre(dp, dpitch, w, h);
		blit8_cpu(sp, spitch, sdir, ref, w, w, h, mode, key);
		if (psdl_backend_gpu_blit(sp, spitch, sdir, dp, dpitch, w, h, mode, key)) {
			chk_post(1, ref, dp, dpitch, w, h, mode + 4 * (sdir < 0));
			return;
		}
	}
#else
	if (psdl_backend_gpu_blit(sp, spitch, sdir, dp, dpitch, w, h, mode, key))
		return;
#endif
	psdl_backend_cpu_touch(sp, dp);
	/* A copy of a surface onto itself (the game's onscreen and offscreen
	 * surfaces share their pixels): every pixel keeps its value, opaque or
	 * keyed, so there is nothing to copy. The backend refuses it as an
	 * overlap; the touch above stays, so the GPU and flip order is the same */
	if (sp == dp && spitch == dpitch && sdir > 0 && mode != PSDL_G_XOR)
		return;
	blit8_cpu(sp, spitch, sdir, dp, dpitch, w, h, mode, key);
}

static void fill8(Uint8 *dp, int dpitch, int w, int h, Uint8 v)
{
#ifdef PSDL_GPU_CHECK
	{
		Uint8 *ref = chk_pre(dp, dpitch, w, h);
		for (int y = 0; y < h; ++y)
			memset(ref + (size_t)y * (size_t)w, v, (size_t)w);
		if (psdl_backend_gpu_fill(dp, dpitch, w, h, v)) {
			chk_post(2, ref, dp, dpitch, w, h, 0);
			return;
		}
	}
#else
	if (psdl_backend_gpu_fill(dp, dpitch, w, h, v))
		return;
#endif
	psdl_backend_cpu_touch(NULL, dp);
	for (int y = 0; y < h; ++y)
		memset(dp + (size_t)y * (size_t)dpitch, v, (size_t)w);
}

/* --------------------------------------------------------------- fill */

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
	if (dst == NULL)
		return -1;

	SDL_Rect r;
	if (rect != NULL) {
		r = *rect;
	} else {
		r.x = 0; r.y = 0; r.w = dst->w; r.h = dst->h;
	}

	const SDL_Rect *c = &dst->clip_rect;
	int x0 = r.x < c->x ? c->x : r.x;
	int y0 = r.y < c->y ? c->y : r.y;
	int x1 = r.x + r.w, y1 = r.y + r.h;
	if (x1 > c->x + c->w) x1 = c->x + c->w;
	if (y1 > c->y + c->h) y1 = c->y + c->h;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > dst->w) x1 = dst->w;
	if (y1 > dst->h) y1 = dst->h;
	if (x1 <= x0 || y1 <= y0)
		return 0;

	fill8(row_of(dst, y0, x0), dst->pitch, x1 - x0, y1 - y0, (Uint8)color);
	return 0;
}

/* -------------------------------------------------------------- blits */

static void PSDL_HOT(blit_rows_opaque)(const Uint8 *sp, int spitch,
                                       Uint8 *dp, int dpitch, int w, int h)
{
	for (int y = 0; y < h; ++y) {
		memcpy(dp, sp, (size_t)w);
		sp += spitch;
		dp += dpitch;
	}
}

static void PSDL_HOT(blit_rows_keyed)(const Uint8 *sp, int spitch,
                                      Uint8 *dp, int dpitch, int w, int h, Uint8 key)
{
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			Uint8 v = sp[x];
			if (v != key)
				dp[x] = v;
		}
		sp += spitch;
		dp += dpitch;
	}
}

static void PSDL_HOT(blit_rows_keyed_offset)(const Uint8 *sp, int spitch,
                                             Uint8 *dp, int dpitch, int w, int h,
                                             Uint8 key, Uint8 offset)
{
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			Uint8 v = sp[x];
			if (v != key)
				dp[x] = (Uint8)(v + offset);
		}
		sp += spitch;
		dp += dpitch;
	}
}

/* The CPU loops of blit8(): the ones each entry point had */
static void PSDL_HOT(blit8_cpu)(const Uint8 *sp, int spitch, int sdir, Uint8 *dp, int dpitch,
                                int w, int h, int mode, Uint8 key)
{
	if (sdir > 0 && mode == PSDL_G_COPY) {
		blit_rows_opaque(sp, spitch, dp, dpitch, w, h);
		return;
	}
	if (sdir > 0 && mode == PSDL_G_KEYED) {
		blit_rows_keyed(sp, spitch, dp, dpitch, w, h, key);
		return;
	}
	for (int y = 0; y < h; ++y) {
		if (mode == PSDL_G_XOR) {
			for (int x = 0; x < w; ++x)
				dp[x] ^= sp[x * sdir];
		} else if (mode == PSDL_G_KEYED) {
			for (int x = 0; x < w; ++x) {
				Uint8 v = sp[x * sdir];
				if (v != key)
					dp[x] = v;
			}
		} else {
			for (int x = 0; x < w; ++x)
				dp[x] = sp[x * sdir];
		}
		sp += spitch;
		dp += dpitch;
	}
}

int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect,
                  SDL_Surface *dst, SDL_Rect *dstrect)
{
	if (src == NULL || dst == NULL)
		return -1;

	SDL_Rect s, d, orig;
	if (!clip_blit(src, srcrect, dst, dstrect, &s, &d, &orig)) {
		if (dstrect != NULL) { dstrect->w = 0; dstrect->h = 0; }
		return 0;
	}

	src_ready(src);
	if (src->flags & PSDL_SURF_NIBBLE) {
		blit_nib(src, s.x, s.y, 1, row_of(dst, d.y, d.x), dst->pitch, s.w, s.h, NIB_COPY,
		         src->has_colorkey, 0);
		if (dstrect != NULL)
			*dstrect = d;
		return 0;
	}
	const Uint8 *sp = crow_of(src, s.y, s.x);
	Uint8       *dp = row_of(dst, d.y, d.x);

	blit8(sp, src->pitch, 1, dp, dst->pitch, s.w, s.h,
	      src->has_colorkey ? PSDL_G_KEYED : PSDL_G_COPY, (Uint8)src->colorkey);

	if (dstrect != NULL)
		*dstrect = d;
	return 0;
}

/*
 * Transparency chosen per call rather than per surface.
 *
 * SDLPoP toggles transparency by calling SDL_SetColorKey on the sprite before
 * each blit. Sprites are const objects in flash here, so that write is not
 * available; the key is baked in and this picks whether to honour it.
 */
int PSDL_BlitTransp(SDL_Surface *src, const SDL_Rect *srcrect,
                    SDL_Surface *dst, SDL_Rect *dstrect, int transparent)
{
	if (src == NULL || dst == NULL)
		return -1;

	SDL_Rect s, d, orig;
	if (!clip_blit(src, srcrect, dst, dstrect, &s, &d, &orig)) {
		if (dstrect != NULL) { dstrect->w = 0; dstrect->h = 0; }
		return 0;
	}

	src_ready(src);
	if (src->flags & PSDL_SURF_NIBBLE) {
		blit_nib(src, s.x, s.y, 1, row_of(dst, d.y, d.x), dst->pitch, s.w, s.h, NIB_COPY,
		         transparent && src->has_colorkey, 0);
		if (dstrect != NULL)
			*dstrect = d;
		return 0;
	}
	const Uint8 *sp = crow_of(src, s.y, s.x);
	Uint8       *dp = row_of(dst, d.y, d.x);

	blit8(sp, src->pitch, 1, dp, dst->pitch, s.w, s.h,
	      (transparent && src->has_colorkey) ? PSDL_G_KEYED : PSDL_G_COPY, (Uint8)src->colorkey);

	if (dstrect != NULL)
		*dstrect = d;
	return 0;
}

/*
 * Draw every non-transparent pixel as one colour, ignoring the source index.
 *
 * This is SDLPoP's method_3_blit_mono, which built a whole ARGB8888 conversion
 * of the sprite to achieve it. On an indexed target it is a keyed blit that
 * writes a constant, and it is how the depth-1 images and the built-in font get
 * drawn in whatever colour the caller wants.
 */
static void PSDL_HOT(blit_rows_mono)(const Uint8 *sp, int spitch,
                                     Uint8 *dp, int dpitch, int w, int h,
                                     Uint8 key, Uint8 color)
{
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			if (sp[x] != key)
				dp[x] = color;
		}
		sp += spitch;
		dp += dpitch;
	}
}

int PSDL_BlitMono(SDL_Surface *src, const SDL_Rect *srcrect,
                  SDL_Surface *dst, SDL_Rect *dstrect, Uint8 color)
{
	if (src == NULL || dst == NULL)
		return -1;

	SDL_Rect s, d, orig;
	if (!clip_blit(src, srcrect, dst, dstrect, &s, &d, &orig)) {
		if (dstrect != NULL) { dstrect->w = 0; dstrect->h = 0; }
		return 0;
	}

	src_ready(src);
	if (src->flags & PSDL_SURF_NIBBLE) {
		blit_nib(src, s.x, s.y, 1, row_of(dst, d.y, d.x), dst->pitch, s.w, s.h, NIB_MONO, 1, color);
		if (dstrect != NULL)
			*dstrect = d;
		return 0;
	}
	psdl_backend_cpu_touch(crow_of(src, s.y, s.x), row_of(dst, d.y, d.x));
	blit_rows_mono(crow_of(src, s.y, s.x), src->pitch,
	               row_of(dst, d.y, d.x), dst->pitch, s.w, s.h,
	               src->has_colorkey ? (Uint8)src->colorkey : 0, color);
	if (dstrect != NULL)
		*dstrect = d;
	return 0;
}

int PSDL_BlitOffset(SDL_Surface *src, const SDL_Rect *srcrect,
                    SDL_Surface *dst, SDL_Rect *dstrect, Uint8 index_offset)
{
	if (src == NULL || dst == NULL)
		return -1;
	if (index_offset == 0)
		return SDL_UpperBlit(src, srcrect, dst, dstrect);

	SDL_Rect s, d, orig;
	if (!clip_blit(src, srcrect, dst, dstrect, &s, &d, &orig)) {
		if (dstrect != NULL) { dstrect->w = 0; dstrect->h = 0; }
		return 0;
	}

	src_ready(src);
	if (src->flags & PSDL_SURF_NIBBLE) {                /* the row already says where */
		blit_nib(src, s.x, s.y, 1, row_of(dst, d.y, d.x), dst->pitch, s.w, s.h, NIB_COPY,
		         src->has_colorkey, 0);
		if (dstrect != NULL)
			*dstrect = d;
		return 0;
	}
	psdl_backend_cpu_touch(crow_of(src, s.y, s.x), row_of(dst, d.y, d.x));
	blit_rows_keyed_offset(crow_of(src, s.y, s.x), src->pitch,
	                       row_of(dst, d.y, d.x), dst->pitch, s.w, s.h,
	                       src->has_colorkey ? (Uint8)src->colorkey : 0xFF,
	                       index_offset);
	if (dstrect != NULL)
		*dstrect = d;
	return 0;
}

/*
 * Mirrored blit. This replaces SDLPoP's hflip(), which allocated a whole
 * surface and blitted into it every time a sprite was drawn facing left -
 * dozens of times a frame. Walking the source backwards costs nothing extra.
 */
int PSDL_BlitMirrored(SDL_Surface *src, const SDL_Rect *srcrect,
                      SDL_Surface *dst, SDL_Rect *dstrect)
{
	if (src == NULL || dst == NULL)
		return -1;

	SDL_Rect s, d, orig;
	if (!clip_blit(src, srcrect, dst, dstrect, &s, &d, &orig)) {
		if (dstrect != NULL) { dstrect->w = 0; dstrect->h = 0; }
		return 0;
	}

	/* Destination column j (from the unclipped start) reads source column
	 * orig.x + orig.w - 1 - j. Clipping advanced us by (s.x - orig.x) columns. */
	int jstart = s.x - orig.x;
	int sx     = orig.x + orig.w - 1 - jstart;

	src_ready(src);
	if (src->flags & PSDL_SURF_NIBBLE) {
		blit_nib(src, sx, s.y, -1, row_of(dst, d.y, d.x), dst->pitch, s.w, s.h, NIB_COPY,
		         src->has_colorkey, 0);
		if (dstrect != NULL)
			*dstrect = d;
		return 0;
	}
	blit8(crow_of(src, s.y, sx), src->pitch, -1, row_of(dst, d.y, d.x), dst->pitch, s.w, s.h,
	      src->has_colorkey ? PSDL_G_KEYED : PSDL_G_COPY, (Uint8)src->colorkey);

	if (dstrect != NULL)
		*dstrect = d;
	return 0;
}

/*
 * XOR blit over palette indices. SDLPoP's blit_xor() built two scratch surfaces
 * per call to get here; the DOS original just XORed indices, which is what the
 * "flash" effect actually wants.
 */
int PSDL_BlitXor(SDL_Surface *src, const SDL_Rect *srcrect,
                 SDL_Surface *dst, SDL_Rect *dstrect)
{
	if (src == NULL || dst == NULL)
		return -1;

	SDL_Rect s, d, orig;
	if (!clip_blit(src, srcrect, dst, dstrect, &s, &d, &orig)) {
		if (dstrect != NULL) { dstrect->w = 0; dstrect->h = 0; }
		return 0;
	}

	src_ready(src);
	if (src->flags & PSDL_SURF_NIBBLE) {
		blit_nib(src, s.x, s.y, 1, row_of(dst, d.y, d.x), dst->pitch, s.w, s.h, NIB_XOR, 0, 0);
		if (dstrect != NULL)
			*dstrect = d;
		return 0;
	}
	blit8(crow_of(src, s.y, s.x), src->pitch, 1, row_of(dst, d.y, d.x), dst->pitch, s.w, s.h,
	      PSDL_G_XOR, 0);

	if (dstrect != NULL)
		*dstrect = d;
	return 0;
}

/* Nearest-neighbour, for completeness. USE_SCALING is off in the target build. */
int SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect,
                   SDL_Surface *dst, SDL_Rect *dstrect)
{
	if (src == NULL || dst == NULL)
		return -1;

	SDL_Rect s = srcrect ? *srcrect : (SDL_Rect){ 0, 0, src->w, src->h };
	SDL_Rect d = dstrect ? *dstrect : (SDL_Rect){ 0, 0, dst->w, dst->h };
	if (s.w <= 0 || s.h <= 0 || d.w <= 0 || d.h <= 0)
		return 0;

	Uint8 key   = (Uint8)src->colorkey;
	int   keyed = src->has_colorkey;

	psdl_backend_cpu_touch(src->pixels, dst->pixels);
	for (int y = 0; y < d.h; ++y) {
		int dy = d.y + y;
		if (dy < dst->clip_rect.y || dy >= dst->clip_rect.y + dst->clip_rect.h)
			continue;
		const Uint8 *sp = crow_of(src, s.y + (y * s.h) / d.h, s.x);
		Uint8       *dp = row_of(dst, dy, 0);
		for (int x = 0; x < d.w; ++x) {
			int dxp = d.x + x;
			if (dxp < dst->clip_rect.x || dxp >= dst->clip_rect.x + dst->clip_rect.w)
				continue;
			Uint8 v = sp[(x * s.w) / d.w];
			if (!keyed || v != key)
				dp[dxp] = v;
		}
	}
	return 0;
}
