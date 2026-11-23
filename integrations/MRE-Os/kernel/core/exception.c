#include "mre_exception.h"
#include "mre_arch.h"
#include "mre_gic.h"
static void (*g_fatal_hook)(const mre_exception_frame_t *);
static volatile uint64_t g_exception_count;
void mre_exception_set_fatal_hook(void (*hook)(const mre_exception_frame_t *))
{ g_fatal_hook=hook; }
void mre_panic(const char *reason)
{
    (void)reason;
    mre_arch_irq_mask();
    for(;;) mre_arch_wfe();
}
void mre_exception_dispatch(mre_exception_frame_t *frame)
{
    if(frame==NULL) mre_panic("null exception frame");
    ++g_exception_count;
    if(frame->vector==1u) { mre_gic_dispatch(); return; }
    if(frame->vector==2u) { mre_gic_dispatch(); return; }
    if(g_fatal_hook!=NULL) g_fatal_hook(frame);
    mre_panic("unhandled synchronous exception");
}
