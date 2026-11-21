#ifndef VQEAF_STORAGE_H
#define VQEAF_STORAGE_H
#include <stdint.h>
#include "vqeaf/vfs.h"
#include "vqeaf/fs_fat16.h"
#include <stdint.h>
#define VQEAF_MOUNT_MAX 8u
typedef struct { const char *name;VqeafVnode *root;uint32_t flags; } VqeafMount;
int vqeaf_storage_mount(const char *path,VqeafVnode *root,uint32_t flags);
int vqeaf_storage_unmount(const char *path);
/* Mounts a FAT16 volume (block device id + base LBA) at a mount path. */
int vqeaf_storage_mount_fat16(uint32_t dev_id,uint64_t base_lba,const char *path,uint32_t flags);
int vqeaf_storage_resolve(const char *path,VqeafVnode **node,const char **relative);
/* Resolve a path and walk the relative part inside a FAT16 mount. Generic
 * mounts (opaque roots without a filesystem behind them) return -3 when the
 * relative part is non-empty. */
int vqeaf_storage_walk(const char *path,VqeafVnode **node);
void vqeaf_storage_reset(void);
/* Return the VqeafFat16Mount for the volume mounted at path, or NULL if not
 * a FAT16 mount or not found. Used by the rescue channel to read/write
 * command files directly on the volume. */
VqeafFat16Mount *vqeaf_storage_fat_mount(const char *path);
#endif
