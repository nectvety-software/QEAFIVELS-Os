/* SPDX-License-Identifier: MIT */
/* tests/host_mre_gic_stub.h - the host GIC the drives link against.
 *
 * Configuring a GIC needs a real AArch64 target, so on the host only the two
 * calls a drive makes are answered, and what they were asked to do is
 * readable from the test. Nothing here pretends an interrupt fired.
 */
#ifndef VQEAF_HOST_MRE_GIC_STUB_H
#define VQEAF_HOST_MRE_GIC_STUB_H

#include <stdint.h>

#include "mre_gic.h"

extern uint32_t vqeaf_host_gic_irq;
extern mre_irq_handler_t vqeaf_host_gic_handler;
extern void *vqeaf_host_gic_context;
extern int vqeaf_host_gic_enabled;

void vqeaf_host_gic_reset(void);

#endif
