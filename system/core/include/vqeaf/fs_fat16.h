#ifndef VQEAF_FS_FAT16_H
#define VQEAF_FS_FAT16_H
#include <stdint.h>
#include "block.h"
#include "vqeaf/vfs.h"
/* First persistent filesystem: read-only FAT16 over a block device or
 * partition. FAT12 and FAT32 are rejected explicitly, not misinterpreted.
 * Write support is NOT implemented in v0.3 and is reported as such. */
#define VQEAF_FAT_NAME_MAX 13u /* 8.3 plus separator, NUL */
#define VQEAF_FAT_NODE_MAX 16u
typedef struct {
 uint32_t dev_id; uint64_t base_lba;
 uint32_t bytes_per_sector,sectors_per_cluster,reserved_sectors,fat_count,root_entries,fat_sectors,total_sectors;
 uint64_t fat_lba,root_lba,data_lba;
 uint32_t cluster_count;
} VqeafFat16;
typedef struct FatNode FatNode;
struct VqeafFat16Mount;
struct FatNode { VqeafVnode vnode; struct VqeafFat16Mount *mnt; uint32_t first_cluster,size; uint8_t is_dir; };
typedef struct VqeafFat16Mount VqeafFat16Mount;
struct VqeafFat16Mount { VqeafFat16 fs; VqeafVnode root_vnode; FatNode pool[VQEAF_FAT_NODE_MAX]; uint32_t node_count; };
extern const VqeafVfsOps vqeaf_fat16_vfs_ops;
FatNode *vqeaf_fat_alloc_node(VqeafFat16Mount *mnt);
int vqeaf_fat16_mount(uint32_t dev_id,uint64_t base_lba,VqeafFat16Mount *out);
int vqeaf_fat16_lookup(VqeafFat16Mount *mnt,VqeafVnode *dir,const char *name,VqeafVnode **out);
int vqeaf_fat16_walk(VqeafFat16Mount *mnt,const char *path,VqeafVnode **out);
/* Write support v0.3.2: overwrite an existing root-level file in place
 * (size may shrink, never grow) or create a new root-level file, allocating
 * clusters from the FAT and flushing FAT copies, the directory sector and
 * the data clusters. Subdirectory creation is implemented by
 * vqeaf_fat16_mkdir. */
int vqeaf_fat16_write_file(VqeafFat16Mount *mnt,const char *name,const void *data,uint32_t len);
/* Create a subdirectory at the given path under the mount root. Path may be
 * nested (e.g. "VQEAF/system/config"); intermediate directories are created
 * as needed. Returns 0 on success, negative on error. */
int vqeaf_fat16_mkdir(VqeafFat16Mount *mnt,const char *path);
#endif
