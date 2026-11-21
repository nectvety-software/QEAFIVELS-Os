/* SPDX-License-Identifier: MIT */
/* platform/violet/fat_ro.c - read-only FAT16/FAT32 reader for radio blobs.
 *
 * The violet kernel has vfat built in but not the NLS charsets, so mounting
 * the modem partition (vfat per vendor/etc/fstab.qcom) fails with EINVAL:
 * "IO charset iso8859-1 not found". The blobs are plain 8.3 names inside
 * /image, so this reader copies them into the kernel firmware search path
 * without needing a mount, a module or a charset table.
 *
 * Nothing is written to the partition: the device is opened O_RDONLY and
 * every offset is bounds-checked against the BPB.
 */
#define _GNU_SOURCE

#include "fat_ro.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int fd;
    unsigned bps;
    unsigned spc;
    unsigned reserved;
    unsigned nfats;
    unsigned root_entries;
    unsigned fat_size;
    unsigned root_cluster;
    unsigned data_start;
    unsigned root_start;
    unsigned root_sectors;
    unsigned clusters;
    int fat32;
    uint32_t *fat;
} fat_vol_t;

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned le32(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int read_at(int fd, uint64_t offset, void *buf, size_t length)
{
    size_t done = 0;
    while(done < length) {
        ssize_t n = pread(fd, (uint8_t *)buf + done, length - done,
                          (off_t)(offset + done));
        if(n < 0) {
            if(errno == EINTR) continue;
            return -1;
        }
        if(n == 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

static int fat_open(const char *device, fat_vol_t *vol)
{
    uint8_t boot[512];
    unsigned fat_bytes;

    memset(vol, 0, sizeof(*vol));
    vol->fd = open(device, O_RDONLY | O_CLOEXEC);
    if(vol->fd < 0) return -errno;
    if(read_at(vol->fd, 0, boot, sizeof(boot)) < 0) {
        int err = -errno;
        close(vol->fd);
        vol->fd = -1;
        return err;
    }
    if(boot[510] != 0x55 || boot[511] != 0xAA) return -ENODATA;

    vol->bps = le16(boot + 11);
    vol->spc = boot[13];
    vol->reserved = le16(boot + 14);
    vol->nfats = boot[16];
    vol->root_entries = le16(boot + 17);
    vol->fat_size = le16(boot + 22);
    vol->fat32 = vol->fat_size == 0;
    if(vol->fat32) vol->fat_size = le32(boot + 36);
    vol->root_cluster = vol->fat32 ? le32(boot + 44) : 0;

    if((vol->bps != 512 && vol->bps != 1024 && vol->bps != 2048 &&
        vol->bps != 4096) || vol->spc == 0 || (vol->spc & (vol->spc - 1)) ||
       vol->nfats == 0 || vol->fat_size == 0)
        return -EINVAL;

    vol->root_start = vol->reserved + vol->nfats * vol->fat_size;
    vol->root_sectors = (vol->root_entries * 32 + vol->bps - 1) / vol->bps;
    vol->data_start = vol->root_start + vol->root_sectors;

    fat_bytes = vol->fat_size * vol->bps;
    vol->fat = (uint32_t *)malloc(fat_bytes);
    if(vol->fat == NULL) return -ENOMEM;
    if(read_at(vol->fd, (uint64_t)vol->reserved * vol->bps, vol->fat,
               fat_bytes) < 0) {
        int err = -errno;
        free(vol->fat);
        vol->fat = NULL;
        return err;
    }
    vol->clusters = fat_bytes / 4;
    return 0;
}

static void fat_close(fat_vol_t *vol)
{
    if(vol->fd >= 0) close(vol->fd);
    free(vol->fat);
    vol->fat = NULL;
    vol->fd = -1;
}

static unsigned fat_next(const fat_vol_t *vol, unsigned cluster)
{
    if(cluster >= vol->clusters) return 0x0FFFFFFFu;
    if(vol->fat32) return vol->fat[cluster] & 0x0FFFFFFFu;
    return vol->fat[cluster] & 0xFFFFu;
}

static int fat_is_eoc(const fat_vol_t *vol, unsigned cluster)
{
    return vol->fat32 ? cluster >= 0x0FFFFFF8u : cluster >= 0xFFF8u;
}

static uint64_t cluster_offset(const fat_vol_t *vol, unsigned cluster)
{
    return ((uint64_t)vol->data_start +
            (uint64_t)(cluster - 2) * vol->spc) * vol->bps;
}

static int fat_read_cluster(const fat_vol_t *vol, unsigned cluster,
                            uint8_t *buf)
{
    if(cluster < 2) return -EINVAL;
    return read_at(vol->fd, cluster_offset(vol, cluster), buf,
                   (size_t)vol->spc * vol->bps);
}

/* Copy one 8.3 entry name to lowercase "name.ext". */
static void entry_name(const uint8_t *entry, char *out, size_t cap)
{
    size_t used = 0;
    int i;

    for(i = 0; i < 8 && entry[i] != ' '; i++) {
        if(used + 1 < cap) out[used++] = (char)tolower(entry[i]);
    }
    if(entry[8] != ' ') {
        if(used + 1 < cap) out[used++] = '.';
        for(i = 8; i < 11 && entry[i] != ' '; i++) {
            if(used + 1 < cap) out[used++] = (char)tolower(entry[i]);
        }
    }
    out[used] = '\0';
}

static int name_is(const uint8_t *entry, const char *wanted)
{
    char name[16];
    entry_name(entry, name, sizeof(name));
    return strcmp(name, wanted) == 0;
}

/* Walk a directory's entries; `wanted` NULL means every regular file. */
typedef int (*fat_entry_fn)(const fat_vol_t *vol, const uint8_t *entry,
                            void *context);

static int fat_walk_dir(const fat_vol_t *vol, unsigned dir_cluster,
                        fat_entry_fn fn, void *context)
{
    uint8_t *buf;
    unsigned cluster = dir_cluster;
    unsigned guard = 0;

    buf = (uint8_t *)malloc((size_t)vol->spc * vol->bps);
    if(buf == NULL) return -ENOMEM;

    if(!vol->fat32 && dir_cluster == 0) {
        /* FAT16 fixed root directory. */
        unsigned sector;
        for(sector = 0; sector < vol->root_sectors; sector++) {
            unsigned offset;
            if(read_at(vol->fd,
                       (uint64_t)(vol->root_start + sector) * vol->bps,
                       buf, vol->bps) < 0) {
                free(buf);
                return -errno;
            }
            for(offset = 0; offset + 32 <= vol->bps; offset += 32) {
                const uint8_t *entry = buf + offset;
                if(entry[0] == 0x00) { free(buf); return 0; }
                if(entry[0] == 0xE5 || entry[11] == 0x0F) continue;
                if(fn(vol, entry, context) < 0) { free(buf); return -1; }
            }
        }
        free(buf);
        return 0;
    }

    while(cluster >= 2 && !fat_is_eoc(vol, cluster) && guard++ < 65536) {
        unsigned offset;
        if(fat_read_cluster(vol, cluster, buf) < 0) {
            free(buf);
            return -EIO;
        }
        for(offset = 0; offset + 32 <= (unsigned)vol->spc * vol->bps;
            offset += 32) {
            const uint8_t *entry = buf + offset;
            if(entry[0] == 0x00) { free(buf); return 0; }
            if(entry[0] == 0xE5 || entry[11] == 0x0F) continue;
            if(fn(vol, entry, context) < 0) { free(buf); return -1; }
        }
        cluster = fat_next(vol, cluster);
    }
    free(buf);
    return 0;
}

typedef struct {
    const char *wanted;
    unsigned cluster;
    unsigned size;
    int found;
} find_ctx_t;

static int find_entry_cb(const fat_vol_t *vol, const uint8_t *entry,
                         void *context)
{
    find_ctx_t *find = (find_ctx_t *)context;
    (void)vol;
    if(!name_is(entry, find->wanted)) return 0;
    find->cluster = (unsigned)le16(entry + 26) |
                    ((unsigned)le16(entry + 20) << 16);
    find->size = le32(entry + 28);
    find->found = 1;
    return -1; /* stop the walk */
}

typedef struct {
    const char *dest_dir;
    int copied;
    unsigned long long bytes;
    int max_files;
    int failed;
} copy_ctx_t;

static int copy_entry_cb(const fat_vol_t *vol, const uint8_t *entry,
                         void *context)
{
    copy_ctx_t *copy = (copy_ctx_t *)context;
    char name[16];
    char path[320];
    unsigned cluster, size;
    uint8_t *buf;
    int out;
    unsigned guard = 0;

    if(entry[11] & 0x08) return 0;          /* volume label */
    if(entry[11] & 0x10) return 0;          /* subdirectory */
    if(entry[0] == '.') return 0;
    if(copy->copied >= copy->max_files) return -1;

    entry_name(entry, name, sizeof(name));
    if(name[0] == '\0') return 0;
    cluster = (unsigned)le16(entry + 26) | ((unsigned)le16(entry + 20) << 16);
    size = le32(entry + 28);
    if(cluster < 2 || size == 0) return 0;

    if(snprintf(path, sizeof(path), "%s/%s", copy->dest_dir, name) >=
       (int)sizeof(path))
        return 0;

    out = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if(out < 0) { copy->failed++; return 0; }

    buf = (uint8_t *)malloc((size_t)vol->spc * vol->bps);
    if(buf == NULL) { close(out); copy->failed++; return 0; }

    while(cluster >= 2 && !fat_is_eoc(vol, cluster) && guard++ < 65536) {
        size_t chunk = (size_t)vol->spc * vol->bps;
        if(fat_read_cluster(vol, cluster, buf) < 0) break;
        if(chunk > size) chunk = size;
        if(chunk > 0 && write(out, buf, chunk) != (ssize_t)chunk) break;
        size -= (unsigned)chunk;
        copy->bytes += chunk;
        if(size == 0) break;
        cluster = fat_next(vol, cluster);
    }
    free(buf);
    close(out);
    if(size != 0) {
        copy->failed++;
        unlink(path);
        return 0;
    }
    copy->copied++;
    return 0;
}

int fat_ro_probe(const char *device, char *info, int cap)
{
    uint8_t boot[512];
    int fd = open(device, O_RDONLY | O_CLOEXEC);
    unsigned bps, spc, fat_size, total;
    const char *type = "unknown";
    ssize_t n;

    if(fd < 0) {
        if(info && cap > 0) snprintf(info, (size_t)cap, "open: %s",
                                     strerror(errno));
        return -1;
    }
    n = pread(fd, boot, sizeof(boot), 0);
    close(fd);
    if(n != (ssize_t)sizeof(boot)) {
        if(info && cap > 0) snprintf(info, (size_t)cap, "short read");
        return -1;
    }
    if(boot[510] != 0x55 || boot[511] != 0xAA) {
        if(info && cap > 0)
            snprintf(info, (size_t)cap, "no 0xAA55 (first=%02x %02x)",
                     boot[0], boot[1]);
        return -1;
    }
    bps = le16(boot + 11);
    spc = boot[13];
    fat_size = le16(boot + 22);
    total = le16(boot + 19);
    if(fat_size == 0) {
        type = "FAT32";
        fat_size = le32(boot + 36);
        total = le32(boot + 32);
    }
    else if(total != 0) {
        unsigned clusters = (total - (le16(boot + 14) +
                             le16(boot + 17) / 16 +
                             boot[16] * fat_size)) / spc;
        type = clusters < 4085 ? "FAT12" : "FAT16";
    }
    if(info && cap > 0)
        snprintf(info, (size_t)cap,
                 "%s bps=%u spc=%u fats=%u root=%u fat=%u total=%u",
                 type, bps, spc, boot[16], le16(boot + 17), fat_size, total);
    return strcmp(type, "unknown") == 0 ? -1 : 0;
}

int fat_ro_copy_dir(const char *device, const char *dir_name,
                    const char *dest_dir, int *copied, unsigned long long *bytes)
{
    fat_vol_t vol;
    find_ctx_t find;
    copy_ctx_t copy;
    int rc;

    if(copied) *copied = 0;
    if(bytes) *bytes = 0;
    rc = fat_open(device, &vol);
    if(rc < 0) return rc;

    memset(&find, 0, sizeof(find));
    find.wanted = dir_name;
    rc = fat_walk_dir(&vol, vol.fat32 ? vol.root_cluster : 0,
                      find_entry_cb, &find);
    if(!find.found) {
        fat_close(&vol);
        return -ENOENT;
    }

    memset(&copy, 0, sizeof(copy));
    copy.dest_dir = dest_dir;
    copy.max_files = 128;
    rc = fat_walk_dir(&vol, find.cluster, copy_entry_cb, &copy);
    fat_close(&vol);

    if(copied) *copied = copy.copied;
    if(bytes) *bytes = copy.bytes;
    if(copy.failed) return -EIO;
    return rc < 0 && copy.copied == 0 ? rc : 0;
}
