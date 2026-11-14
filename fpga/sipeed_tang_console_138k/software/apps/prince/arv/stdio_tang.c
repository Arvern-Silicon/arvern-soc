/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      stdio_tang.c (prince)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The stdio calls of the game's file.c (the configuration files and the
 * saved commanders, in the program's folder on the card) over FatFs: the
 * game's fopen ... sscanf are renamed to these at compile time (Makefile).
 * Two files open at a time; sscanf knows %d and literal characters, what
 * the configuration files need. And the memory report of the diagnostics.
 *----------------------------------------------------------------------------*/
#include <stdarg.h>
#include <string.h>
#include "ff.h"
#include "tang.h"

int arv_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int arv_vprintf(const char *fmt, va_list ap);

static FIL files[2];
static uint8_t used[2];

void *arv_fopen(const char *name, const char *mode)
{
    const BYTE m = mode[0] == 'w' ? (FA_WRITE | FA_CREATE_ALWAYS) : FA_READ;
    for (int i = 0; i < 2; i++)
        if (!used[i]) {
            if (f_open(&files[i], name, m) != FR_OK)
                return 0;
            used[i] = 1;
            return &files[i];
        }
    return 0;
}

int arv_fclose(void *f)
{
    FIL *fp = f;
    used[fp - files] = 0;
    return f_close(fp) == FR_OK ? 0 : -1;
}

unsigned arv_fread(void *buf, unsigned size, unsigned n, void *f)
{
    UINT got;
    if (f_read(f, buf, size * n, &got) != FR_OK || !size)
        return 0;
    return got / size;
}

unsigned arv_fwrite(const void *buf, unsigned size, unsigned n, void *f)
{
    UINT put;
    if (f_write(f, buf, size * n, &put) != FR_OK || !size)
        return 0;
    return put / size;
}

char *arv_fgets(char *s, int size, void *f)
{
    int n = 0;
    while (n + 1 < size) {
        char c;
        UINT got;
        if (f_read(f, &c, 1, &got) != FR_OK || got != 1)
            break;
        s[n++] = c;
        if (c == '\n')
            break;
    }
    s[n] = '\0';
    return n ? s : 0;
}

int arv_vfprintf(void *f, const char *fmt, va_list ap)
{
    char buf[160];
    UINT put;
    int n;
    if (!f)
        return arv_vprintf(fmt, ap);
    n = arv_vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > (int) sizeof(buf) - 1)
        n = (int) sizeof(buf) - 1;
    f_write(f, buf, (UINT) n, &put);
    return n;
}

int arv_fprintf(void *f, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = arv_vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int arv_sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    int n = 0;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt == '%' && fmt[1] == 'd') {
            int v = 0, neg = 0, digits = 0;
            fmt++;
            while (*s == ' ' || *s == '\t')
                s++;
            if (*s == '-' || *s == '+')
                neg = *s++ == '-';
            for (; *s >= '0' && *s <= '9'; s++, digits++)
                v = v * 10 + (*s - '0');
            if (!digits)
                break;
            *va_arg(ap, int *) = neg ? -v : v;
            n++;
        } else if (*s == *fmt) {
            s++;
        } else {
            break;
        }
    }
    va_end(ap);
    return n;
}

/*------------------------------- report -------------------------------------*/

extern char _heap_start[], _heap_end[], _bss_end[], _stack_end[];
#define STACK_MARK 0x57AC57ACu

/* The stack below the current one marked, so the report can tell how deep it
 * went; never inlined, and from the stack pointer down (an inlined copy's
 * frame address would be its caller's, whose locals lie below it) */
__attribute__((noinline)) void arv_stack_paint(void)
{
    uint32_t *p = (uint32_t *) (((uintptr_t) _bss_end + 3u) & ~3u);
    uint32_t *sp;
    __asm__ volatile ("mv %0, sp" : "=r" (sp));
    sp -= 64;
    while (p < sp)
        *p++ = STACK_MARK;
}

void arv_mem_report(const char *where)
{
    const uint32_t *p = (const uint32_t *) (((uintptr_t) _bss_end + 3u) & ~3u);
    while (p < (const uint32_t *) _stack_end && *p == STACK_MARK)
        p++;
    arv_printf("mem %s: stack %u, heap %u of %u\n", where, (unsigned) ((const char *) _stack_end - (const char *) p),
               (unsigned) (arv_heap_brk() - _heap_start), (unsigned) (_heap_end - _heap_start));
}

/* time (the game seeds its random numbers with it): the machine timer */
long time(long *t)
{
    const long v = (long) arv_mtime();
    if (t)
        *t = v;
    return v;
}
