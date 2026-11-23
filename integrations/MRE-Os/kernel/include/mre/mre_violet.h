#ifndef MRE_VIOLET_H
#define MRE_VIOLET_H
#include "mre_fdt.h"
#include "mre_gic.h"
#define MRE_VIOLET_MAX_MEMORY_REGIONS 4u
#define MRE_VIOLET_MAX_RESERVED_REGIONS 32u
typedef struct mre_violet_region {
    uint64_t base, size;
} mre_violet_region_t;
typedef struct mre_violet_boot_info {
    uint32_t valid;
    uint32_t gic_found, uart_found, timer_found;
    uint32_t memory_map_valid;
    uint32_t board_identity_verified;
    uint32_t memory_region_count, reserved_region_count, dynamic_reserved_count;
    mre_gic_config_t gic;
    uint64_t uart_base, uart_size;
    uint32_t uart_irq, timer_irq;
    uint64_t dtb_size;
    mre_violet_region_t memory[MRE_VIOLET_MAX_MEMORY_REGIONS];
    mre_violet_region_t reserved[MRE_VIOLET_MAX_RESERVED_REGIONS];
} mre_violet_boot_info_t;
mre_status_t mre_violet_discover(const mre_fdt_t *fdt, mre_violet_boot_info_t *info);
typedef struct mre_violet_kernel_layout {
    uint64_t image_start, image_end;
    uint64_t text_start, text_end;
    uint64_t rodata_start, rodata_end;
} mre_violet_kernel_layout_t;
mre_status_t mre_violet_mmu_prerequisites(const mre_violet_boot_info_t *info);
mre_status_t mre_violet_build_page_tables(const mre_violet_boot_info_t *info,
                                           const mre_violet_kernel_layout_t *layout);
const mre_violet_boot_info_t *mre_violet_boot_info(void);
typedef struct mre_kernel_boot_report {
    mre_status_t fdt_status;
    mre_status_t discovery_status;
    mre_status_t mmu_prerequisite_status;
    mre_status_t mmu_build_status;
    uint32_t mmu_enabled;
} mre_kernel_boot_report_t;
const mre_kernel_boot_report_t *mre_kernel_boot_report(void);
mre_status_t mre_kernel_enable_mmu(void);
void mre_kernel_main(uint64_t dtb_address);
#endif
