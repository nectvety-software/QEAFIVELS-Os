#ifndef MRE_RESOURCE_H
#define MRE_RESOURCE_H
#include "mre_types.h"

/* Resource access is capability-mediated. Drivers must never dereference
 * arbitrary physical addresses or install interrupts directly. */
typedef struct mre_mmio_region {
    uintptr_t virtual_base;
    size_t length;
    uint32_t capability_id;
} mre_mmio_region_t;

typedef void (*mre_irq_handler_fn)(void *context);
typedef struct mre_irq_handle {
    uint32_t irq_number;
    uint32_t capability_id;
} mre_irq_handle_t;

typedef struct mre_dma_buffer {
    void *virtual_address;
    uintptr_t device_address;
    size_t length;
    uint32_t capability_id;
} mre_dma_buffer_t;

typedef struct mre_resource_ops {
    mre_status_t (*map_mmio)(uint32_t device_capability, mre_mmio_region_t *out);
    mre_status_t (*request_irq)(uint32_t device_capability, mre_irq_handler_fn handler,
                                void *context, mre_irq_handle_t *out);
    mre_status_t (*allocate_dma)(uint32_t device_capability, size_t length,
                                 size_t alignment, mre_dma_buffer_t *out);
    mre_status_t (*release_dma)(mre_dma_buffer_t *buffer);
} mre_resource_ops_t;

#endif
