/* qeaf_code_util.h - small freestanding helpers shared by the port. */
#ifndef QEAF_CODE_UTIL_H
#define QEAF_CODE_UTIL_H

#include <stddef.h>

static inline int qeaf_strlen(const char *s)
{
    int n = 0;
    if(s == NULL) return 0;
    while(s[n] != '\0') n++;
    return n;
}

static inline int qeaf_copy_trunc(char *dst, const char *src, int cap)
{
    int k = 0;
    if(dst == NULL || src == NULL || cap <= 0) return 0;
    while(src[k] != '\0' && k < cap - 1) {
        dst[k] = src[k];
        k++;
    }
    dst[k] = '\0';
    return k;
}

static inline int qeaf_int_str(int n, char *buf)
{
    char tmp[12];
    int i = 0, p = 0, k;
    if(n == 0) tmp[i++] = '0';
    while(n > 0) {
        tmp[i++] = (char)('0' + n % 10);
        n /= 10;
    }
    for(k = i - 1; k >= 0; k--) buf[p++] = tmp[k];
    buf[p] = '\0';
    return p;
}

static inline int qeaf_is_ident_head(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static inline int qeaf_is_ident_char(char c)
{
    return qeaf_is_ident_head(c) || (c >= '0' && c <= '9');
}

/* Does `s` start with `word` followed by a non-identifier character? */
static inline int qeaf_word_at(const char *s, int n, const char *word)
{
    int i = 0;
    while(word[i] != '\0') {
        if(i >= n || s[i] != word[i]) return 0;
        i++;
    }
    if(i < n && qeaf_is_ident_char(s[i])) return 0;
    return 1;
}

/* Bounded append helpers used by the screen painters. They always keep the
 * result NUL terminated and return the new length. */
static inline int qeaf_append_str(char *dst, int cap, int p, const char *s)
{
    int i = 0;
    if(dst == NULL || cap <= 0) return 0;
    while(s != NULL && s[i] != '\0' && p < cap - 1) dst[p++] = s[i++];
    dst[p] = '\0';
    return p;
}

static inline int qeaf_append_int(char *dst, int cap, int p, int n)
{
    char num[12];
    qeaf_int_str(n, num);
    return qeaf_append_str(dst, cap, p, num);
}

/* Append at most `n` bytes from `s`, for clipping a source line to the view. */
static inline int qeaf_append_n(char *dst, int cap, int p, const char *s, int n)
{
    int i = 0;
    if(dst == NULL || cap <= 0) return 0;
    while(s != NULL && s[i] != '\0' && i < n && p < cap - 1)
        dst[p++] = s[i++];
    dst[p] = '\0';
    return p;
}

#endif /* QEAF_CODE_UTIL_H */
