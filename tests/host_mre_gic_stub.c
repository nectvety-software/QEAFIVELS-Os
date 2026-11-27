/* SPDX-License-Identifier: MIT */
/* tests/host_mre_gic_stub.c - answers the two GIC calls a drive makes. */
#include "host_mre_gic_stub.h"

#include <stddef.h>

uint32_t vqeaf_host_gic_irq;
mre_irq_handler_t vqeaf_host_gic_handler;
void *vqeaf_host_gic_context;
int vqeaf_host_gic_enabled;

void vqeaf_host_gic_reset(void)
{
    vqeaf_host_gic_irq = 0u;
    vqeaf_host_gic_handler = NULL;
    vqeaf_host_gic_context = NULL;
    vqeaf_host_gic_enabled = 0;
}

mre_status_t mre_gic_register_handler(uint32_t irq, mre_irq_handler_t handler,
                                      void *context)
{
    if (handler == NULL) return MRE_E_INVALID;
    vqeaf_host_gic_irq = irq;
    vqeaf_host_gic_handler = handler;
    vqeaf_host_gic_context = context;
    return MRE_OK;
}

mre_status_t mre_gic_enable_irq(uint32_t irq)
{
    if (irq != vqeaf_host_gic_irq) return MRE_E_INVALID;
    vqeaf_host_gic_enabled = 1;
    return MRE_OK;
}
