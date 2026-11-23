#include "mre_timer.h"
#include "mre_gic.h"
static uint64_t g_frequency;
static uint64_t g_period_ticks;
static uint32_t g_irq;
static volatile uint64_t g_ticks;
#if defined(__aarch64__)
static void timer_irq_handler(uint32_t irq,void *context)
{
    (void)irq;(void)context;mre_timer_irq();
}
#endif
mre_status_t mre_timer_init(uint32_t irq)
{
#if defined(__aarch64__)
    uint64_t freq;
    mre_status_t st;
    if(irq>=1024u) return MRE_E_INVALID;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if(freq==0u) return MRE_E_UNSUPPORTED;
    __asm__ volatile("msr cntp_ctl_el0, xzr\nisb" ::: "memory");
    st=mre_gic_register_handler(irq,timer_irq_handler,NULL);
    if(st!=MRE_OK) return st;
    st=mre_gic_enable_irq(irq);
    if(st!=MRE_OK) return st;
    g_frequency=freq;g_irq=irq;g_period_ticks=0u;g_ticks=0u;
    return MRE_OK;
#else
    (void)irq;return MRE_E_UNSUPPORTED;
#endif
}
uint64_t mre_timer_frequency(void) { return g_frequency; }
uint64_t mre_timer_counter(void)
{
#if defined(__aarch64__)
    uint64_t v;__asm__ volatile("mrs %0, cntpct_el0":"=r"(v));return v;
#else
    return 0u;
#endif
}
uint64_t mre_timer_ticks_from_us(uint64_t us)
{ return mre_timer_ticks_from_frequency(g_frequency,us); }
uint64_t mre_timer_ticks_elapsed(void) { return g_ticks; }
uint32_t mre_timer_irq_number(void) { return g_irq; }
mre_status_t mre_timer_set_periodic(uint64_t period_us)
{
#if defined(__aarch64__)
    uint64_t ticks;
    if(g_frequency==0u || g_irq==0u) return MRE_E_INVALID;
    ticks=mre_timer_ticks_from_us(period_us);
    if(ticks==0u || ticks>0x7FFFFFFFull) return MRE_E_INVALID;
    g_period_ticks=ticks;
    __asm__ volatile("msr cntp_tval_el0, %0\nmov x9, #1\nmsr cntp_ctl_el0, x9\nisb"
                     ::"r"(ticks):"x9","memory");
    return MRE_OK;
#else
    (void)period_us;return MRE_E_UNSUPPORTED;
#endif
}
void mre_timer_stop(void)
{
#if defined(__aarch64__)
    __asm__ volatile("msr cntp_ctl_el0, xzr\nisb" ::: "memory");
#endif
    g_period_ticks=0u;
}
void mre_timer_irq(void)
{
    ++g_ticks;
#if defined(__aarch64__)
    if(g_period_ticks!=0u)
        __asm__ volatile("msr cntp_tval_el0, %0"::"r"(g_period_ticks):"memory");
#endif
}
