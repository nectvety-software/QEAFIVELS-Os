#ifndef VQEAF_EL0_H
#define VQEAF_EL0_H
/* EL0-side syscall wrappers for the independent AArch64 kernel.
 *
 * Included by both static EL0 executables (launcher) and .qen modules (app).
 * There is no libc: every call is a direct SVC with x8 = syscall number.
 * The ABI (numbers + structs) comes from the kernel's single source of truth
 * kernel/include/vqeaf/el0_abi.h; both sides are built with -I kernel/include.
 */
#include "vqeaf/el0_abi.h"

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;
typedef int i32;

static inline i64 vqeaf_sys0(i64 n)
{
    register i64 x8 __asm__("x8") = n;
    register i64 x0 __asm__("x0") = 0;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline i64 vqeaf_sys1(i64 n, i64 a0)
{
    register i64 x8 __asm__("x8") = n;
    register i64 x0 __asm__("x0") = a0;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline i64 vqeaf_sys2(i64 n, i64 a0, i64 a1)
{
    register i64 x8 __asm__("x8") = n;
    register i64 x0 __asm__("x0") = a0;
    register i64 x1 __asm__("x1") = a1;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return x0;
}

static inline i64 vqeaf_sys3(i64 n, i64 a0, i64 a1, i64 a2)
{
    register i64 x8 __asm__("x8") = n;
    register i64 x0 __asm__("x0") = a0;
    register i64 x1 __asm__("x1") = a1;
    register i64 x2 __asm__("x2") = a2;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

/* A whole marker in one syscall: SVC runs with IRQ masked at EL1, so the
 * string cannot be interleaved with another task's UART output. */
static inline void el0_print(const char *s)
{
    u64 n = 0;
    while(s[n]) n++;
    vqeaf_sys2(VQEAF_SYS_PRINT, (i64)(u64)(unsigned long)s, (i64)n);
}

static inline void el0_exit(void)
{
    vqeaf_sys0(VQEAF_SYS_EXIT);
    for(;;) {}
}

static inline int el0_frame_done(void)
{
    return (int)vqeaf_sys1(VQEAF_SYS_FRAME_DONE, 0);
}

#endif /* VQEAF_EL0_H */
