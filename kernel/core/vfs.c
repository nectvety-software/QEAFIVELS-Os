#include "../include/vqeaf/vfs.h"
#include "../include/vqeaf/scheduler.h"
int vqeaf_vfs_validate_path(const char*p){if(!p||p[0]!='/')return -1;for(uint64_t i=0;p[i];i++){if((unsigned char)p[i]<32)return -2;if(p[i]=='.'&&p[i+1]=='.'&&(i==0||p[i-1]=='/')&&(p[i+2]=='/'||p[i+2]==0))return -3;}return 0;}
int64_t vqeaf_vfs_read(VqeafVnode*n,uint64_t o,void*b,uint64_t s){if(!n||!n->ops||!n->ops->read||(!b&&s))return -1;return n->ops->read(n,o,b,s);}
int64_t vqeaf_vfs_write(VqeafVnode*n,uint64_t o,const void*b,uint64_t s){if(!n||!n->ops||!n->ops->write||(!b&&s))return -1;return n->ops->write(n,o,b,s);}

/* ---- Per-task handle table ---------------------------------------------- */
/* The credential is bound at open time. read/write verify the caller still
 * has that exact identity AND re-check the vnode mode using the bound copy,
 * so a handle cannot be lent to another app or survive a credential change. */
typedef struct { VqeafVnode *node; uint64_t offset; uint32_t flags; uint8_t used;
                 VqeafCred cred; } VfsHandle;
static VfsHandle handles[VQEAF_TASK_CAPACITY][VQEAF_VFS_HANDLE_MAX];
static VqeafVnode *vfs_root;

void vqeaf_vfs_set_root(VqeafVnode *root) { vfs_root = root; }

/* Unix-style owner/group/other mode check. VQEAF_CAP_FS_ADMIN is the only
 * bypass, and app credentials never carry it, so an app can never read another
 * app's 0600 file even if it guesses the path. */
static int access_ok(const VqeafCred *cred, const VqeafVnode *node, uint32_t want)
{
    if(!cred || !node) return 0;
    if(cred->caps & VQEAF_CAP_FS_ADMIN) return 1;
    uint32_t shift = 0;
    if(cred->uid == node->uid) shift = 6;
    else if(cred->gid == node->gid) shift = 3;
    return (((node->mode >> shift) & 7u) & want) == want;
}

static int slot_ok(int slot) { return slot >= 0 && slot < (int)VQEAF_TASK_CAPACITY; }

int vqeaf_vfs_handle_open(int slot, const VqeafCred *cred, const char *path, uint32_t flags)
{
    if(!slot_ok(slot) || !cred || !path) return VQEAF_EINVAL;
    if(!(flags & (VQEAF_VFS_OPEN_READ | VQEAF_VFS_OPEN_WRITE))) return VQEAF_EINVAL;
    if(flags & ~(VQEAF_VFS_OPEN_READ | VQEAF_VFS_OPEN_WRITE)) return VQEAF_EINVAL;
    if(vqeaf_vfs_validate_path(path) != 0) return VQEAF_EINVAL;
    if(!vfs_root || !vfs_root->ops || !vfs_root->ops->lookup) return VQEAF_ENOENT;
    VqeafVnode *node = 0;
    if(vfs_root->ops->lookup(vfs_root, path, &node) != 0 || !node) return VQEAF_ENOENT;
    if(flags & VQEAF_VFS_OPEN_READ) {
        if(!node->ops || !node->ops->read) return VQEAF_EACCES;
        if(!access_ok(cred, node, VQEAF_MODE_READ)) return VQEAF_EACCES;
    }
    if(flags & VQEAF_VFS_OPEN_WRITE) {
        if(!node->ops || !node->ops->write) return VQEAF_EACCES;
        if(!access_ok(cred, node, VQEAF_MODE_WRITE)) return VQEAF_EACCES;
    }
    for(int h = 0; h < (int)VQEAF_VFS_HANDLE_MAX; h++) {
        if(handles[slot][h].used) continue;
        handles[slot][h].node = node;
        handles[slot][h].offset = 0;
        handles[slot][h].flags = flags;
        handles[slot][h].cred = *cred;
        handles[slot][h].used = 1;
        return h;
    }
    return VQEAF_EMFILE;
}

static VfsHandle *handle_get(int slot, int h)
{
    if(!slot_ok(slot) || h < 0 || h >= (int)VQEAF_VFS_HANDLE_MAX) return 0;
    if(!handles[slot][h].used) return 0;
    return &handles[slot][h];
}

int64_t vqeaf_vfs_handle_read(int slot, const VqeafCred *cred, int h, void *buf, uint64_t size)
{
    VfsHandle *fh = handle_get(slot, h);
    if(!fh || !cred || (!buf && size)) return VQEAF_EBADF;
    if(!vqeaf_sec_cred_eq(&fh->cred, cred)) return VQEAF_EACCES;
    if(!(fh->flags & VQEAF_VFS_OPEN_READ)) return VQEAF_EACCES;
    if(!fh->node->ops || !fh->node->ops->read) return VQEAF_EACCES;
    if(!access_ok(&fh->cred, fh->node, VQEAF_MODE_READ)) return VQEAF_EACCES;
    int64_t n = fh->node->ops->read(fh->node, fh->offset, buf, size);
    if(n > 0) fh->offset += (uint64_t)n;
    return n;
}

int64_t vqeaf_vfs_handle_write(int slot, const VqeafCred *cred, int h, const void *buf, uint64_t size)
{
    VfsHandle *fh = handle_get(slot, h);
    if(!fh || !cred || (!buf && size)) return VQEAF_EBADF;
    if(!vqeaf_sec_cred_eq(&fh->cred, cred)) return VQEAF_EACCES;
    if(!(fh->flags & VQEAF_VFS_OPEN_WRITE)) return VQEAF_EACCES;
    if(!fh->node->ops || !fh->node->ops->write) return VQEAF_EACCES;
    if(!access_ok(&fh->cred, fh->node, VQEAF_MODE_WRITE)) return VQEAF_EACCES;
    int64_t n = fh->node->ops->write(fh->node, fh->offset, buf, size);
    if(n > 0) fh->offset += (uint64_t)n;
    return n;
}

int vqeaf_vfs_handle_close(int slot, int h)
{
    VfsHandle *fh = handle_get(slot, h);
    if(!fh) return VQEAF_EBADF;
    fh->node = 0; fh->offset = 0; fh->flags = 0; fh->used = 0;
    fh->cred.uid = 0; fh->cred.gid = 0; fh->cred.caps = 0;
    return 0;
}

void vqeaf_vfs_release_task(int slot)
{
    if(!slot_ok(slot)) return;
    for(int h = 0; h < (int)VQEAF_VFS_HANDLE_MAX; h++) {
        handles[slot][h].node = 0; handles[slot][h].offset = 0;
        handles[slot][h].flags = 0; handles[slot][h].used = 0;
        handles[slot][h].cred.uid = 0; handles[slot][h].cred.gid = 0;
        handles[slot][h].cred.caps = 0;
    }
}
