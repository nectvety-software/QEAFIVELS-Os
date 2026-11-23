#include <string.h>
#include "vqeaf/violet_boot.h"
#include "vqeaf/memory.h"

typedef struct vqeaf_exclusion {
    uint64_t start;
    uint64_t end;
} vqeaf_exclusion_t;

static vqeaf_efi_memory_descriptor_t g_violet_pmm_map[VQEAF_VIOLET_MAX_PMM_DESCRIPTORS];
static uint32_t g_violet_pmm_ready;

static uint64_t page_down(uint64_t v)
{
    return v & ~(VQEAF_PAGE_SIZE - 1u);
}

static int page_up_checked(uint64_t v, uint64_t *out)
{
    if (v > UINT64_MAX - (VQEAF_PAGE_SIZE - 1u)) return -1;
    *out = (v + VQEAF_PAGE_SIZE - 1u) & ~(VQEAF_PAGE_SIZE - 1u);
    return 0;
}

static int append_descriptor(vqeaf_efi_memory_descriptor_t *out,
                             size_t capacity, size_t *count,
                             uint64_t start, uint64_t end)
{
    vqeaf_efi_memory_descriptor_t *d;
    if (end <= start || ((start | end) & (VQEAF_PAGE_SIZE - 1u)) != 0u)
        return -1;
    if (*count >= capacity) return -3;
    d = &out[(*count)++];
    d->type = 7u; /* EFI conventional memory, matching QEAF PMM contract */
    d->pad = 0u;
    d->physical_start = start;
    d->virtual_start = start;
    d->number_of_pages = (end - start) / VQEAF_PAGE_SIZE;
    d->attribute = 0u;
    return 0;
}

int vqeaf_violet_build_pmm_descriptors(
    const vqeaf_violet_boot_info_t *info,
    uint64_t kernel_start, uint64_t kernel_end,
    uint64_t dtb_start, uint64_t dtb_size,
    vqeaf_efi_memory_descriptor_t *out, size_t capacity, size_t *count)
{
    vqeaf_exclusion_t exclusions[VQEAF_VIOLET_MAX_RESERVED_REGIONS + 2u];
    size_t exclusion_count = 0u, produced = 0u;
    uint32_t m, i;
    uint64_t dtb_end;

    if (info == NULL || out == NULL || count == NULL || capacity == 0u ||
        info->dtb_valid == 0u || info->board_identity_verified == 0u ||
        info->boot_handoff_verified == 0u || info->memory_map_valid == 0u || info->dynamic_reserved_count != 0u ||
        info->mmu_prerequisite_status != 0 ||
        info->memory_region_count == 0u ||
        info->memory_region_count > VQEAF_VIOLET_MAX_MEMORY_REGIONS ||
        info->reserved_region_count > VQEAF_VIOLET_MAX_RESERVED_REGIONS ||
        kernel_start >= kernel_end || dtb_size == 0u ||
        dtb_start > UINT64_MAX - dtb_size)
        return -1;

    dtb_end = dtb_start + dtb_size;
    exclusions[exclusion_count].start = page_down(kernel_start);
    if (page_up_checked(kernel_end, &exclusions[exclusion_count].end) != 0)
        return -1;
    ++exclusion_count;
    exclusions[exclusion_count].start = page_down(dtb_start);
    if (page_up_checked(dtb_end, &exclusions[exclusion_count].end) != 0)
        return -1;
    ++exclusion_count;

    for (i = 0; i < info->reserved_region_count; ++i) {
        uint64_t end;
        if (info->reserved[i].size == 0u ||
            info->reserved[i].base > UINT64_MAX - info->reserved[i].size)
            return -1;
        end = info->reserved[i].base + info->reserved[i].size;
        exclusions[exclusion_count].start = page_down(info->reserved[i].base);
        if (page_up_checked(end, &exclusions[exclusion_count].end) != 0)
            return -1;
        ++exclusion_count;
    }

    for (m = 0; m < info->memory_region_count; ++m) {
        uint64_t mem_end, cursor, limit;
        if (info->memory[m].size == 0u ||
            info->memory[m].base > UINT64_MAX - info->memory[m].size)
            return -1;
        mem_end = info->memory[m].base + info->memory[m].size;
        /* The current QEAF PMM bitmap addresses only [0, 4 GiB). Do not
         * silently clip a 6/8 GiB Violet memory map. */
        if (mem_end > VQEAF_PHYS_LIMIT) return -2;
        if (page_up_checked(info->memory[m].base, &cursor) != 0)
            return -1;
        limit = page_down(mem_end);

        while (cursor < limit) {
            uint64_t skip_to = cursor;
            uint64_t next_exclusion = limit;
            for (i = 0; i < exclusion_count; ++i) {
                const vqeaf_exclusion_t *x = &exclusions[i];
                if (x->end <= cursor || x->start >= limit) continue;
                if (x->start <= cursor) {
                    if (x->end > skip_to) skip_to = x->end;
                } else if (x->start < next_exclusion) {
                    next_exclusion = x->start;
                }
            }
            if (skip_to > cursor) {
                cursor = skip_to < limit ? skip_to : limit;
                continue;
            }
            if (next_exclusion <= cursor) return -1;
            {
                int rc = append_descriptor(out, capacity, &produced,
                                           cursor, next_exclusion);
                if (rc != 0) return rc;
            }
            cursor = next_exclusion;
        }
    }
    if (produced == 0u) return -2;
    *count = produced;
    return 0;
}

int vqeaf_violet_init_pmm(const vqeaf_violet_boot_info_t *info,
                          uint64_t kernel_start, uint64_t kernel_end,
                          uint64_t dtb_start, uint64_t dtb_size)
{
    size_t count = 0u;
    int rc;
    if (info == NULL || info != vqeaf_violet_current_info() ||
        g_violet_pmm_ready)
        return -1;
    rc = vqeaf_violet_build_pmm_descriptors(
        info, kernel_start, kernel_end, dtb_start, dtb_size,
        g_violet_pmm_map, VQEAF_VIOLET_MAX_PMM_DESCRIPTORS, &count);
    if (rc != 0) return rc;
    rc = vqeaf_pmm_init(g_violet_pmm_map,
                        (uint64_t)(count * sizeof(g_violet_pmm_map[0])),
                        sizeof(g_violet_pmm_map[0]));
    if (rc != 0) return rc;
    g_violet_pmm_ready = 1u;
    return 0;
}

int vqeaf_violet_pmm_is_ready(void)
{
    return g_violet_pmm_ready ? 1 : 0;
}
