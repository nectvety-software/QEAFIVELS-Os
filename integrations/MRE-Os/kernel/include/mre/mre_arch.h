#ifndef MRE_ARCH_H
#define MRE_ARCH_H
#include <stdint.h>
static inline void mre_arch_dsb_sy(void) {
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}
static inline void mre_arch_isb(void) {
#if defined(__aarch64__)
    __asm__ volatile("isb" ::: "memory");
#endif
}
static inline void mre_arch_wfe(void) {
#if defined(__aarch64__)
    __asm__ volatile("wfe");
#endif
}
static inline void mre_arch_irq_mask(void) {
#if defined(__aarch64__)
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
#endif
}
#endif
