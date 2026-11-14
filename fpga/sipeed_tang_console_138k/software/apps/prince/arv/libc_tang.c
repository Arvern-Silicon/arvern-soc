/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      libc_tang.c (prince)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The few C library calls the game makes that newlib would bring in whole:
 * the printf family (the engine's are renamed to these at compile time, see
 * the Makefile) with %d %i %u %x %X %c %s %p %%, the - and 0 flags, a width
 * and l / z / h modifiers, output to the TTY or a buffer (stdio_tang.c
 * writes files with it); and the program's system calls (the heap break).
 *----------------------------------------------------------------------------*/
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "tang.h"

typedef struct { char *buf; size_t size, n; } out_t;

static void put(out_t *o, char c)
{
    if (o->buf) {
        if (o->n + 1u < o->size)
            o->buf[o->n] = c;
    } else {
        arv_putc(c);
    }
    o->n++;
}

static void format(out_t *o, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        char tmp[12], *s, pad = ' ';
        int width = 0, left = 0, len;
        uint32_t v;
        bool neg = false;
        if (*fmt != '%') {
            put(o, *fmt);
            continue;
        }
        fmt++;
        for (; *fmt == '-' || *fmt == '0'; fmt++)
            if (*fmt == '-') left = 1; else pad = '0';
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        for (; *fmt >= '0' && *fmt <= '9'; fmt++)
            width = width * 10 + (*fmt - '0');
        while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h')
            fmt++;
        s = tmp + sizeof(tmp);
        *--s = '\0';
        switch (*fmt) {
        case 'd': case 'i': {
            int32_t i = va_arg(ap, int32_t);
            neg = i < 0;
            v = neg ? (uint32_t) -i : (uint32_t) i;
            do { *--s = (char) ('0' + v % 10u); v /= 10u; } while (v);
            break;
        }
        case 'u':
            v = va_arg(ap, uint32_t);
            do { *--s = (char) ('0' + v % 10u); v /= 10u; } while (v);
            break;
        case 'x': case 'X': case 'p':
            v = (*fmt == 'p') ? (uint32_t) (uintptr_t) va_arg(ap, void *) : va_arg(ap, uint32_t);
            do { *--s = "0123456789abcdef0123456789ABCDEF"[(v & 15u) + (*fmt == 'X' ? 16u : 0u)]; v >>= 4; } while (v);
            break;
        case 'c':
            *--s = (char) va_arg(ap, int);
            break;
        case 's':
            s = va_arg(ap, char *);
            if (!s) s = "(null)";
            break;
        case '%':
            *--s = '%';
            break;
        default:
            if (!*fmt) return;
            *--s = *fmt;
            break;
        }
        len = (int) strlen(s) + (neg ? 1 : 0);
        if (neg && pad == '0') put(o, '-');
        if (!left) for (; width > len; width--) put(o, pad);
        if (neg && pad != '0') put(o, '-');
        while (*s) put(o, *s++);
        for (; width > len; width--) put(o, ' ');
    }
}

int arv_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    out_t o = { buf, size, 0 };
    format(&o, fmt, ap);
    if (buf && size)
        buf[o.n < size ? o.n : size - 1u] = '\0';
    return (int) o.n;
}

int arv_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = arv_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int arv_sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = arv_vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap);
    return n;
}

int arv_vprintf(const char *fmt, va_list ap)
{
    out_t o = { 0, 0, 0 };
    format(&o, fmt, ap);
    return (int) o.n;
}

int arv_printf(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = arv_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int arv_puts_nl(const char *s) { arv_puts(s); arv_putc('\n'); return 0; }

void arv_exit(int code)
{
    arv_printf("prince: exit %d\n", code);
    arv_halt();
}

/*------------------------------- system calls -------------------------------*/

extern char _heap_start[], _heap_end[];
static char *brk_ptr = _heap_start;

char *arv_heap_brk(void) { return brk_ptr; }

void *_sbrk(ptrdiff_t incr)
{
    char *old = brk_ptr;
    if (brk_ptr + incr > _heap_end || brk_ptr + incr < _heap_start) {
        errno = ENOMEM;
        return (void *) -1;
    }
    brk_ptr += incr;
    return old;
}

/* memset in words (newlib-nano's stores one byte per iteration); the
 * attribute keeps GCC from turning the loops back into a memset call */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memset(void *dst, int c, size_t n)
{
    uint8_t *p = dst;
    const uint32_t w = (uint8_t) c * 0x01010101u;
    while (n && ((uintptr_t) p & 3u)) { *p++ = (uint8_t) c; n--; }
    for (; n >= 4u; n -= 4u, p += 4u)
        *(uint32_t *) p = w;
    while (n--)
        *p++ = (uint8_t) c;
    return dst;
}
