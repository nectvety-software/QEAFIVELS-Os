/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_settings.c - host gate for the VQEAF Settings Service.
 *
 * Proves, over a real RAM-backed writable FAT16 volume, that:
 *   - with no backend the store is session-only (reset loses it);
 *   - typed get/set honour defaults and type mismatches;
 *   - attaching the FAT16 backend writes through and a fresh mount of the same
 *     device (== reboot) reloads the saved values, including bold-text;
 *   - the prefs image is a fixed 512-byte in-place overwrite;
 *   - a tampered image is rejected (checksum) and the store starts empty;
 *   - the store is bounded (key length, value length, entry count).
 * Nothing here touches physical hardware; the disk is a host RAM array.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../system/core/include/vqeaf/block.h"
#include "../system/core/include/vqeaf/fs_fat16.h"
#include "../system/core/include/vqeaf/settings.h"
#include "../system/core/include/vqeaf/settings_fat16.h"
#include "../kernel/include/vqeaf/vfs.h"

#define DISK_SECTORS 8192u
#define VOL_BASE 8u
#define DATA_LBA (VOL_BASE + 1u + 2u + 1u)   /* reserved 1 + FAT 2x1 + root 1 */

static uint8_t disk[DISK_SECTORS][512];

static int rd_read(struct VqeafBlockDevice *d, uint64_t lba, uint32_t n, void *out)
{
    (void)d;
    if(lba > DISK_SECTORS || DISK_SECTORS - lba < n) return -1;
    memcpy(out, disk + lba, (size_t)n * 512u);
    return 0;
}
static int rd_write(struct VqeafBlockDevice *d, uint64_t lba, uint32_t n, const void *in)
{
    (void)d;
    if(lba > DISK_SECTORS || DISK_SECTORS - lba < n) return -1;
    memcpy(disk + lba, in, (size_t)n * 512u);
    return 0;
}
static void wr16(uint8_t *p, uint16_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }

/* A minimal writable FAT16 volume with an empty root directory. */
static void build_fat16(void)
{
    memset(disk, 0, sizeof(disk));
    uint8_t *bpb = disk[VOL_BASE];
    bpb[0]='V'; bpb[1]='Q'; bpb[2]='F'; bpb[3]='1';
    wr16(bpb+11, 512); bpb[13]=1; wr16(bpb+14, 1); bpb[16]=2; wr16(bpb+17, 16);
    wr16(bpb+19, 8184); bpb[21]=0xF8; wr16(bpb+22, 1);
    wr16(bpb+510, 0xAA55);
    for(unsigned f = 0; f < 2; f++) {
        uint8_t *fat = disk[VOL_BASE + 1 + f];
        wr16(fat+0, 0xFFF8u); wr16(fat+2, 0xFFFFu);   /* media + reserved EOC */
    }
    /* root dir (VOL_BASE+3) stays zeroed: no files yet */
}

static struct VqeafBlockDevice ram = {
    "ram0", 512u, DISK_SECTORS, rd_read, rd_write, NULL
};

static void fresh_disk(void)
{
    build_fat16();
    vqeaf_block_reset();
    assert(vqeaf_block_register(&ram) == 0);
}

/* A reboot: drop all RAM state and mount the same device again. */
static void reboot(VqeafFat16Mount *mnt)
{
    vqeaf_settings_reset();
    memset(mnt, 0, sizeof(*mnt));
    assert(vqeaf_fat16_mount(0, VOL_BASE, mnt) == 0);
    assert(vqeaf_settings_attach_fat16(mnt) == 0);
    assert(vqeaf_settings_persistent() == 1);
}

