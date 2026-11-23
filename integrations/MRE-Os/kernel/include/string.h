#ifndef MRE_FREESTANDING_STRING_H
#define MRE_FREESTANDING_STRING_H
#include <stddef.h>
void *memset(void *dst,int value,size_t n);
void *memcpy(void *dst,const void *src,size_t n);
void *memmove(void *dst,const void *src,size_t n);
size_t strlen(const char *s);
int strcmp(const char *a,const char *b);
int memcmp(const void *a,const void *b,size_t n);
void *memchr(const void *s,int c,size_t n);
#endif
