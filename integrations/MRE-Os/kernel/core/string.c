#include <stddef.h>
#include <string.h>
void *memset(void *dst, int value, size_t n)
{
    unsigned char *d=(unsigned char *)dst;
    while(n--) *d++=(unsigned char)value;
    return dst;
}
void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d=(unsigned char *)dst; const unsigned char *s=(const unsigned char *)src;
    while(n--) *d++=*s++;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d=(unsigned char *)dst; const unsigned char *s=(const unsigned char *)src;
    if(d<s) { while(n--) *d++=*s++; }
    else if(d>s) { d+=n; s+=n; while(n--) *--d=*--s; }
    return dst;
}
size_t strlen(const char *s) { size_t n=0; if(s) while(s[n]) ++n; return n; }
int strcmp(const char *a,const char *b)
{ while(*a && *a==*b){++a;++b;} return (unsigned char)*a-(unsigned char)*b; }
int memcmp(const void *a,const void *b,size_t n)
{ const unsigned char *x=(const unsigned char *)a,*y=(const unsigned char *)b; while(n--){if(*x!=*y)return (int)*x-(int)*y;++x;++y;} return 0; }
void *memchr(const void *s,int c,size_t n)
{ const unsigned char *p=(const unsigned char *)s; while(n--){if(*p==(unsigned char)c)return (void *)p;++p;} return NULL; }
