#ifndef VQEAF_STORAGE_LAYOUT_H
#define VQEAF_STORAGE_LAYOUT_H

/* Standard storage directory structure for VQEAF OS.
 *
 * Mirrors the SDCARD/LUN layout from QeafdropOs (PROMPT.md, fs/storage.c):
 * three top-level volumes VQEAF, UserData, Install, each with nested
 * system/application/framework/theme/boot/driver/service/update directories.
 *
 * This is a data definition only; storage_layout.c publishes the tree onto a
 * FAT16 volume via fat16_mkdir_in.
 */

/* Top-level storage roots. */
#define STORAGE_ROOT_COUNT 3
#define STORAGE_ROOT_VQEAF  0
#define STORAGE_ROOT_USER   1
#define STORAGE_ROOT_INST   2

/* VQEAF subtree: system/settings/... */
#define STORAGE_VQEAF_SUB_COUNT 9
extern const char *const STORAGE_VQEAF_SUB[];

/* UserData subtree: Documents, Downloads, ... */
#define STORAGE_USER_SUB_COUNT 7
extern const char *const STORAGE_USER_SUB[];

/* Install subtree: Apps, Themes, Drivers, Updates */
#define STORAGE_INSTALL_SUB_COUNT 4
extern const char *const STORAGE_INSTALL_SUB[];

/* Publish the standard directory tree onto a FAT16 volume.
 * Returns 0 on success, negative on error. */
int vqeaf_storage_publish_layout(void *fat_mount);

#endif /* VQEAF_STORAGE_LAYOUT_H */
