#include "vqeaf/violet_boot.h"
#include "vqeaf/arch_aarch64.h"
#include "vqeaf/scheduler.h"
#include "mre_gic.h"
#include "mre_timer.h"

static uint32_t g_violet_irq_ready;

int vqeaf_violet_irq_init(void)
{
    const vqeaf_violet_boot_info_t *info = vqeaf_violet_current_info();
    mre_gic_config_t cfg;
    mre_status_t st;

    if (g_violet_irq_ready) return -1;
    if (info == 0 || !vqeaf_violet_mmu_is_enabled() ||
        !info->board_identity_verified || !info->boot_handoff_verified ||
        !info->gic_found ||
        !info->timer_found || info->timer_irq < 16u ||
        info->timer_irq >= 32u || info->gic_distributor_base == 0u)
        return -1;

    cfg.version = (mre_gic_version_t)info->gic_version;
    cfg.distributor_base = (uintptr_t)info->gic_distributor_base;
    cfg.distributor_size = (size_t)info->gic_distributor_size;
    cfg.cpu_interface_base = (uintptr_t)info->gic_cpu_interface_base;
    cfg.cpu_interface_size = (size_t)info->gic_cpu_interface_size;
    cfg.redistributor_base = (uintptr_t)info->gic_redistributor_base;
    cfg.redistributor_size = (size_t)info->gic_redistributor_size;
    cfg.interrupt_count = 0u;

    st = mre_gic_configure(&cfg);
    if (st != MRE_OK) return (int)st;
    st = mre_timer_init(info->timer_irq);
    if (st != MRE_OK) return (int)st;
    st = mre_timer_set_periodic(10000u);
    if (st != MRE_OK) {
        mre_timer_stop();
        return (int)st;
    }

    g_violet_irq_ready = 1u;
    return 0;
}

int vqeaf_violet_irq_is_active(void)
{
    return g_violet_irq_ready ? 1 : 0;
}

int vqeaf_violet_irq_request(uint32_t irq,
                             void (*handler)(uint32_t, void *), void *context)
{
    mre_status_t st;
    if(!g_violet_irq_ready || handler == 0 || irq < 32u)
        return -1;
    st = mre_gic_register_handler(irq, handler, context);
    if(st != MRE_OK) return (int)st;
    st = mre_gic_enable_irq(irq);
    if(st != MRE_OK) {
        (void)mre_gic_unregister_handler(irq);
        return (int)st;
    }
    return 0;
}

int vqeaf_violet_irq_release(uint32_t irq)
{
    if(!g_violet_irq_ready || irq < 32u) return -1;
    return (int)mre_gic_unregister_handler(irq);
}

uint64_t vqeaf_violet_irq_dispatch(uint64_t frame_base)
{
    uint64_t before, after;
    before = mre_timer_ticks_elapsed();
    mre_gic_dispatch();
    after = mre_timer_ticks_elapsed();
    if (after != before) {
        vqeaf_a64_timer_external_tick();
        if (vqeaf_a64_sched_active())
            return vqeaf_a64_sched_switch(frame_base);
    }
    return frame_base;
}
