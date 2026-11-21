#include "../include/vqeaf/vfs.h"
/* Built-in kernel root namespace for the independent kernel.
 *
 * There is no disk file system on the AArch64 port yet (the virtio block
 * driver exposes raw sectors only), so the security model is anchored on a
 * small RAM tree that has real ownership:
 *
 *   /system/version        uid 0    mode 0444   read-only, world-readable
 *   /data/<uid>/private    uid <uid> mode 0600  read/write, owner only
 *
 * The per-uid file is the boundary proof: an app can open its own file and is
 * refused another app's file by vfs.c's mode check, not by a name check.
 * Contents live in RAM and do not survive a reboot; persistence is the block
 * layer's job and is not claimed here. */
static char version_text[] = "VQEAF OS aarch64 0.2\n";
static char app_store[VQEAF_UID_APP_SLOTS][256];
static VqeafVnode nodes[1 + VQEAF_UID_APP_SLOTS];
static VqeafVnode ram_root;

#define RAMFS_NODE_FILE 1u
#define RAMFS_NODE_DIR  2u

static int64_t ram_read(VqeafVnode *n, uint64_t off, void *buf, uint64_t size)
{
    if(!n || (!buf && size)) return -1;
    if(off >= n->size) return 0;
    uint64_t avail = n->size - off;
    uint64_t count = size < avail ? size : avail;
    const uint8_t *src = (const uint8_t *)n->private_data + off;
    for(uint64_t i = 0; i < count; i++) ((uint8_t *)buf)[i] = src[i];
    return (int64_t)count;
}

static int64_t ram_write(VqeafVnode *n, uint64_t off, const void *buf, uint64_t size)
{
    if(!n || !n->private_data || (!buf && size)) return -1;
    if(off >= 256u) return VQEAF_ENOSPC;
    uint64_t room = 256u - off;
    uint64_t count = size < room ? size : room;
    uint8_t *dst = (uint8_t *)n->private_data + off;
    for(uint64_t i = 0; i < count; i++) dst[i] = ((const uint8_t *)buf)[i];
    if(off + count > n->size) n->size = off + count;
    return (int64_t)count;
}

static int parse_uid(const char *p, uint32_t *out)
{
    uint64_t v = 0;
    int digits = 0;
    while(*p >= '0' && *p <= '9') {
        v = v * 10u + (uint32_t)(*p - '0');
        if(v > 0xFFFFFFFFull) return -1;
        p++; digits++;
    }
    if(!digits) return -1;
    *out = (uint32_t)v;
    return 0;
}

static int path_eq(const char *a, const char *b)
{
    while(*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int ram_lookup(VqeafVnode *root, const char *path, VqeafVnode **out)
{
    (void)root;
    if(!path || !out) return VQEAF_EINVAL;
    if(path_eq(path, "/system/version")) { *out = &nodes[0]; return 0; }
    /* /data/<uid>/private, exactly. No intermediate directory traversal, so a
     * path component can never be interpreted as a uid by another route. */
    const char *p = path;
    if(p[0] != '/' || p[1] != 'd' || p[2] != 'a' || p[3] != 't' || p[4] != 'a' || p[5] != '/')
        return VQEAF_ENOENT;
    p += 6;
    uint32_t uid = 0;
    if(parse_uid(p, &uid) != 0) return VQEAF_ENOENT;
    while(*p >= '0' && *p <= '9') p++;
    if(!path_eq(p, "/private")) return VQEAF_ENOENT;
    if(uid < VQEAF_UID_FIRST_APP || uid >= VQEAF_UID_FIRST_APP + VQEAF_UID_APP_SLOTS)
        return VQEAF_ENOENT;
    *out = &nodes[1 + (uid - VQEAF_UID_FIRST_APP)];
    return 0;
}

static const VqeafVfsOps ram_root_ops = { ram_lookup, 0, 0, 0 };
static const VqeafVfsOps ram_version_ops = { 0, ram_read, 0, 0 };
static const VqeafVfsOps ram_private_ops = { 0, ram_read, ram_write, 0 };

void vqeaf_ramfs_init(void)
{
    nodes[0].ops = &ram_version_ops;
    nodes[0].private_data = version_text;
    nodes[0].size = (uint64_t)sizeof(version_text) - 1u;
    nodes[0].type = RAMFS_NODE_FILE; nodes[0].flags = 0;
    nodes[0].uid = VQEAF_UID_SYSTEM; nodes[0].gid = VQEAF_GID_SYSTEM;
    nodes[0].mode = VQEAF_MODE_RWX(VQEAF_MODE_READ); /* 0444 */

    for(uint32_t i = 0; i < VQEAF_UID_APP_SLOTS; i++) {
        VqeafVnode *n = &nodes[1 + i];
        n->ops = &ram_private_ops;
        n->private_data = app_store[i];
        n->size = 0;
        n->type = RAMFS_NODE_FILE; n->flags = 0;
        n->uid = VQEAF_UID_FIRST_APP + i;
        n->gid = VQEAF_UID_FIRST_APP + i;
        n->mode = VQEAF_MODE_OWNER(VQEAF_MODE_READ | VQEAF_MODE_WRITE); /* 0600 */
    }

    ram_root.ops = &ram_root_ops;
    ram_root.private_data = 0;
    ram_root.size = 0;
    ram_root.type = RAMFS_NODE_DIR;
    ram_root.flags = 0;
    ram_root.uid = VQEAF_UID_SYSTEM;
    ram_root.gid = VQEAF_GID_SYSTEM;
    ram_root.mode = VQEAF_MODE_RWX(VQEAF_MODE_READ | VQEAF_MODE_EXEC); /* 0555 */
    vqeaf_vfs_set_root(&ram_root);
}
