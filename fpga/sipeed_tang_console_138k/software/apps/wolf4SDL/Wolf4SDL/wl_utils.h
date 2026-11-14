// WL_UTILS.H

#ifndef __WL_UTILS_H_
#define __WL_UTILS_H_

#include "wl_def.h"

#define SafeMalloc(s)    safe_malloc ((s),__FILE__,__LINE__)
#define FRACBITS         16

void     *safe_malloc (size_t size, const char *fname, uint32_t line);
#ifdef WOLF_FIXEDMUL_INLINE             /* inlined: a 32 x 32 -> 64-bit product, mul + mulh */
static inline fixed FixedMul (fixed a, fixed b)
{
    return (fixed)(((int64_t)a * b + 0x8000) >> FRACBITS);
}
#else
fixed    FixedMul (fixed a, fixed b);
#endif
fixed    FixedDiv (fixed a, fixed b);

uint16_t ReadShort (void *ptr);
uint32_t ReadLong (void *ptr);

void     Error (const char *string);
void     Help (const char *string);

#endif
