#ifndef MRE_MMU_H
#define MRE_MMU_H
#include "mre_types.h"
#define MRE_PAGE_SIZE 4096u
#define MRE_MMU_MAX_TABLES 64u
typedef enum mre_memory_type {
    MRE_MEMORY_DEVICE = 0,
    MRE_MEMORY_NORMAL_WB = 1
} mre_memory_type_t;
typedef struct mre_mmu_region {
    uint64_t virtual_address, physical_address, size;
    mre_memory_type_t type;
    uint8_t writable, executable;
} mre_mmu_region_t;
mre_status_t mre_mmu_reset(void);
mre_status_t mre_mmu_map_range(const mre_mmu_region_t *region);
const uint64_t *mre_mmu_root_table(void);
size_t mre_mmu_tables_used(void);
uint64_t mre_mmu_max_mapped_address(void);
mre_status_t mre_mmu_enable(void);
#endif
