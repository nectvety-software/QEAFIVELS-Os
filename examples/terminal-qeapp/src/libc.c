/* libc.c - freestanding string/memory routines for the terminal module.
 *
 * qenlink rejects undefined symbols, so a native module has no libc. The
 * compiler may still emit calls to these four routines (struct copies, string
 * helpers), so the module defines them itself. Volatile accesses keep the
 * optimiser from turning a loop back into a call to the same function.
 */
#include <stddef.h>

void *memset(void *dst, int value, size_t count)
{
    volatile unsigned char *p = (volatile unsigned char *)dst;
    while(count-- > 0) *p++ = (unsigned char)value;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t count)
{
    volatile unsigned char *d = (volatile unsigned char *)dst;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    while(count-- > 0) *d++ = *s++;
    return dst;
}

size_t strlen(const char *text)
{
    const volatile char *p = (const volatile char *)text;
    size_t n = 0;
    while(p[n] != '\0') n++;
    return n;
}

int strcmp(const char *left, const char *right)
{
    const volatile unsigned char *a = (const volatile unsigned char *)left;
    const volatile unsigned char *b = (const volatile unsigned char *)right;
    while(*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}
