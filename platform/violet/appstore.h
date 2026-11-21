/* SPDX-License-Identifier: MIT */
/* platform/violet/appstore.h - durable QEAPP catalog in the p77 reserve.
 *
 * QEAFRESCUE is a tmpfs volume rebuilt at every boot, so packages installed
 * from it are lost on reboot. The catalog and package bytes are therefore
 * written to the unused tail of p77 (the recovery partition that already holds
 * the boot image): two variable-size slots that share the 32 MiB reserve, each
 * with its own sequence number and CRC. Slot payloads are allocated by their
 * real size (up to ~16 MiB each), so neither the app count nor a package is
 * limited by a fixed 4 MiB slot. Boot reinstalls the packages straight from
 * the newer slot through the normal signed path.
 */
#ifndef VQEAF_APPSTORE_H
#define VQEAF_APPSTORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APPSTORE_NAME 64

typedef struct {
    char name[APPSTORE_NAME];   /* root name inside QEAFRESCUE */
    uint32_t size;
    /* Path inside the RAM volume (/Install/Apps/x.qeapp). NULL means the file
     * sits at the root. Used to read the new package back from FAT. */
    const char *path;
    /* Optional fallback used when the RAM volume no longer has the file (an
     * app restored from this store after boot). Borrowed by appstore_save. */
    const uint8_t *data;
    size_t data_size;
    /* Per-app data blob (like /data/data/<id>), written with the catalog and
     * carried across an update. NULL/0 when the app has no data. */
    const uint8_t *app_data;
    size_t app_data_size;
    /* User-approved permission subset, separate from app data. */
    uint32_t granted_permissions;
} appstore_entry_t;

struct fat16_volume;

/* The FAT volume is owned by main; these callbacks keep the rescue worker's
 * reads and the catalog's writes from touching it at the same time. */
void appstore_set_volume(struct fat16_volume *vol,
                         void (*lock)(void), void (*unlock)(void));

/* Persists the given packages (read back from the volume by name). Empty list
 * clears the catalog. Returns 0 when the written slot reads back identical. */
int appstore_save(const appstore_entry_t *entries, int count);

/* Returns the package names stored in the newest valid slot (no FAT write). */
int appstore_restore(char names[][APPSTORE_NAME], int max);

/* Allocates and returns the stored package bytes for `name` (malloc; caller
 * frees). This is the launch/restore source after the rescue volume resets. */
int appstore_read(const char *name, uint8_t **out, size_t *size);

/* Per-app data blob, by stored package name (malloc; caller frees). */
int appstore_read_data(const char *name, uint8_t **out, size_t *size);
/* Reads persisted user grants; legacy v2/v3 records migrate to zero grants. */
int appstore_read_grants(const char *name, uint32_t *grants);

/* Nonzero when the RAM volume still holds `name` (so a save can read it from
 * FAT instead of the catalog copy). */
int appstore_volume_has(const char *name);

/* Header-only view of the newest slot, for the rescue `store` verb. */
int appstore_info(uint32_t *seq, uint32_t *count, uint32_t *bytes, int *slot);

/* Set while boot reinstall runs, so installs do not rewrite the catalog. */
void appstore_set_restoring(int restoring);
int appstore_is_restoring(void);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_APPSTORE_H */
