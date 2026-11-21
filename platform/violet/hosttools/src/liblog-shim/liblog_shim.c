/* SPDX-License-Identifier: MIT
 * liblog shim for QEAFRESCUE: redirect Android logcat calls to stderr/fd 2 so
 * the rescue channel captures cnss-daemon debug in /tmp/hostsvc-*.log on a
 * kernel that has no /dev/log (android logger). Built freestanding with a raw
 * write(2) syscall, so it needs no libc and introduces no new NEEDED entries.
 * This changes only where log text goes: no QMI or service state is faked.
 */
#include <stdarg.h>
#include <stddef.h>

#define ANDROID_LOG_VERBOSE 2
#define ANDROID_LOG_DEBUG   3
#define ANDROID_LOG_INFO    4
#define ANDROID_LOG_WARN    5
#define ANDROID_LOG_ERROR   6
#define ANDROID_LOG_FATAL   7

static long sys_write(int fd, const void *buf, unsigned long n)
{
    long ret;
    __asm__ volatile("svc #0"
                     : "=r"(ret)
                     : "r"(0L /* __NR_write on arm64 */), "r"((long)fd),
                       "r"(buf), "r"(n)
                     : "memory");
    return ret;
}

static size_t xstrlen(const char *s)
{
    size_t n = 0;
    while(s[n]) n++;
    return n;
}

static void emit(const char *s, size_t n) { (void)sys_write(2, s, n); }

static const char *prio(int p)
{
    switch(p) {
    case ANDROID_LOG_VERBOSE: return "V";
    case ANDROID_LOG_DEBUG:   return "D";
    case ANDROID_LOG_INFO:    return "I";
    case ANDROID_LOG_WARN:    return "W";
    case ANDROID_LOG_ERROR:   return "E";
    case ANDROID_LOG_FATAL:   return "F";
    default:                  return "?";
    }
}

static void put_int(int v, char *buf, int *len)
{
    char tmp[16];
    int n = 0, i;
    if(v < 0) { buf[(*len)++] = '-'; v = -v; }
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while(v);
    for(i = n; i > 0; i--) buf[(*len)++] = tmp[i - 1];
}

/* Minimal printf: handles %s %d %u %x %ld %lu %llu %p %c %% and widths. */
static void vprintk(const char *fmt, va_list ap)
{
    char out[512];
    int len = 0;
    const char *p = fmt;

    while(*p && len < (int)sizeof(out) - 16) {
        if(*p != '%') { out[len++] = *p++; continue; }
        p++;
        if(*p == '%') { out[len++] = '%'; p++; continue; }
        /* skip flags/width/precision/length modifiers */
        int longlong = 0, lng = 0;
        while(*p && (*p == '-' || *p == '+' || *p == ' ' || *p == '#' ||
                     *p == '0' || (*p >= '1' && *p <= '9') || *p == '.')) p++;
        if(*p == 'l') { lng = 1; p++; if(*p == 'l') { longlong = 1; p++; } }
        if(*p == 'z' || *p == 't') p++;

        switch(*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if(!s) s = "(null)";
            while(*s && len < (int)sizeof(out) - 2) out[len++] = *s++;
            break;
        }
        case 'c': out[len++] = (char)va_arg(ap, int); break;
        case 'd':
        case 'i': {
            long v = longlong ? (long)va_arg(ap, long long)
                              : lng ? (long)va_arg(ap, long)
                                    : (long)va_arg(ap, int);
            put_int((int)v, out, &len);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            unsigned long long v = longlong ? va_arg(ap, unsigned long long)
                                : lng ? va_arg(ap, unsigned long)
                                      : (unsigned long)va_arg(ap, unsigned int);
            char tmp[32]; int n = 0;
            const char *dig = (*p == 'x' || *p == 'X') ? "0123456789abcdef" : "0123456789";
            if(*p != 'u' && v == 0) tmp[n++] = '0';
            while(v) { tmp[n++] = dig[v % 16]; v /= 16; }
            while(n > 0) out[len++] = tmp[--n];
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(unsigned long)va_arg(ap, void *);
            out[len++] = '0'; out[len++] = 'x';
            char tmp[32]; int n = 0;
            if(!v) tmp[n++] = '0';
            while(v) { tmp[n++] = "0123456789abcdef"[v % 16]; v /= 16; }
            while(n > 0) out[len++] = tmp[--n];
            break;
        }
        default: out[len++] = '?'; break;
        }
        p++;
    }
    emit(out, (size_t)len);
}

static int log_line(int pr, const char *tag, const char *fmt, va_list ap)
{
    emit("[", 1); emit(prio(pr), 1); emit(" ", 1);
    if(tag) { emit(tag, xstrlen(tag)); emit(" ", 1); }
    emit("] ", 2);
    vprintk(fmt, ap);
    emit("\n", 1);
    return 0;
}

int __android_log_print(int pr, const char *tag, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = log_line(pr, tag, fmt, ap); va_end(ap);
    return r;
}

int __android_log_vprint(int pr, const char *tag, const char *fmt, va_list ap)
{
    return log_line(pr, tag, fmt, ap);
}

int __android_log_write(int pr, const char *tag, const char *text)
{
    return __android_log_print(pr, tag, "%s", text ? text : "");
}

int __android_log_buf_write(int buf, int pr, const char *tag, const char *text)
{ (void)buf; return __android_log_write(pr, tag, text); }

int __android_log_buf_print(int buf, int pr, const char *tag,
                            const char *fmt, ...)
{
    va_list ap; int r; (void)buf;
    va_start(ap, fmt); r = log_line(pr, tag, fmt, ap); va_end(ap);
    return r;
}

int __android_log_is_debuggable(void) { return 1; }

/* libcutils and other bionic libs also reference these; provide real-enough
 * forwarding to stderr so nothing fails to resolve. */
void __android_log_assert(const char *cond, const char *tag,
                          const char *fmt, ...)
{
    va_list ap;
    emit("[F ", 3); if(tag) { emit(tag, xstrlen(tag)); emit(" ", 1); }
    emit("] ASSERT ", 9);
    if(cond) { emit(cond, xstrlen(cond)); emit(" ", 1); }
    if(fmt) {
        va_start(ap, fmt);
        vprintk(fmt, ap);
        va_end(ap);
    }
    emit("\n", 1);
    /* Match real __android_log_assert: abort the process. We use a raw
     * illegal instruction via inline asm so no libc is needed. */
    __asm__ volatile("brk #0" ::: "memory");
    __builtin_unreachable();
}

int __android_log_set_logger(void *logger) { (void)logger; return 0; }
int __android_log_set_default_tag(const char *tag) { (void)tag; return 0; }

/* Remaining bionic liblog API surface used by libcutils/libutils/libbase.
 * These are harmless shims: debug text already flows through the print
 * wrappers above; none of them fabricate any service or QMI state. */
int __android_log_error_write(int tag, const char *buf, size_t len)
{ (void)tag; emit("[E error] ", 10); emit(buf ? buf : "", len); emit("\n", 1); return 0; }
int __android_log_get_minimum_priority(void) { return ANDROID_LOG_VERBOSE; }
int __android_log_set_minimum_priority(int p) { (void)p; return 0; }
int __android_log_is_loggable(int pr, const char *tag, int def)
{ (void)pr; (void)tag; return def; }
void __android_log_call_aborter(void) { __asm__ volatile("brk #0" ::: "memory"); }
void __android_log_set_aborter(void *aborter) { (void)aborter; }
void *__android_log_logd_logger(void *logger) { (void)logger; return 0; }
int __android_log_write_log_message(void *msg)
{ (void)msg; return 0; }