int main(void)
{
    /* ---- 1. session-only without a backend ---- */
    vqeaf_settings_reset();
    assert(vqeaf_settings_persistent() == 0);
    assert(vqeaf_settings_get_bool("display.bold_text", 0) == 0);
    assert(vqeaf_settings_set_bool("display.bold_text", 1) == 0);
    assert(vqeaf_settings_get_bool("display.bold_text", 0) == 1);
    assert(vqeaf_settings_set_int("ui.scale", 120) == 0);
    assert(vqeaf_settings_get_int("ui.scale", 0) == 120);
    assert(vqeaf_settings_set_str("locale", "vi-VN") == 0);
    assert(strcmp(vqeaf_settings_get_str("locale", ""), "vi-VN") == 0);
    /* defaults and type mismatches */
    assert(vqeaf_settings_get_bool("absent", 7) == 7);
    assert(vqeaf_settings_get_int("display.bold_text", 9) == 9);   /* bool != int */
    assert(strcmp(vqeaf_settings_get_str("ui.scale", "d"), "d") == 0);
    /* reset loses session state */
    vqeaf_settings_reset();
    assert(vqeaf_settings_get_bool("display.bold_text", 0) == 0);
    assert(vqeaf_settings_get_int("ui.scale", 0) == 0);

    /* ---- 2. durable persistence across a reboot ---- */
    fresh_disk();
    VqeafFat16Mount mnt;
    memset(&mnt, 0, sizeof(mnt));
    assert(vqeaf_fat16_mount(0, VOL_BASE, &mnt) == 0);
    vqeaf_settings_reset();
    assert(vqeaf_settings_attach_fat16(&mnt) == 0);
    assert(vqeaf_settings_persistent() == 1);
    /* first boot: no image yet, defaults apply */
    assert(vqeaf_settings_get_bool("display.bold_text", 0) == 0);
    assert(vqeaf_settings_set_bool("display.bold_text", 1) == 0);
    assert(vqeaf_settings_set_int("ui.scale", 120) == 0);
    assert(vqeaf_settings_set_str("locale", "vi-VN") == 0);

    /* the prefs image is a fixed 512-byte root file */
    VqeafVnode *prefs = NULL;
    assert(vqeaf_fat16_walk(&mnt, "/PREFS.DAT", &prefs) == 0 && prefs);
    assert(prefs->size == VQEAF_SETTINGS_BLOB_SIZE);

    reboot(&mnt);
    assert(vqeaf_settings_get_bool("display.bold_text", 0) == 1);
    assert(vqeaf_settings_get_int("ui.scale", 0) == 120);
    assert(strcmp(vqeaf_settings_get_str("locale", ""), "vi-VN") == 0);

    /* toggle bold off, reboot, it stays off */
    assert(vqeaf_settings_set_bool("display.bold_text", 0) == 0);
    reboot(&mnt);
    assert(vqeaf_settings_get_bool("display.bold_text", 1) == 0);
    assert(vqeaf_settings_get_int("ui.scale", 0) == 120);   /* others survive */
    assert(vqeaf_fat16_walk(&mnt, "/PREFS.DAT", &prefs) == 0);
    assert(prefs->size == VQEAF_SETTINGS_BLOB_SIZE);        /* still fixed size */

    /* ---- 3. a tampered image is rejected, store starts empty ---- */
    assert(vqeaf_settings_set_bool("display.bold_text", 1) == 0);
    disk[DATA_LBA][0] ^= 0xFF;          /* corrupt the magic of cluster 2 */
    reboot(&mnt);
    assert(vqeaf_settings_get_bool("display.bold_text", 0) == 0);  /* rejected */
    assert(vqeaf_settings_persistent() == 1);                     /* still usable */
    /* a fresh set writes a clean image that survives the next reboot */
    assert(vqeaf_settings_set_bool("display.bold_text", 1) == 0);
    reboot(&mnt);
    assert(vqeaf_settings_get_bool("display.bold_text", 0) == 1);

    /* ---- 4. bounds: key length, value length, entry count ---- */
    vqeaf_settings_reset();
    char longkey[VQEAF_SETTINGS_KEY_MAX + 4];
    memset(longkey, 'k', sizeof(longkey)); longkey[sizeof(longkey)-1] = 0;
    assert(vqeaf_settings_set_bool(longkey, 1) == -1);
    assert(vqeaf_settings_set_bool(NULL, 1) == -1);
    char longval[VQEAF_SETTINGS_STR_MAX + 4];
    memset(longval, 'v', sizeof(longval)); longval[sizeof(longval)-1] = 0;
    assert(vqeaf_settings_set_str("k", longval) == -1);
    assert(vqeaf_settings_set_str("k", NULL) == -1);
    char key[8];
    for(unsigned i = 0; i < VQEAF_SETTINGS_MAX; i++) {
        snprintf(key, sizeof(key), "k%02u", i);
        assert(vqeaf_settings_set_bool(key, 1) == 0);
    }
    assert(vqeaf_settings_set_bool("overflow", 1) == -1);   /* store full */

    printf("all settings service tests passed\n");
    return 0;
}