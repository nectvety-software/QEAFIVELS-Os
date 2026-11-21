/* SPDX-License-Identifier: MIT */
/* platform/violet/fat_ro.h - read-only FAT16/FAT32 access for firmware blobs. */
#ifndef VIO_FAT_RO_H
#define VIO_FAT_RO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Describes the BPB of `device` in `info`; 0 when it is a FAT volume. */
int fat_ro_probe(const char *device, char *info, int cap);

/* Copies every regular file of directory `dir_name` (8.3, case-insensitive)
 * from the volume into `dest_dir`, lowercasing the names so request_firmware
 * finds them. Returns 0 on success. */
int fat_ro_copy_dir(const char *device, const char *dir_name,
                    const char *dest_dir, int *copied,
                    unsigned long long *bytes);

#ifdef __cplusplus
}
#endif

#endif /* VIO_FAT_RO_H */
