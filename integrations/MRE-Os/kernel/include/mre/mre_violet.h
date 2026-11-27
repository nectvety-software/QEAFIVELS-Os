#ifndef MRE_VIOLET_H
#define MRE_VIOLET_H
#include "mre_fdt.h"
#include "mre_gic.h"
#define MRE_VIOLET_MAX_MEMORY_REGIONS 4u
#define MRE_VIOLET_MAX_RESERVED_REGIONS 32u
#define MRE_VIOLET_MAX_DISPLAY_CLOCKS 6u
#define MRE_VIOLET_MAX_DISPLAY_SUPPLIES 8u
#define MRE_VIOLET_DISPLAY_SUPPLY_NAME 16u

typedef struct mre_violet_region {
    uint64_t base, size;
} mre_violet_region_t;
typedef struct mre_violet_clock_ref {
    uint32_t provider_phandle;
    uint32_t id;
} mre_violet_clock_ref_t;
typedef struct mre_violet_supply_ref {
    uint32_t phandle;
    char name[MRE_VIOLET_DISPLAY_SUPPLY_NAME];
} mre_violet_supply_ref_t;
typedef struct mre_violet_display_info {
    uint32_t found;
    uint32_t mdss_found, dpu_found, dsi_found, phy_found, dispcc_found, panel_found;
    uint64_t mdss_base, mdss_size;
    uint64_t dpu_base, dpu_size;
    uint64_t dpu_vbif_base, dpu_vbif_size;
    uint64_t dsi_base, dsi_size;
    uint64_t phy_base, phy_size;
    uint64_t phy_lane_base, phy_lane_size;
    uint64_t phy_pll_base, phy_pll_size;
    uint64_t dispcc_base, dispcc_size;
    uint32_t mdss_irq;
    uint32_t dpu_irq_controller, dpu_irq_index;
    uint32_t dsi_irq_controller, dsi_irq_index;
    uint32_t dsi_clock_count;
    mre_violet_clock_ref_t dsi_clocks[MRE_VIOLET_MAX_DISPLAY_CLOCKS];
    uint32_t dsi_assigned_clock_count;
    mre_violet_clock_ref_t dsi_assigned_clocks[MRE_VIOLET_MAX_DISPLAY_CLOCKS];
    uint32_t dsi_assigned_parent_count;
    mre_violet_clock_ref_t dsi_assigned_parents[MRE_VIOLET_MAX_DISPLAY_CLOCKS];
    uint32_t lane_count;
    uint8_t lanes[4];
    uint32_t reset_phandle, reset_gpio, reset_flags, reset_active_low;
    uint32_t backlight_phandle;
    uint32_t supply_count;
    mre_violet_supply_ref_t supplies[MRE_VIOLET_MAX_DISPLAY_SUPPLIES];
    uint32_t panel_timing_present;
    uint32_t panel_command_data_present;
} mre_violet_display_info_t;
typedef struct mre_violet_boot_info {
    uint32_t valid;
    uint32_t gic_found, uart_found, timer_found;
    mre_status_t display_status;
    mre_violet_display_info_t display;
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
