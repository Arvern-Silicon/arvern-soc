// WL_UTILS.C

#include "wl_utils.h"


/*
===================
=
= safe_malloc
=
= Wrapper for malloc with a NULL check
=
===================
*/

void *safe_malloc (size_t size, const char *fname, uint32_t line)
{
    void *ptr;

    ptr = malloc(size);

#ifdef WOLF_GR_CACHE
    if (!ptr)
    {
        CA_FreeGrCache ();                          // the cached pictures reload when drawn
        ptr = malloc(size);
    }
#endif
#ifdef WOLF_HEAP_REPORT
    if (!ptr)
    {
        extern char *arv_heap_brk (void), _heap_start[], _heap_end[];
        Quit ("SafeMalloc: %u bytes at %s: line %u (heap %u of %u bytes reached)", (unsigned) size, fname, line,
              (unsigned) (arv_heap_brk () - _heap_start), (unsigned) (_heap_end - _heap_start));
    }
#endif
    if (!ptr)
        Quit ("SafeMalloc: Out of memory at %s: line %u",fname,line);

    return ptr;
}


#ifndef WOLF_FIXEDMUL_INLINE
fixed FixedMul (fixed a, fixed b)
{
	return (fixed)(((int64_t)a * b + 0x8000) >> FRACBITS);
}
#endif

fixed FixedDiv (fixed a, fixed b)
{
	int64_t c = ((int64_t)a << FRACBITS) / (int64_t)b;

	return (fixed)c;
}

uint16_t ReadShort (void *ptr)
{
    unsigned value;
    byte     *work;

    work = ptr;
    value = work[0] | (work[1] << 8);

    return value;
}

uint32_t ReadLong (void *ptr)
{
    uint32_t value;
    byte     *work;

    work = ptr;
    value = work[0] | (work[1] << 8) | (work[2] << 16) | (work[3] << 24);

    return value;
}


void Error (const char *string)
{
    SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_ERROR,"Wolf4SDL",string,NULL);
}

void Help (const char *string)
{
    SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_INFORMATION,"Wolf4SDL",string,NULL);
}
