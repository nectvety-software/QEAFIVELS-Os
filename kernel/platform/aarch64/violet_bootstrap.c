#include "vqeaf/violet_boot.h"

static int valid_kernel_layout(const vqeaf_violet_kernel_layout_t *layout)
{
    if (layout == 0 || layout->image_start >= layout->image_end ||
        layout->text_start >= layout->text_end ||
        layout->rodata_start >= layout->rodata_end)
        return 0;
    if (layout->text_start < layout->image_start ||
        layout->text_end > layout->image_end ||
        layout->rodata_start < layout->image_start ||
        layout->rodata_end > layout->image_end)
        return 0;
    if (layout->text_start < layout->rodata_end &&
        layout->rodata_start < layout->text_end)
        return 0;
    return 1;
}

int vqeaf_violet_native_bootstrap(
    const void *dtb_virtual, size_t dtb_available, uint32_t source_flags,
    uint64_t dtb_physical, const vqeaf_violet_kernel_layout_t *layout,
    vqeaf_violet_boot_info_t *report)
{
    const vqeaf_violet_boot_info_t *info;
    vqeaf_violet_boot_info_t local = {0};
    int rc;

    if (dtb_virtual == 0 || dtb_physical == 0u ||
        !valid_kernel_layout(layout))
        return -1;

    /* A read-only live dump is useful evidence, but it is not proof that this
     * DTB was handed to this kernel by the active boot chain. Never activate
     * PMM/MMU from a caller-supplied LIVE_HANDSET flag alone. */
    if ((source_flags & VQEAF_VIOLET_DTB_SOURCE_BOOT_HANDOFF) == 0u)
        return -4;

    rc = vqeaf_violet_probe_dtb(dtb_virtual, dtb_available,
                                source_flags, &local);
    if (report != 0) *report = local;
    if (rc != 0) return rc;
    info = vqeaf_violet_current_info();
    if (info == 0 || !info->board_identity_verified) return -2;
    if (!info->boot_handoff_verified || !info->memory_map_valid ||
        info->mmu_prerequisite_status != 0)
        return -3;

    rc = vqeaf_violet_init_pmm(info, layout->image_start, layout->image_end,
                               dtb_physical, info->dtb_size);
    if (rc != 0) return rc;
    rc = vqeaf_violet_build_mmu(layout);
    if (rc != 0) return rc;
    rc = vqeaf_violet_enable_mmu();
    if (rc != 0) return rc;

    if (report != 0) *report = *info;
    return 0;
}
