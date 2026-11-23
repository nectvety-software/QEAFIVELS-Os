#ifndef VQEAF_VIOLET_BOOT_H
#define VQEAF_VIOLET_BOOT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_VIOLET_MAX_MEMORY_REGIONS 4u
#define VQEAF_VIOLET_MAX_RESERVED_REGIONS 32u
#define VQEAF_VIOLET_MAX_PMM_DESCRIPTORS 160u

#define VQEAF_VIOLET_DTB_SOURCE_UNTRUSTED       0u
#define VQEAF_VIOLET_DTB_SOURCE_LIVE_HANDSET    (1u << 0)
#define VQEAF_VIOLET_DTB_SOURCE_BOOT_HANDOFF    (1u << 1)

typedef struct vqeaf_violet_region {
    uint64_t base;
    uint64_t size;
} vqeaf_violet_region_t;

typedef struct vqeaf_efi_memory_descriptor {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} vqeaf_efi_memory_descriptor_t;

typedef struct vqeaf_violet_kernel_layout {
    uint64_t image_start;
    uint64_t image_end;
    uint64_t text_start;
    uint64_t text_end;
    uint64_t rodata_start;
    uint64_t rodata_end;
} vqeaf_violet_kernel_layout_t;

/* Immutable discovery snapshot. This reports DTB-described resources only. */
typedef struct vqeaf_violet_boot_info {
    uint32_t dtb_valid;
    int32_t fdt_status;
    int32_t discovery_status;
    int32_t mmu_prerequisite_status;
    uint32_t dtb_source_flags;
    uint32_t board_identity_candidate;
    uint32_t board_identity_verified;
    uint32_t boot_handoff_verified;
    uint32_t gic_version;
    uint32_t gic_found;
    uint32_t uart_found;
    uint32_t timer_found;
    uint32_t memory_map_valid;
    uint32_t memory_region_count;
    uint32_t reserved_region_count;
    uint32_t dynamic_reserved_count;
    uint32_t uart_irq;
    uint32_t timer_irq;
    uint64_t dtb_size;
    uint64_t gic_distributor_base;
    uint64_t gic_distributor_size;
    uint64_t gic_cpu_interface_base;
    uint64_t gic_cpu_interface_size;
    uint64_t gic_redistributor_base;
    uint64_t gic_redistributor_size;
    uint64_t uart_base;
    uint64_t uart_size;
    vqeaf_violet_region_t memory[VQEAF_VIOLET_MAX_MEMORY_REGIONS];
    vqeaf_violet_region_t reserved[VQEAF_VIOLET_MAX_RESERVED_REGIONS];
} vqeaf_violet_boot_info_t;

/* Parse and discover only. No MMIO access or hardware initialization occurs. */
int vqeaf_violet_probe_dtb(const void *blob, size_t available,
                           uint32_t source_flags,
                           vqeaf_violet_boot_info_t *out);
const vqeaf_violet_boot_info_t *vqeaf_violet_current_info(void);
int vqeaf_violet_is_target(void);
int vqeaf_violet_platform_mode(void);

/* Split verified RAM into usable EFI-style descriptors, excluding all
 * reserved ranges, the kernel image and the DTB. Returns -2 if RAM exceeds
 * the current 4 GiB QEAF PMM limit; it never silently truncates RAM. */
int vqeaf_violet_build_pmm_descriptors(
    const vqeaf_violet_boot_info_t *info,
    uint64_t kernel_start, uint64_t kernel_end,
    uint64_t dtb_start, uint64_t dtb_size,
    vqeaf_efi_memory_descriptor_t *out, size_t capacity, size_t *count);

/* Hardware activation is explicit and requires a successful probe, verified
 * Violet identity, complete memory map, and successful PMM initialization. */
int vqeaf_violet_init_pmm(const vqeaf_violet_boot_info_t *info,
                          uint64_t kernel_start, uint64_t kernel_end,
                          uint64_t dtb_start, uint64_t dtb_size);
int vqeaf_violet_pmm_is_ready(void);
int vqeaf_violet_build_mmu(const vqeaf_violet_kernel_layout_t *layout);
int vqeaf_violet_enable_mmu(void);
/* Full early-boot sequence for a dedicated Violet entry point. Caller must
 * supply the physical DTB address and linker-derived image/text/rodata ranges.
 * Does not enable GIC/timer or assume a load address. */
int vqeaf_violet_native_bootstrap(
    const void *dtb_virtual, size_t dtb_available, uint32_t source_flags,
    uint64_t dtb_physical, const vqeaf_violet_kernel_layout_t *layout,
    vqeaf_violet_boot_info_t *report);
uint64_t vqeaf_violet_mmu_root(void);
int vqeaf_violet_mmu_is_enabled(void);

/* GIC/timer activation is separate from discovery and is allowed only after
 * the Violet MMU has been enabled with DTB-derived MMIO mappings. */
int vqeaf_violet_irq_init(void);
int vqeaf_violet_irq_is_active(void);
uint64_t vqeaf_violet_irq_dispatch(uint64_t frame_base);
void vqeaf_a64_timer_external_tick(void);

#ifdef __cplusplus
}
#endif
#endif
