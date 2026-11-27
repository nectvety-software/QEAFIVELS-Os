#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "vqeaf/violet_boot.h"
#include "mre_fdt.h"

#ifndef VQEAF_REFERENCE_DTB_PATH
#error VQEAF_REFERENCE_DTB_PATH must name the checked-in reference DTB
#endif

int vqeaf_a64_sched_active(void) { return 0; }
uint64_t vqeaf_a64_sched_switch(uint64_t frame) { return frame; }
void vqeaf_a64_timer_external_tick(void) { }

static void test_abl_display_discovery(const vqeaf_violet_boot_info_t *info)
{
    const vqeaf_violet_display_info_t *d = &info->display;
    assert(info->display_discovery_status == 0);
    assert(d->found && d->mdss_found && d->dpu_found && d->dsi_found &&
           d->phy_found && d->dispcc_found && d->panel_found);
    assert(d->mdss_base == 0x0ae00000ull && d->mdss_size == 0x1000u);
    assert(d->dpu_base == 0x0ae01000ull && d->dpu_size == 0x8f000u);
    assert(d->dpu_vbif_base == 0x0aeb0000ull && d->dpu_vbif_size == 0x2008u);
    assert(d->dsi_base == 0x0ae94000ull && d->dsi_size == 0x400u);
    assert(d->phy_base == 0x0ae94400ull && d->phy_size == 0x100u);
    assert(d->phy_lane_base == 0x0ae94500ull && d->phy_lane_size == 0x300u);
    assert(d->phy_pll_base == 0x0ae94800ull && d->phy_pll_size == 0x124u);
    assert(d->dispcc_base == 0x0af00000ull && d->dispcc_size == 0x20000u);
    assert(d->mdss_irq == 115u);
    assert(d->dpu_irq_controller == 0xe9u && d->dpu_irq_index == 0u);
    assert(d->dsi_irq_controller == 0xe9u && d->dsi_irq_index == 4u);
    assert(d->dsi_clock_count == 6u);
    assert(d->dsi_clocks[0].provider_phandle == 0xe7u && d->dsi_clocks[0].id == 2u);
    assert(d->dsi_clocks[2].id == 24u && d->dsi_clocks[3].id == 18u);
    assert(d->dsi_clocks[4].id == 0u);
    assert(d->dsi_clocks[5].provider_phandle == 0x37u && d->dsi_clocks[5].id == 30u);
    assert(d->dsi_assigned_clock_count == 2u);
    assert(d->dsi_assigned_clocks[0].id == 3u && d->dsi_assigned_clocks[1].id == 25u);
    assert(d->dsi_assigned_parent_count == 2u);
    assert(d->dsi_assigned_parents[0].provider_phandle == 0xefu &&
           d->dsi_assigned_parents[0].id == 0u);
    assert(d->dsi_assigned_parents[1].provider_phandle == 0xefu &&
           d->dsi_assigned_parents[1].id == 1u);
    assert(d->lane_count == 4u && d->lanes[0] == 1u && d->lanes[1] == 2u &&
           d->lanes[2] == 3u && d->lanes[3] == 0u);
    assert(d->reset_phandle == 0x41u && d->reset_gpio == 91u);
    assert(d->backlight_phandle == 0xf4u);
    assert(d->supply_count == 5u);
    assert(d->supplies[0].phandle == 0xf1u);
    assert(d->supplies[1].phandle == 0xf6u);
    assert(d->supplies[2].phandle == 0x43u);
    assert(d->supplies[3].phandle == 0x42u && d->supplies[4].phandle == 0x42u);
    assert(d->panel_timing_present == 0u);
    assert(d->panel_command_data_present == 0u);
}

static void test_memory_exclusions(void)
{
    vqeaf_violet_boot_info_t info = {0};
    vqeaf_efi_memory_descriptor_t desc[16];
    size_t count = 0u;
    info.dtb_valid = 1u;
    info.board_identity_verified = 1u;
    info.boot_handoff_verified = 1u;
    info.memory_map_valid = 1u;
    info.mmu_prerequisite_status = 0;
    info.memory_region_count = 1u;
    info.memory[0].base = 0x80000000ull;
    info.memory[0].size = 0x40000000ull;
    info.reserved_region_count = 1u;
    info.reserved[0].base = 0x90000000ull;
    info.reserved[0].size = 0x00200000ull;

    assert(vqeaf_violet_build_pmm_descriptors(
        &info, 0x80000000ull, 0x80400000ull,
        0x81000000ull, 0x00010000ull, desc, 16u, &count) == 0);
    assert(count == 3u);
    assert(desc[0].physical_start == 0x80400000ull);
    assert(desc[0].number_of_pages == 0xC00u);
    assert(desc[1].physical_start == 0x81010000ull);
    assert(desc[2].physical_start == 0x90200000ull);

    info.boot_handoff_verified = 0u;
    assert(vqeaf_violet_build_pmm_descriptors(
        &info, 0x80000000ull, 0x80400000ull,
        0x81000000ull, 0x00010000ull, desc, 16u, &count) == -1);
    info.boot_handoff_verified = 1u;

    info.memory[0].size = 0x120000000ull;
    count = 0u;
    assert(vqeaf_violet_build_pmm_descriptors(
        &info, 0x80000000ull, 0x80400000ull,
        0x81000000ull, 0x10000ull, desc, 16u, &count) == -2);
    info.board_identity_verified = 0u;
    assert(vqeaf_violet_build_pmm_descriptors(
        &info, 0x80000000ull, 0x80400000ull,
        0x81000000ull, 0x10000ull, desc, 16u, &count) == -1);
}

