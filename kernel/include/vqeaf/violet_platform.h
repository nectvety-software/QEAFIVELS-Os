/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_VIOLET_PLATFORM_H
#define VQEAF_VIOLET_PLATFORM_H
#include <stdint.h>
#include <stddef.h>

/* Violet Native Platform Backend: resource/provider layer for the stock
 * SM6150 device tree. It reports real DT values and fails closed when a
 * hardware primitive is not available. No MMIO address, IRQ or DMA value is
 * invented here. */
typedef struct {
    int probed;                       /* 1 after a successful DT parse */
    char compatible[64];
    char path[128];
    uint64_t mmio[3][2];              /* (base,size) from icnss reg */
    size_t mmio_count;
    uint32_t ce_irq[16];
    size_t ce_irq_count;
    uint64_t msa_base, msa_size;      /* reserved-memory region */
    int msa_resolved;                 /* 1 when phandle -> reg resolved */
    uint32_t msa_requested_size;      /* qcom,wlan-msa-memory */
    uint32_t smmu_id;                 /* iommus ID cell (phandle skipped) */
    uint32_t smmu_flags;              /* iommus second cell */
    int has_smmu;
    /* Regulator supplies discovered from DT (resource level only; enabling
     * them stays UNSUPPORTED until a kernel primitive exists). */
    char regulator_supplies[4][40];
    uint32_t regulator_phandles[4];
    size_t regulator_count;
    /* Interrupt parent and its cell count, used to parse CE interrupts. */
    uint32_t irq_parent_phandle;
    uint32_t irq_cells;
    int irq_parent_found;
} vqeaf_violet_resources_t;

/* Provider status: VERIFIED when read from DT, UNSUPPORTED when the platform
 * has no primitive, UNKNOWN when the DT has no property. */
typedef enum {
    VQEAF_PROV_UNKNOWN = 0,
    VQEAF_PROV_VERIFIED,
    VQEAF_PROV_UNSUPPORTED
} vqeaf_provider_state_t;

typedef struct {
    vqeaf_provider_state_t mmio, regulators, clocks, resets, irq, dma,
                           iommu, remoteproc, memory;
} vqeaf_provider_census_t;

int vqeaf_violet_probe(const void *fdt, size_t size);
const vqeaf_violet_resources_t *vqeaf_violet_resources(void);
const vqeaf_provider_census_t *vqeaf_violet_providers(void);
/* Build a phase2 device from parsed DT values. Ops are real backend entry
 * points that return -ENOSYS until a kernel MMIO/irq/DMA primitive is supplied. */
int vqeaf_violet_bind_phase2(void);
/* Build the first-generation wcn3990 device from the same parsed resources. */
int vqeaf_violet_bind_wcn3990(void);
const char *vqeaf_violet_state_name(int state);

#endif
