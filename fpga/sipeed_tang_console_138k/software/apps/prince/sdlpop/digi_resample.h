/*
 * The digi resampler.
 *
 * A digi sound is 8-bit unsigned mono at 8200, 11000 or 14000 Hz, sitting in
 * flash exactly as the DAT holds it. The mixer steps a fixed-point cursor
 * through it at source_rate/output_rate and interpolates, so nothing is
 * expanded and nothing is allocated.
 *
 * It used to be expanded at load time to 16-bit stereo at 44.1 kHz and cached -
 * about 16x, and 1858 KB for the whole game, measured. See TODO.md.
 *
 * This lives in a header so that seg009.c's mixer and picosdl/test/digi_check.c
 * share one definition: the test compares this against the float expansion it
 * replaced, and it can only prove anything if it is testing the real thing.
 *
 * Fixed point is 32.32. Two reasons it is not the more obvious 16.16:
 *
 *   - the interpolation `(s1 - s0) * frac` overflows a 32-bit int, because the
 *     widened samples span the full 16-bit range and frac is 16 bits;
 *   - a 16.16 step truncates, and the mixer *accumulates* it, so the error
 *     compounds over a sound instead of cancelling.
 *
 * On a Cortex-M33 the 64-bit add is one instruction pair and the 32x32->64
 * multiply is a single SMULL, so this is not the expensive choice it looks.
 */
#ifndef PICOPOP_DIGI_RESAMPLE_H
#define PICOPOP_DIGI_RESAMPLE_H

#include <stdint.h>

/* 32.32 source frames per output frame. */
static inline uint64_t digi_resample_step(unsigned source_rate, unsigned out_rate)
{
	if (out_rate == 0) return 0;
	return ((uint64_t)source_rate << 32) / out_rate;
}

/*
 * One output sample for cursor `pos`, or 0 past the end.
 *
 * The byte is widened as `(v | v<<8) - 32768`, which is what the old
 * pre-conversion did, so the two paths are comparable sample for sample.
 */
static inline int digi_resample_sample(const unsigned char *src, int count,
                                       uint64_t pos)
{
	uint64_t idx = pos >> 32;
	if (src == NULL || (int64_t)idx >= count) return 0;

	int s0 = ((src[idx] | (src[idx] << 8)) - 32768);
	if ((int64_t)idx + 1 >= count) return s0;

	int s1 = ((src[idx + 1] | (src[idx + 1] << 8)) - 32768);

	/*
	 * The full 32-bit fraction, and rounded rather than truncated. Using only
	 * the top 16 bits and truncating costs up to 2 LSB, which is worse than
	 * the float path this replaced managed on average; with all 32 and a
	 * round-to-nearest it is within 1 LSB of exact everywhere.
	 */
	uint32_t frac = (uint32_t)(pos & 0xFFFFFFFFu);
	int64_t  delta = (((int64_t)(s1 - s0) * (int64_t)frac) + ((int64_t)1 << 31)) >> 32;
	return s0 + (int)delta;
}

#endif /* PICOPOP_DIGI_RESAMPLE_H */