int main(int argc, char **argv)
{
    const char *dtb_path = argc > 1 ? argv[1] : VQEAF_REFERENCE_DTB_PATH;
    const uint32_t expected_board = argc > 1 ? 1u : 0u;
    FILE *fp = fopen(dtb_path, "rb");
    long size;
    uint8_t *blob;
    vqeaf_violet_boot_info_t info, boot_report;
    vqeaf_violet_kernel_layout_t layout = {
        0x80000000ull, 0x80400000ull,
        0x80000000ull, 0x80100000ull,
        0x80100000ull, 0x80200000ull
    };
    const vqeaf_violet_boot_info_t *current;
    int expected_bootstrap_gate = expected_board ? -3 : -2;

    assert(fp != NULL);
    assert(fseek(fp, 0, SEEK_END) == 0);
    size = ftell(fp);
    assert(size > 40);
    assert(fseek(fp, 0, SEEK_SET) == 0);
    blob = (uint8_t *)malloc((size_t)size);
    assert(blob != NULL);
    assert(fread(blob, 1, (size_t)size, fp) == (size_t)size);
    fclose(fp);

    {
        mre_fdt_t fdt;
        mre_fdt_node_t timer_node;
        const void *ints = NULL;
        uint32_t cursor = 0u, ints_len = 0u;
        mre_status_t fdt_st = mre_fdt_open(&fdt, blob, (size_t)size);
        mre_status_t timer_st = fdt_st == MRE_OK
            ? mre_fdt_find_compatible(&fdt, "arm,armv8-timer", &cursor, &timer_node)
            : fdt_st;
        if (timer_st == MRE_OK)
            timer_st = mre_fdt_get_property(&fdt, timer_node, "interrupts",
                                            &ints, &ints_len);
        fprintf(stderr, "FDT timer search status=%d interrupts_status=%d length=%u\\n",
                (int)fdt_st, (int)timer_st, ints_len);
    }

    assert(vqeaf_violet_probe_dtb(blob, (size_t)size,
                                  VQEAF_VIOLET_DTB_SOURCE_UNTRUSTED,
                                  &info) == 0);
    fprintf(stderr,
        "DTB probe: path=%s board=%u gic=%u uart=%u timer=%u mem=%u reserved=%u dynamic=%u mmu_gate=%d\n",
        dtb_path, info.board_identity_candidate, info.gic_version,
        info.uart_found, info.timer_found, info.memory_region_count,
        info.reserved_region_count, info.dynamic_reserved_count,
        info.mmu_prerequisite_status);

    assert(info.dtb_valid == 1u);
    assert(info.dtb_size == (uint64_t)size);
    assert(info.board_identity_candidate == expected_board);
    assert(info.board_identity_verified == 0u);
    assert(info.boot_handoff_verified == 0u);
    assert(info.memory_map_valid == 0u);
    assert(info.mmu_prerequisite_status != 0);
    if (expected_board) test_abl_display_discovery(&info);

    /* A live read-only dump can identify the board, but cannot prove that
     * this DTB was handed to the running kernel by the active boot chain. */
    assert(vqeaf_violet_probe_dtb(blob, (size_t)size,
                                  VQEAF_VIOLET_DTB_SOURCE_LIVE_HANDSET,
                                  &info) == 0);
    assert(info.board_identity_candidate == expected_board);
    assert(info.board_identity_verified == expected_board);
    assert(info.boot_handoff_verified == 0u);

    /* Both the generic SoC tree and the board-marked ABL tree are incomplete
     * for kernel boot. The ABL tree is closer to Violet identity, but it has
     * no usable RAM map, so bootstrap must stop before PMM/MMU/GIC/timer. */
    assert(vqeaf_violet_native_bootstrap(
        blob, (size_t)size, VQEAF_VIOLET_DTB_SOURCE_LIVE_HANDSET,
        (uint64_t)(uintptr_t)blob, &layout, &boot_report) == -4);
    assert(vqeaf_violet_native_bootstrap(
        blob, (size_t)size, VQEAF_VIOLET_DTB_SOURCE_BOOT_HANDOFF,
        (uint64_t)(uintptr_t)blob, &layout, &boot_report) == expected_bootstrap_gate);

    current = vqeaf_violet_current_info();
    assert(current != NULL);
    assert(vqeaf_violet_platform_mode() == 1);
    assert(vqeaf_violet_is_target() == (int)expected_board);
    assert(current->boot_handoff_verified == expected_board);
    assert(vqeaf_violet_init_pmm(current, 0x80000000ull, 0x80400000ull,
                                 (uint64_t)(uintptr_t)blob, (uint64_t)size) != 0);
    assert(vqeaf_violet_build_mmu(NULL) != 0);
    assert(vqeaf_violet_enable_mmu() != 0);
    assert(vqeaf_violet_irq_init() != 0);
    assert(vqeaf_violet_mmu_is_enabled() == 0);
    assert(vqeaf_violet_irq_is_active() == 0);

    if (!expected_board) test_memory_exclusions();
    free(blob);
    puts("VQEAF Violet DTB bootstrap gate passed; no hardware access performed");
    return 0;
}
