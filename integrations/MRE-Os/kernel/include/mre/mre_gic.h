#ifndef MRE_GIC_H
#define MRE_GIC_H
#include "mre_types.h"
typedef enum mre_gic_version { MRE_GIC_NONE=0, MRE_GIC_V2=2, MRE_GIC_V3=3 } mre_gic_version_t;
typedef struct mre_gic_config {
    mre_gic_version_t version;
    uintptr_t distributor_base;
    size_t distributor_size;
    uintptr_t cpu_interface_base;
    size_t cpu_interface_size;
    uintptr_t redistributor_base;
    size_t redistributor_size;
    uint32_t interrupt_count;
} mre_gic_config_t;
typedef void (*mre_irq_handler_t)(uint32_t irq, void *context);
mre_status_t mre_gic_configure(const mre_gic_config_t *config);
mre_status_t mre_gic_register_handler(uint32_t irq, mre_irq_handler_t handler, void *context);
mre_status_t mre_gic_enable_irq(uint32_t irq);
mre_status_t mre_gic_disable_irq(uint32_t irq);
/* Disable the source, wait for the distributor write to complete, then clear
 * the registered handler. Safe to call only after the IRQ subsystem is live. */
mre_status_t mre_gic_unregister_handler(uint32_t irq);
uint32_t mre_gic_acknowledge(void);
void mre_gic_end(uint32_t irq);
void mre_gic_dispatch(void);
uint32_t mre_gic_irq_id(uint32_t iar);
#endif
