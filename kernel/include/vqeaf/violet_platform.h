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
    uint64_t mmio[3][2];              /* reg tuples: SNOC, SMMU IOVA, IPA */
    size_t mmio_count;
    char reg_names[3][32];
    uint64_t smmu_iova_base, smmu_iova_size;
    uint64_t smmu_ipa_base, smmu_ipa_size;
    uint32_t ce_irq[16];              /* GIC INTIDs, not raw DT SPI indices */
    uint32_t ce_irq_flags[16];
    uint32_t ce_irq_type[16];
    size_t ce_irq_count;
    uint64_t msa_base, msa_size;      /* reserved-memory region */
    int msa_resolved;                 /* 1 when phandle -> reg resolved */
    uint32_t msa_requested_size;      /* qcom,wlan-msa-memory */
    uint32_t smmu_id;                 /* iommus ID cell (phandle skipped) */
    uint32_t smmu_flags;              /* iommus second cell */
    int has_smmu;
    int smmu_s1_bypass;
    int irq_parent_is_gic; /* interrupt parent is GIC or a GIC-backed domain */
    int irq_parent_is_pdc; /* qcom,pdc-sm6150 hierarchy over GIC */
    int status_enabled;
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

/* Provider census describes resource discovery separately from executable
 * hardware providers. VERIFIED means the value is present and structurally
 * valid in DT; it does not claim that power/DMA/remoteproc can be operated. */
typedef enum {
    VQEAF_PROV_UNKNOWN = 0,
    VQEAF_PROV_VERIFIED,
    VQEAF_PROV_UNSUPPORTED
} vqeaf_provider_state_t;

typedef struct {
    vqeaf_provider_state_t mmio, regulators, clocks, resets, irq, dma,
                           iommu, remoteproc, memory;
} vqeaf_provider_census_t;

/* Physical SM6150 services are injected only after their own DT/PMIC/clock/
 * IOMMU driver has proved its resources. dma_alloc MUST return CPU memory
 * coherent for the WCN DMA master (or explicitly non-cacheable); CE IRQ
 * callbacks execute in interrupt context and must be non-blocking. */
typedef struct vqeaf_violet_hw_ops {
    void *ctx;
    int (*regulators_enable)(void *, const vqeaf_violet_resources_t *);
    void (*regulators_disable)(void *, const vqeaf_violet_resources_t *);
    int (*clocks_enable)(void *, const vqeaf_violet_resources_t *);
    void (*clocks_disable)(void *, const vqeaf_violet_resources_t *);
    int (*icnss_power_on)(void *, const vqeaf_violet_resources_t *);
    void (*icnss_power_off)(void *, const vqeaf_violet_resources_t *);
    int (*msa_prepare)(void *, const vqeaf_violet_resources_t *);
    void (*msa_release)(void *, const vqeaf_violet_resources_t *);
    int (*dma_alloc)(void *, uint64_t, uint64_t, void **, uint64_t *);
    void (*dma_free)(void *, uint64_t, void *, uint64_t);
    int (*ce_irq)(void *, uint8_t, uint32_t);
    uint64_t (*now_ms)(void *);
    void (*delay_ms)(void *, uint32_t);
} vqeaf_violet_hw_ops_t;


int vqeaf_violet_probe(const void *fdt, size_t size);
const vqeaf_violet_resources_t *vqeaf_violet_resources(void);
const vqeaf_provider_census_t *vqeaf_violet_providers(void);
/* Register a complete, board-verified low-level provider. This stores callbacks
 * only; it does not power on WLAN or write firmware/calibration. */
int vqeaf_violet_register_hw_ops(const vqeaf_violet_hw_ops_t *ops);
int vqeaf_violet_hw_provider_ready(void);
/* Add the DT-verified SNOC register window to the MMU as Device memory before
 * MMU enable. The fixed MSA pool remains reserved until a DMA/SMMU provider
 * sets up the correct mapping and cache attributes. */
int vqeaf_violet_map_snoc_mmu(void);
/* Build a phase2 device from parsed DT values. Ops are real backend entry
 * points that return -ENOSYS until a kernel MMIO/irq/DMA primitive is supplied. */
int vqeaf_violet_bind_phase2(void);
/* Build the first-generation wcn3990 device from the same parsed resources. */
int vqeaf_violet_bind_wcn3990(void);
const char *vqeaf_violet_state_name(int state);

#endif
