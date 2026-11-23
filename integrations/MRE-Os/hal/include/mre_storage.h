#ifndef MRE_STORAGE_H
#define MRE_STORAGE_H
#include "mre_types.h"
typedef struct mre_block_info { uint64_t block_count; uint32_t block_size; } mre_block_info_t;
typedef struct mre_storage_ops {
    mre_status_t (*get_info)(mre_block_info_t *out);
    mre_status_t (*read_blocks)(uint64_t lba, uint32_t count, void *buffer);
    mre_status_t (*write_blocks)(uint64_t lba, uint32_t count, const void *buffer);
    mre_status_t (*flush)(void);
} mre_storage_ops_t;
#endif
