/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_WIFI_DEVICE_H
#define VQEAF_WIFI_DEVICE_H
#include <stdint.h>
#include <stddef.h>

/* Hardware resources are discovered from the live DTB, not hard-coded.
 * No bus address, GPIO, IRQ or DMA value is invented in the OS. */
typedef enum {
    VQEAF_WIFI_BUS_UNKNOWN = 0,
    VQEAF_WIFI_BUS_SNOC,
    VQEAF_WIFI_BUS_PCIE,
    VQEAF_WIFI_BUS_USB
} vqeaf_wifi_bus_t;

typedef struct {
    vqeaf_wifi_bus_t bus;
    const char *compatible;       /* pointer into the DTB strings block */
    uint64_t mmio_base, mmio_size;
    uint64_t msa_base, msa_size;
    uint32_t irqs[16];            /* inline: no allocator in the hot path */
    size_t irq_count;
    int dt_node;                  /* struct-block offset, -1 when absent */
    char path[128];               /* DTB path the values came from */
    int msa_found;
} vqeaf_wifi_device_t;

const char *vqeaf_wifi_bus_name(vqeaf_wifi_bus_t bus);
void vqeaf_wifi_device_release(vqeaf_wifi_device_t *dev);

/* Real Flattened Device Tree parser. Reads `reg`, `interrupts`, `compatible`
 * and the MSA reserved-memory region from the DTB blob supplied by the
 * platform. Returns 0 on discovery, -ENODEV when no supported node exists,
 * or a negative parse error. Never returns a fabricated resource. */
int vqeaf_wifi_fdt_valid(const void *fdt, size_t size);
int vqeaf_wifi_fdt_discover(const void *fdt, size_t size,
                            vqeaf_wifi_device_t *dev);

#endif
