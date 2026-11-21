/* SPDX-License-Identifier: MIT */
/* fs/fat16.h - FAT16 filesystem in RAM (for the USB mass storage LUN).
 *
 * The rescue volume is built here, byte by byte, and served straight from RAM
 * by bsp/violet/usb_gadget.c. Everything a host reads off the drive - boot
 * sector, both FATs, the root directory, subdirectory clusters - comes from
 * this file, so the geometry written and the entries appended have to agree
 * exactly or Windows sees an empty or corrupt disk.
 */

#ifndef UMOS_FAT16_H
#define UMOS_FAT16_H

#include <stdint.h>
#include <stddef.h>

#define FAT16_VOLUME_SIZE (32 * 1024 * 1024)  /* 32 MiB */
#define FAT16_SECTOR_SIZE 512
#define FAT16_LABEL "QEAFRESCUE"
#define FAT16_ROOT_ENTRIES 512

/* Directory entry attributes. Only the four this volume actually writes. */
#define FAT16_ATTR_READ_ONLY 0x01
#define FAT16_ATTR_VOLUME    0x08  /* the one label entry in the root */
#define FAT16_ATTR_DIR       0x10
#define FAT16_ATTR_ARCHIVE   0x20
#define FAT16_ATTR_LFN       0x0F  /* a long-name fragment, not an entry */

#define FAT16_ENTRY_SIZE 32
#define FAT16_LFN_LAST 0x40        /* OR'd into the sequence byte */
#define FAT16_EOF 0xFFFF           /* end of a cluster chain */

struct fat16_volume {
    uint8_t *data;
    size_t size;
    uint32_t fat_start;
    uint32_t fat_size;
    uint32_t root_dir_start;
    uint32_t root_dir_sectors;
    uint32_t data_start;
    uint32_t total_clusters;
    uint8_t sectors_per_cluster;
    uint8_t num_fats;
    uint16_t reserved_sectors;
    uint16_t bytes_per_sector;
    /* Allocation state the builder keeps between calls. Cluster 2 is the
     * first data cluster; entries are handed out from the root in order. */
    uint32_t next_cluster;
    uint32_t root_entries_used;
};

int fat16_init(struct fat16_volume *vol, uint8_t *data, size_t size);
int fat16_read_sector(struct fat16_volume *vol, uint32_t lba, uint8_t *buf);
int fat16_write_sector(struct fat16_volume *vol, uint32_t lba, const uint8_t *buf);
uint32_t fat16_total_sectors(struct fat16_volume *vol);

/* Adds a subdirectory of the root, with the name exactly as it should appear
 * in a file manager. Names that do not fit the 8.3 short form get a long-name
 * record in front of their short entry, so Windows shows "Documents" while a
 * reader that only understands 8.3 still sees a valid DOCUME~1.
 * Returns the cluster it occupies, or -1. */
int fat16_mkdir(struct fat16_volume *vol, const char *name);
int fat16_mkdir_in(struct fat16_volume *vol, uint32_t parent_cluster, const char *name);

/* Creates (or replaces in place) a small root file. Used by the sync
 * protocol: the phone publishes DEVINFO.TXT so a PC tool reads machine
 * state instead of scraping a 64 KiB log ring. `len` must fit the cluster
 * chain already allocated on the first call; returns 0 or -1. */
int fat16_write_file(struct fat16_volume *vol, const char *name,
                     const uint8_t *data, size_t len);

/* Reads a root file back by name. Returns the byte count, or -1. Used by the
 * rescue command loop to see the QEAF.CMD a host just dropped on the LUN. */
int fat16_read_file(struct fat16_volume *vol, const char *name,
                    uint8_t *out, size_t cap);

/* Size of a root file in bytes, or -1. Lets read_file allocate exactly once
 * instead of guessing a capacity. */
int fat16_file_size(struct fat16_volume *vol, const char *name);

/* Streaming read of a root file by cluster chain. The rescue flash verb walks
 * a 20 MiB BOOTIMG.BIN through this: each call hands back the next run of
 * bytes that is physically contiguous in the volume image, pointing straight
 * into it, so no second 20 MiB buffer exists and a file the host scattered
 * over the drive costs a few writes instead of being refused. */
struct fat16_file {
    uint32_t size;     /* file size in bytes */
    uint32_t offset;   /* bytes already handed out */
    uint32_t cluster;  /* next cluster to visit */
};

/* Opens a root file for streaming. Returns 0, or -1 when the name is absent,
 * empty or its first cluster is unusable. */
int fat16_file_open(struct fat16_volume *vol, const char *name,
                    struct fat16_file *out);

/* Next contiguous run: its length (pointed to by *chunk) when > 0, 0 at the
 * end of the file, -1 if the chain breaks before the size is reached. */
int fat16_file_next(struct fat16_volume *vol, struct fat16_file *file,
                    const uint8_t **chunk);

/* A root entry as the sync page shows it: long name when the volume has one,
 * otherwise the 8.3 form, plus the byte size and the directory flag. */
typedef struct {
    char name[64];
    uint32_t size;
    uint8_t is_dir;
} fat16_entry_t;

int fat16_list_root(struct fat16_volume *vol, fat16_entry_t *out, int max);

/* Cluster-aware browsing for the file manager's rescue tab. cluster 0 is the
 * root; a subdirectory's cluster comes from fat16_subdir_cluster. The path
 * helpers resolve "/a/b" against the volume so the UI can navigate and read
 * nested entries, not just the root. */
int fat16_list_dir(struct fat16_volume *vol, uint32_t cluster,
                   fat16_entry_t *out, int max);
int fat16_subdir_cluster(struct fat16_volume *vol, uint32_t cluster,
                         const char *name);
int fat16_read_file_in(struct fat16_volume *vol, uint32_t cluster,
                       const char *name, uint8_t *out, size_t cap);
int fat16_list_path(struct fat16_volume *vol, const char *path,
                    fat16_entry_t *out, int max);
int fat16_read_path(struct fat16_volume *vol, const char *path,
                    uint8_t *out, size_t cap);

#endif
