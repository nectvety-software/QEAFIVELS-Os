/* qeaf_code_libc.c - minimal freestanding string/memory routines.
 *
 * Native QEAPP modules link with qenlink, which rejects every undefined
 * symbol: there is no libc. The portable core (and the compiler itself, for
 * large struct copies) may still emit calls to these routines, so the module
 * provides them. Volatile accesses keep the optimiser from rewriting these
 * loops back into calls to themselves.
 *
 * This file is compiled only into the native module; host tests link the
 * platform C library and never include it.
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

void *memmove(void *dst, const void *src, size_t count)
{
    volatile unsigned char *d = (volatile unsigned char *)dst;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    if(d < s) {
        while(count-- > 0) *d++ = *s++;
    } else if(d > s) {
        d += count;
        s += count;
        while(count-- > 0) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const volatile unsigned char *a = (const volatile unsigned char *)left;
    const volatile unsigned char *b = (const volatile unsigned char *)right;
    while(count-- > 0) {
        if(*a != *b) return (int)*a - (int)*b;
        a++;
        b++;
    }
    return 0;
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

int strncmp(const char *left, const char *right, size_t count)
{
    const volatile unsigned char *a = (const volatile unsigned char *)left;
    const volatile unsigned char *b = (const volatile unsigned char *)right;
    while(count-- > 0) {
        if(*a != *b) return (int)*a - (int)*b;
        if(*a == '\0') return 0;
        a++;
        b++;
    }
    return 0;
}

char *strcpy(char *dst, const char *src)
{
    volatile char *d = (volatile char *)dst;
    const volatile char *s = (const volatile char *)src;
    while((*d++ = *s++) != '\0') { }
    return dst;
}

char *strncpy(char *dst, const char *src, size_t count)
{
    volatile char *d = (volatile char *)dst;
    const volatile char *s = (const volatile char *)src;
    while(count-- > 0) {
        *d++ = *s;
        if(*s != '\0') s++;
    }
    return dst;
}
