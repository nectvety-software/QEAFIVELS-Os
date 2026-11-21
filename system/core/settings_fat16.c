/* SPDX-License-Identifier: MIT */
/* system/core/settings_fat16.c - durable Settings Service backend over a
 * mounted FAT16 volume. The image is a fixed 512-byte root file so each save
 * is an in-place overwrite (the FAT16 writer allows create/shrink/equal, never
 * growth), and a fresh mount of the same device reads the last saved image —
 * which is exactly what a reboot does. */
#include "vqeaf/settings.h"
#include "vqeaf/settings_fat16.h"

#include <stddef.h>

#define PREFS_FILE "PREFS.DAT"

typedef struct {
    VqeafFat16Mount *mnt;
} fat_ctx_t;

static fat_ctx_t ctx;

static int fat_save(void *c, const uint8_t *blob, uint32_t len)
{
    fat_ctx_t *fc = (fat_ctx_t *)c;
    if(fc == NULL || fc->mnt == NULL || blob == NULL) return -1;
    int r = vqeaf_fat16_write_file(fc->mnt, PREFS_FILE, blob, len);
    return r == (int)len ? 0 : -1;
}

static int fat_load(void *c, uint8_t *blob, uint32_t cap, uint32_t *len)
{
    fat_ctx_t *fc = (fat_ctx_t *)c;
    if(fc == NULL || fc->mnt == NULL || blob == NULL || len == NULL) return -1;
    VqeafVnode *node = NULL;
    if(vqeaf_fat16_walk(fc->mnt, "/" PREFS_FILE, &node) != 0 || node == NULL)
        return -1;                   /* no image yet: first boot */
    int64_t got = vqeaf_vfs_read(node, 0, blob, cap);
    if(got < 0) return -1;
    *len = (uint32_t)got;
    return 0;
}

int vqeaf_settings_attach_fat16(VqeafFat16Mount *mnt)
{
    if(mnt == NULL) return -1;
    ctx.mnt = mnt;
    vqeaf_settings_backend_t b;
    b.save = fat_save;
    b.load = fat_load;
    b.ctx = &ctx;
    return vqeaf_settings_attach_backend(&b);
}