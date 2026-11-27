#include <string.h>
#include "vqeaf/violet_boot.h"
#include "vqeaf/violet_platform.h"
#include "mre_fdt.h"
#include "mre_violet.h"
#include "mre_mmu.h"

static mre_violet_boot_info_t g_mre_info;
static vqeaf_violet_boot_info_t g_public_info;
static uint32_t g_probe_ready;
static uint32_t g_platform_mode;
static uint32_t g_mmu_built;
static uint32_t g_mmu_enabled;

int vqeaf_violet_probe_dtb(const void *blob, size_t available,
                           uint32_t source_flags,
                           vqeaf_violet_boot_info_t *out)
{
    mre_fdt_t fdt;
    mre_status_t st;
    uint32_t i;

    /* Entering the Violet probe path permanently disables QEMU-only IRQ
     * fallback for this boot, even if the DTB is malformed or unrecognized. */
    g_platform_mode = 1u;
    if (blob == NULL || out == NULL || vqeaf_violet_pmm_is_ready() ||
        (source_flags & ~(VQEAF_VIOLET_DTB_SOURCE_LIVE_HANDSET |
                          VQEAF_VIOLET_DTB_SOURCE_BOOT_HANDOFF)) != 0u)
        return -1;
    memset(out, 0, sizeof(*out));
    memset(&g_mre_info, 0, sizeof(g_mre_info));
    memset(&g_public_info, 0, sizeof(g_public_info));
    g_probe_ready = g_mmu_built = g_mmu_enabled = 0u;

    st = mre_fdt_open(&fdt, blob, available);
    out->fdt_status = (int32_t)st;
    if (st != MRE_OK) return (int)st;

    out->dtb_valid = 1u;
    out->dtb_size = fdt.total_size;
    st = mre_violet_discover(&fdt, &g_mre_info);
    out->discovery_status = (int32_t)st;
    if (st != MRE_OK) return (int)st;

    /* ICNSS discovery is additive: a device may boot without WLAN support.
     * Only the resource parser runs here; power, firmware and PD remain OFF. */
    out->icnss_discovery_status =
        (int32_t)vqeaf_violet_probe(blob, fdt.total_size);

    out->dtb_source_flags = source_flags;
    out->board_identity_candidate = g_mre_info.board_identity_verified;
    out->board_identity_verified =
        (g_mre_info.board_identity_verified &&
         (source_flags & (VQEAF_VIOLET_DTB_SOURCE_LIVE_HANDSET |
                          VQEAF_VIOLET_DTB_SOURCE_BOOT_HANDOFF))) ? 1u : 0u;
    out->boot_handoff_verified =
        (g_mre_info.board_identity_verified &&
         (source_flags & VQEAF_VIOLET_DTB_SOURCE_BOOT_HANDOFF)) ? 1u : 0u;
    out->gic_version = (uint32_t)g_mre_info.gic.version;
    out->gic_found = g_mre_info.gic_found;
    out->uart_found = g_mre_info.uart_found;
    out->timer_found = g_mre_info.timer_found;
    out->memory_map_valid = g_mre_info.memory_map_valid;
    out->memory_region_count = g_mre_info.memory_region_count;
    out->reserved_region_count = g_mre_info.reserved_region_count;
    out->dynamic_reserved_count = g_mre_info.dynamic_reserved_count;
    out->uart_irq = g_mre_info.uart_irq;
    out->timer_irq = g_mre_info.timer_irq;
    out->gic_distributor_base = g_mre_info.gic.distributor_base;
    out->gic_distributor_size = g_mre_info.gic.distributor_size;
    out->gic_cpu_interface_base = g_mre_info.gic.cpu_interface_base;
    out->gic_cpu_interface_size = g_mre_info.gic.cpu_interface_size;
    out->gic_redistributor_base = g_mre_info.gic.redistributor_base;
    out->gic_redistributor_size = g_mre_info.gic.redistributor_size;
    out->uart_base = g_mre_info.uart_base;
    out->uart_size = g_mre_info.uart_size;
    out->display_discovery_status = (int32_t)g_mre_info.display_status;
    _Static_assert(sizeof(out->display) == sizeof(g_mre_info.display),
                   "Violet display discovery ABI mismatch");
    memcpy(&out->display, &g_mre_info.display, sizeof(out->display));

    for (i = 0; i < g_mre_info.memory_region_count &&
                i < VQEAF_VIOLET_MAX_MEMORY_REGIONS; ++i) {
        out->memory[i].base = g_mre_info.memory[i].base;
        out->memory[i].size = g_mre_info.memory[i].size;
    }
    for (i = 0; i < g_mre_info.reserved_region_count &&
                i < VQEAF_VIOLET_MAX_RESERVED_REGIONS; ++i) {
        out->reserved[i].base = g_mre_info.reserved[i].base;
        out->reserved[i].size = g_mre_info.reserved[i].size;
    }
    out->mmu_prerequisite_status =
        (int32_t)mre_violet_mmu_prerequisites(&g_mre_info);
    g_public_info = *out;
    g_probe_ready = 1u;
    return 0;
}

const vqeaf_violet_boot_info_t *vqeaf_violet_current_info(void)
{
    return g_probe_ready ? &g_public_info : NULL;
}

int vqeaf_violet_is_target(void)
{
    return g_probe_ready && g_public_info.board_identity_verified ? 1 : 0;
}

int vqeaf_violet_platform_mode(void) { return g_platform_mode ? 1 : 0; }

int vqeaf_violet_build_mmu(const vqeaf_violet_kernel_layout_t *layout)
{
    mre_violet_kernel_layout_t mre_layout;
    mre_status_t st;
    if (!g_probe_ready || !vqeaf_violet_pmm_is_ready() || layout == NULL ||
        g_public_info.board_identity_verified == 0u ||
        g_public_info.mmu_prerequisite_status != MRE_OK)
        return -1;
    mre_layout.image_start = layout->image_start;
    mre_layout.image_end = layout->image_end;
    mre_layout.text_start = layout->text_start;
    mre_layout.text_end = layout->text_end;
    mre_layout.rodata_start = layout->rodata_start;
    mre_layout.rodata_end = layout->rodata_end;
    st = mre_violet_build_page_tables(&g_mre_info, &mre_layout);
    if (st != MRE_OK) return (int)st;
    if(g_public_info.icnss_discovery_status == 0) {
        int wifi_map_rc = vqeaf_violet_map_snoc_mmu();
        if(wifi_map_rc != 0) return wifi_map_rc;
    }
    g_mmu_built = 1u;
    return 0;
}

int vqeaf_violet_enable_mmu(void)
{
    mre_status_t st;
    if (!g_mmu_built || g_mmu_enabled) return -1;
    st = mre_mmu_enable();
    if (st != MRE_OK) return (int)st;
    g_mmu_enabled = 1u;
    return 0;
}

uint64_t vqeaf_violet_mmu_root(void)
{
    return g_mmu_built ? (uint64_t)(uintptr_t)mre_mmu_root_table() : 0u;
}

int vqeaf_violet_mmu_is_enabled(void) { return g_mmu_enabled ? 1 : 0; }
