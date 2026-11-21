#ifndef VQEAF_VFS_H
#define VQEAF_VFS_H
#include <stdint.h>
#include "security.h"
#include "el0_abi.h"
#define VQEAF_VFS_HANDLE_MAX 8u
#define VQEAF_VFS_OPEN_READ  VQEAF_O_RDONLY
#define VQEAF_VFS_OPEN_WRITE VQEAF_O_WRONLY
#define VQEAF_MODE_READ  4u
#define VQEAF_MODE_WRITE 2u
#define VQEAF_MODE_EXEC  1u
/* Node modes are nine-bit rwx triplets, exactly like a Unix mode: build them
 * with these helpers so an owner bit is never mistaken for an other bit. */
#define VQEAF_MODE_OWNER(rwx) (((rwx) & 7u) << 6)
#define VQEAF_MODE_GROUP(rwx) (((rwx) & 7u) << 3)
#define VQEAF_MODE_OTHER(rwx) ((rwx) & 7u)
#define VQEAF_MODE_RWX(rwx) (VQEAF_MODE_OWNER(rwx) | VQEAF_MODE_GROUP(rwx) | VQEAF_MODE_OTHER(rwx))
typedef struct VqeafVnode VqeafVnode;
typedef struct { int (*lookup)(VqeafVnode*,const char*,VqeafVnode**); int64_t (*read)(VqeafVnode*,uint64_t,void*,uint64_t); int64_t (*write)(VqeafVnode*,uint64_t,const void*,uint64_t); int (*close)(VqeafVnode*); } VqeafVfsOps;
struct VqeafVnode { const VqeafVfsOps *ops; void *private_data; uint64_t size; uint32_t type,flags;
                    uint32_t uid,gid,mode; };
int vqeaf_vfs_validate_path(const char *path);int64_t vqeaf_vfs_read(VqeafVnode *node,uint64_t offset,void *buffer,uint64_t size);int64_t vqeaf_vfs_write(VqeafVnode *node,uint64_t offset,const void *buffer,uint64_t size);

/* ---- Kernel VFS handles (F4) ------------------------------------------------
 * A handle names an open file inside the handle table of ONE task slot and is
 * bound to the credential that opened it: read/write refuse a caller whose
 * uid/gid/caps differ, even if it somehow reaches the same slot and handle
 * number. Open checks the vnode's owner and mode against the caller's
 * credential; read/write re-check it against the bound copy, so a capability
 * alone cannot cross an app boundary.
 * Paths are canonical (vqeaf_vfs_validate_path) and resolved from the single
 * mounted root; there is no cross-mount traversal. */
void vqeaf_vfs_set_root(VqeafVnode *root);
int vqeaf_vfs_handle_open(int slot, const VqeafCred *cred, const char *path, uint32_t flags);
int64_t vqeaf_vfs_handle_read(int slot, const VqeafCred *cred, int handle, void *buffer, uint64_t size);
int64_t vqeaf_vfs_handle_write(int slot, const VqeafCred *cred, int handle, const void *buffer, uint64_t size);
int vqeaf_vfs_handle_close(int slot, int handle);
/* Called when a task exits: closes every handle the slot still owns. */
void vqeaf_vfs_release_task(int slot);

/* Built-in root namespace: /system/version (0444, system-owned) and one
 * private 0600 RAM file per app uid at /data/<uid>/private. RAM only: contents
 * do not survive a reboot (persistence belongs to the block layer). */
void vqeaf_ramfs_init(void);
#endif
