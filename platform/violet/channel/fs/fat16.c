/* SPDX-License-Identifier: MIT */
/* fs/fat16.c - FAT16 volume builder (in RAM) for the QEAFRESCUE LUN. */

#include "fat16.h"
#include <string.h>
#include "core/log.h"

/* One timestamp for the whole volume: the rescue disk is rebuilt at every
 * boot, so a clock here would only make two boots differ for no reason. */
#define DOS_TIME 0x0000
#define DOS_DATE (((2026 - 1980) << 9) | (11 << 5) | 5)

static uint32_t cluster_sector(const struct fat16_volume *vol, uint32_t cluster) {
    return vol->data_start + (cluster - 2) * vol->sectors_per_cluster;
}

/* Writes one FAT entry into every FAT, because a host may read either one. */
static void fat_set(struct fat16_volume *vol, uint32_t cluster, uint16_t value) {
    uint32_t off = cluster * 2;
    for (uint8_t f = 0; f < vol->num_fats; f++) {
        uint8_t *fat = vol->data + (size_t)(vol->fat_start + f * vol->fat_size) *
                         vol->bytes_per_sector;
        fat[off] = (uint8_t)(value & 0xFF);
        fat[off + 1] = (uint8_t)(value >> 8);
    }
}

static uint8_t *root_entry(struct fat16_volume *vol, uint32_t index) {
    return vol->data + (size_t)vol->root_dir_start * vol->bytes_per_sector +
           (size_t)index * FAT16_ENTRY_SIZE;
}

static uint16_t lfn_checksum(const uint8_t *short_name) {
    uint16_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint16_t)(((sum & 1u) ? 0x8000u : 0u) + (sum >> 1) + short_name[i]);
    return sum;
}

/* 13 UTF-16LE slots per record: five at 1, six at 14, two at 28. Past the
 * name comes a NUL, then 0xFFFF to the end of the record. */
static void lfn_put_chars(uint8_t *e, const char *name, int len, int first) {
    static const int slot_off[3] = {1, 14, 28};
    static const int slot_len[3] = {5, 6, 2};
    int slot = 0, n = 0;

    for (int i = 0; i < 13; i++) {
        int ci = first + i;
        uint16_t u;
        uint8_t *p;
        if (ci < len) u = (uint16_t)(unsigned char)name[ci];
        else if (ci == len) u = 0x0000;
        else u = 0xFFFF;
        if (n == slot_len[slot]) { slot++; n = 0; }
        p = e + slot_off[slot] + n * 2;
        p[0] = (uint8_t)(u & 0xFF);
        p[1] = (uint8_t)(u >> 8);
        n++;
    }
}

static int char_ok(unsigned char c) {
    if (c < 0x20 || c > 0x7E) return 0;
    if (strchr("\"*/\\[]:;|=,+?<>", (int)c)) return 0;
    return 1;
}

static int name_ok(const char *name) {
    size_t n;
    if (!name || !name[0]) return 0;
    n = strlen(name);
    if (n > 20) return 0;                    /* this volume's names are <= 9 */
    if (name[0] == ' ' || name[n - 1] == ' ') return 0;
    for (size_t i = 0; i < n; i++)
        if (!char_ok((unsigned char)name[i])) return 0;
    return 1;
}

static char upper_of(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* Splits NAME into the base and the extension an 8.3 entry would carry. */
static void split_name(const char *name, char *base, int *base_len,
                       char *ext, int *ext_len) {
    const char *dot = strrchr(name, '.');
    size_t n = dot ? (size_t)(dot - name) : strlen(name);

    for (size_t i = 0; i < n; i++) base[i] = upper_of(name[i]);
    *base_len = (int)n;
    if (!dot) { *ext_len = 0; return; }
    n = strlen(dot + 1);
    if (n > 3) n = 3;
    for (size_t i = 0; i < n; i++) ext[i] = upper_of(dot[1 + i]);
    *ext_len = (int)n;
}

static int short_taken(struct fat16_volume *vol, const uint8_t *short_name) {
    for (uint32_t i = 0; i < FAT16_ROOT_ENTRIES; i++) {
        const uint8_t *e = root_entry(vol, i);
        if (e[0] == 0x00) break;
        if (e[0] == 0xE5) continue;
        if (e[11] == FAT16_ATTR_LFN || e[11] == 0x00 || e[11] == FAT16_ATTR_VOLUME)
            continue;
        if (memcmp(e, short_name, 11) == 0) return 1;
    }
    return 0;
}

/* Fills OUT with the padded 11-byte short name: the stem itself when it fits
 * eight characters and is free, otherwise six characters plus ~n. Returns 0
 * when no short name is left, which means the root directory is full. */
static int make_short_name(struct fat16_volume *vol, const char *base,
                           int base_len, const char *ext, int ext_len,
                           uint8_t *out) {
    int take = base_len <= 8 ? base_len : 6;

    for (int i = 0; i < 11; i++) out[i] = ' ';
    memcpy(out, base, (size_t)take);
    for (int i = 0; i < ext_len; i++) out[8 + i] = (uint8_t)ext[i];
    if (base_len <= 8 && !short_taken(vol, out)) return 1;

    /* The stem does not fit, or the plain stem is already taken: BASE~n,
     * with at most six characters left for the stem. */
    take = base_len < 6 ? base_len : 6;
    for (uint32_t n = 1; n <= 999999u; n++) {
        uint32_t v = n;
        int d = 0;
        for (int i = 0; i < 11; i++) out[i] = ' ';
        memcpy(out, base, (size_t)take);
        while (v) { v /= 10; d++; }
        if (take + 1 + d > 8) return 0;
        v = n;
        out[take] = '~';
        for (int i = d; i > 0; i--) { out[take + i] = (char)('0' + v % 10); v /= 10; }
        for (int i = 0; i < ext_len; i++) out[8 + i] = (uint8_t)ext[i];
        if (!short_taken(vol, out)) return 1;
    }
    return 0;
}

static void put_entry(struct fat16_volume *vol, const uint8_t *name11,
                      uint8_t attr, uint32_t cluster, uint32_t size) {
    uint8_t *e = root_entry(vol, vol->root_entries_used++);
    memcpy(e, name11, 11);
    e[11] = attr;
    e[13] = 0;                              /* creation time tenths */
    e[14] = (uint8_t)(DOS_TIME & 0xFF); e[15] = (uint8_t)(DOS_TIME >> 8);
    e[16] = (uint8_t)(DOS_DATE & 0xFF); e[17] = (uint8_t)(DOS_DATE >> 8);
    e[18] = (uint8_t)(DOS_DATE & 0xFF); e[19] = (uint8_t)(DOS_DATE >> 8);
    e[20] = 0; e[21] = 0;                   /* no high cluster in FAT16 */
    e[22] = (uint8_t)(DOS_TIME & 0xFF); e[23] = (uint8_t)(DOS_TIME >> 8);
    e[24] = (uint8_t)(DOS_DATE & 0xFF); e[25] = (uint8_t)(DOS_DATE >> 8);
    e[26] = (uint8_t)(cluster & 0xFF); e[27] = (uint8_t)(cluster >> 8);
    e[28] = (uint8_t)(size & 0xFF);
    e[29] = (uint8_t)((size >> 8) & 0xFF);
    e[30] = (uint8_t)((size >> 16) & 0xFF);
    e[31] = (uint8_t)((size >> 24) & 0xFF);
}

int fat16_init(struct fat16_volume *vol, uint8_t *data, size_t size) {
    uint8_t label11[11];
    uint8_t *bs;
    uint32_t total_sectors;

    if (!vol || !data || size < FAT16_VOLUME_SIZE) return -1;
    memset(vol, 0, sizeof(*vol));
    vol->data = data;
    vol->size = size;

    /* Geometry: 32 MiB FAT16, 4 KiB clusters, 63 sectors/track, 255 heads */
    vol->bytes_per_sector = FAT16_SECTOR_SIZE;
    vol->sectors_per_cluster = 8;  /* 4 KiB cluster */
    vol->reserved_sectors = 32;
    vol->num_fats = 2;
    vol->fat_size = 256;  /* sectors per FAT */
    vol->root_dir_sectors = FAT16_ROOT_ENTRIES * FAT16_ENTRY_SIZE / FAT16_SECTOR_SIZE;
    vol->fat_start = vol->reserved_sectors;
    vol->root_dir_start = vol->fat_start + vol->num_fats * vol->fat_size;
    vol->data_start = vol->root_dir_start + vol->root_dir_sectors;
    vol->total_clusters = (size / vol->bytes_per_sector - vol->data_start) /
                          vol->sectors_per_cluster;
    vol->next_cluster = 2;                  /* clusters 0 and 1 are the FAT's own */
    vol->root_entries_used = 0;

    /* Build boot sector (BPB) */
    bs = vol->data;
    memset(bs, 0, vol->bytes_per_sector);
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;         /* jump boot */
    memcpy(bs + 3, "QEAFOS  ", 8);                     /* OEM name */
    *(uint16_t *)(bs + 11) = FAT16_SECTOR_SIZE;        /* bytes per sector */
    bs[13] = vol->sectors_per_cluster;
    *(uint16_t *)(bs + 14) = vol->reserved_sectors;
    bs[16] = vol->num_fats;
    *(uint16_t *)(bs + 17) = FAT16_ROOT_ENTRIES;
    *(uint16_t *)(bs + 19) = 0;                        /* use the 32-bit count */
    bs[21] = 0xF8;                                     /* media descriptor */
    *(uint16_t *)(bs + 22) = vol->fat_size;
    *(uint16_t *)(bs + 24) = 63;                       /* sectors per track */
    *(uint16_t *)(bs + 26) = 255;                      /* heads */
    *(uint32_t *)(bs + 28) = 0;                        /* hidden sectors */
    total_sectors = (uint32_t)(size / FAT16_SECTOR_SIZE);
    *(uint32_t *)(bs + 32) = total_sectors;
    bs[36] = 0x80;                                     /* physical drive number */
    bs[38] = 0x29;                                     /* extended boot signature */
    *(uint32_t *)(bs + 39) = 0x12345678;               /* volume serial */
    memset(label11, ' ', 11);
    memcpy(label11, FAT16_LABEL, strlen(FAT16_LABEL));
    memcpy(bs + 43, label11, 11);
    memcpy(bs + 54, "FAT16   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;

    /* Clear FATs and root directory */
    memset(vol->data + (size_t)vol->fat_start * vol->bytes_per_sector, 0,
           (size_t)vol->num_fats * vol->fat_size * vol->bytes_per_sector);
    memset(vol->data + (size_t)vol->root_dir_start * vol->bytes_per_sector, 0,
           (size_t)vol->root_dir_sectors * vol->bytes_per_sector);

    /* FAT[0] carries the media descriptor in its high byte and FAT[1] is an
     * end-of-chain marker. They are 16-bit entries, in every FAT copy - the
     * old builder wrote three loose bytes, which left FAT[1] a reserved
     * value a host is free to read as a corrupt disk. */
    fat_set(vol, 0, 0xFFF8);
    fat_set(vol, 1, 0xFFFF);

    /* The label is a root entry too, not only a BPB field: the reader on the
     * phone walks directory entries and would call an unlabelled disk lost. */
    put_entry(vol, label11, FAT16_ATTR_VOLUME, 0, 0);

    umos_logf("fat16: volume %u bytes, %u clusters, %u root entries\n",
              (unsigned)size, (unsigned)vol->total_clusters,
              (unsigned)FAT16_ROOT_ENTRIES);
    return 0;
}

int fat16_mkdir(struct fat16_volume *vol, const char *name) {
    char base[21], ext[4];
    uint8_t short_name[11];
    uint8_t *dir;
    int base_len, ext_len, len, records;
    uint32_t cluster;
    uint16_t sum;

    if (!vol || !name_ok(name)) return -1;
    if (vol->next_cluster - 2 >= vol->total_clusters) return -1;
    len = (int)strlen(name);
    /* One long-name record per 13 characters, always: the short entry cannot
     * carry the case the storage table asks for ("Music", not "MUSIC"). */
    records = (len + 1 + 12) / 13;
    if (vol->root_entries_used + (uint32_t)records + 1 > FAT16_ROOT_ENTRIES)
        return -1;

    split_name(name, base, &base_len, ext, &ext_len);
    if (!make_short_name(vol, base, base_len, ext, ext_len, short_name))
        return -1;

    cluster = vol->next_cluster++;
    fat_set(vol, cluster, FAT16_EOF);

    /* The new directory holds "." and ".." and nothing else yet. Its cluster
     * is wiped first: the RAM behind the LUN is reused between boots. */
    dir = vol->data + (size_t)cluster_sector(vol, cluster) * vol->bytes_per_sector;
    memset(dir, 0, (size_t)vol->sectors_per_cluster * vol->bytes_per_sector);
    for (int i = 0; i < 2; i++) {
        uint8_t *e = dir + i * FAT16_ENTRY_SIZE;
        uint32_t c = i ? 0 : cluster;       /* ".." points at the root: no cluster */
        memset(e, ' ', 11);
        e[0] = '.';
        if (i) e[1] = '.';
        e[11] = FAT16_ATTR_DIR;
        e[16] = (uint8_t)(DOS_DATE & 0xFF); e[17] = (uint8_t)(DOS_DATE >> 8);
        e[24] = (uint8_t)(DOS_DATE & 0xFF); e[25] = (uint8_t)(DOS_DATE >> 8);
        e[26] = (uint8_t)(c & 0xFF); e[27] = (uint8_t)(c >> 8);
    }

    sum = lfn_checksum(short_name);
    for (int r = records; r >= 1; r--) {
        uint8_t *e = root_entry(vol, vol->root_entries_used++);
        memset(e, 0, FAT16_ENTRY_SIZE);
        e[0] = (uint8_t)((r == records ? FAT16_LFN_LAST : 0) | r);
        e[11] = FAT16_ATTR_LFN;
        e[13] = (uint8_t)(sum & 0xFF);
        lfn_put_chars(e, name, len, (r - 1) * 13);
    }
    put_entry(vol, short_name, FAT16_ATTR_DIR, cluster, 0);

    umos_logf("fat16: mkdir %s -> cluster %u, short %c%c%c%c%c%c%c%c%c%c%c, "
              "%d long record(s)\n", name, (unsigned)cluster,
              short_name[0], short_name[1], short_name[2], short_name[3],
              short_name[4], short_name[5], short_name[6], short_name[7],
              short_name[8], short_name[9], short_name[10], records);
    return (int)cluster;
}

int fat16_mkdir_in(struct fat16_volume *vol, uint32_t parent_cluster, const char *name)
{
    char base[21], ext[4];
    uint8_t short_name[11];
    uint8_t *dir;
    uint8_t *pdir;
    int base_len, ext_len, len, records;
    uint32_t cluster;
    uint16_t sum;
    int slot;

    if (!vol || !name_ok(name) || parent_cluster < 2) return -1;
    if (vol->next_cluster - 2 >= vol->total_clusters) return -1;
    len = (int)strlen(name);
    records = (len + 1 + 12) / 13;

    split_name(name, base, &base_len, ext, &ext_len);
    if (!make_short_name(vol, base, base_len, ext, ext_len, short_name))
        return -1;

    cluster = vol->next_cluster++;
    fat_set(vol, cluster, FAT16_EOF);

    dir = vol->data + (size_t)cluster_sector(vol, cluster) * vol->bytes_per_sector;
    memset(dir, 0, (size_t)vol->sectors_per_cluster * vol->bytes_per_sector);
    for (int i = 0; i < 2; i++) {
        uint8_t *e = dir + i * FAT16_ENTRY_SIZE;
        uint32_t c = i ? parent_cluster : cluster;
        memset(e, ' ', 11);
        e[0] = '.';
        if (i) e[1] = '.';
        e[11] = FAT16_ATTR_DIR;
        e[16] = (uint8_t)(DOS_DATE & 0xFF); e[17] = (uint8_t)(DOS_DATE >> 8);
        e[24] = (uint8_t)(DOS_DATE & 0xFF); e[25] = (uint8_t)(DOS_DATE >> 8);
        e[26] = (uint8_t)(c & 0xFF); e[27] = (uint8_t)(c >> 8);
    }

    pdir = vol->data + (size_t)cluster_sector(vol, parent_cluster) * vol->bytes_per_sector;
    slot = 2;
    while (slot * FAT16_ENTRY_SIZE < (int)(vol->sectors_per_cluster * vol->bytes_per_sector) &&
           pdir[slot * FAT16_ENTRY_SIZE] != 0x00 &&
           pdir[slot * FAT16_ENTRY_SIZE] != 0xE5)
        slot++;
    sum = lfn_checksum(short_name);
    for (int r = records; r >= 1; r--) {
        uint8_t *e = pdir + slot * FAT16_ENTRY_SIZE;
        memset(e, 0, FAT16_ENTRY_SIZE);
        e[0] = (uint8_t)((r == records ? FAT16_LFN_LAST : 0) | r);
        e[11] = FAT16_ATTR_LFN;
        e[13] = (uint8_t)(sum & 0xFF);
        lfn_put_chars(e, name, len, (r - 1) * 13);
        slot++;
    }
    {
        uint8_t *e = pdir + slot * FAT16_ENTRY_SIZE;
        memset(e, 0, FAT16_ENTRY_SIZE);
        memcpy(e, short_name, 11);
        e[11] = FAT16_ATTR_DIR;
        e[16] = (uint8_t)(DOS_DATE & 0xFF); e[17] = (uint8_t)(DOS_DATE >> 8);
        e[24] = (uint8_t)(DOS_DATE & 0xFF); e[25] = (uint8_t)(DOS_DATE >> 8);
        e[26] = (uint8_t)(cluster & 0xFF); e[27] = (uint8_t)(cluster >> 8);
    }
    umos_logf("fat16: mkdir_in %s in %u -> cluster %u\n", name, (unsigned)parent_cluster, (unsigned)cluster);
    return (int)cluster;
}

/* ---- small root files (sync protocol) ------------------------------------- */

/* Windows writes an 8.3 alias for .qeapp names ("HELLO~1.QEA"), but the UI
 * lists the long name. Match the long entry too so a file copied from a PC can
 * be read back by the name the file manager showed. ASCII names only here;
 * the 8.3 path stays authoritative for the phone's own files. */
static uint8_t *find_long_entry(struct fat16_volume *vol, const char *name) {
    uint8_t *base = vol->data +
        (size_t)vol->root_dir_start * vol->bytes_per_sector;
    uint16_t lfn[20 * 13];
    int last = 0, have = 0;
    memset(lfn, 0, sizeof(lfn));
    for (uint32_t i = 0; i < FAT16_ROOT_ENTRIES; i++) {
        uint8_t *entry = base + (size_t)i * FAT16_ENTRY_SIZE;
        uint8_t attr = entry[11];
        if (entry[0] == 0x00) break;
        if (entry[0] == 0xE5) { have = 0; last = 0; continue; }
        if (attr == FAT16_ATTR_LFN) {
            static const int offsets[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
            int sequence = entry[0] & 0x1F;
            if (sequence >= 1 && sequence <= 20) {
                int slot = (sequence - 1) * 13;
                for (int c = 0; c < 13; c++)
                    lfn[slot + c] = (uint16_t)(entry[offsets[c]] |
                                               (entry[offsets[c] + 1] << 8));
                if (sequence > last) last = sequence;
                have = 1;
            }
            continue;
        }
        if (attr & FAT16_ATTR_VOLUME) { have = 0; last = 0; continue; }
        if (have) {
            char decoded[64];
            size_t used = 0;
            for (int c = 0; c < last * 13 && used + 1 < sizeof(decoded); c++) {
                uint16_t unit = lfn[c];
                if (unit == 0 || unit == 0xFFFF) break;
                decoded[used++] = unit < 0x80 ? (char)unit : '?';
            }
            decoded[used] = '\0';
            if (strcmp(decoded, name) == 0) return entry;
        }
        have = 0;
        last = 0;
    }
    return NULL;
}

static uint8_t *find_short_entry(struct fat16_volume *vol,
                                 const uint8_t *short_name) {
    /* Scan the whole root region, not just the entries this builder wrote: a
     * host (Windows) appends QEAF.CMD past root_entries_used, and that is the
     * entry the rescue loop must read. Stop at the 0x00 end marker, skip 0xE5
     * deleted slots; root_entries_used stays the builder's own allocation
     * high-water mark and is never trusted as the host-visible boundary. */
    for (uint32_t i = 0; i < FAT16_ROOT_ENTRIES; i++) {
        uint8_t *e = root_entry(vol, i);
        if (e[0] == 0x00) break;
        if (e[0] == 0xE5) continue;
        if (e[11] == FAT16_ATTR_LFN || e[11] == FAT16_ATTR_VOLUME) continue;
        if (e[11] & FAT16_ATTR_DIR) continue;
        if (memcmp(e, short_name, 11) == 0) return e;
    }
    return NULL;
}

static uint32_t file_capacity(struct fat16_volume *vol, uint32_t first) {
    uint32_t n = 0, cur = first;
    uint16_t fat_val;
    while (cur >= 2 && cur < FAT16_EOF - 8 && n < 64) {
        uint8_t *f = vol->data + (size_t)vol->fat_start * vol->bytes_per_sector;
        memcpy(&fat_val, f + cur * 2, 2);
        n++;
        cur = fat_val;
    }
    return n * vol->sectors_per_cluster * vol->bytes_per_sector;
}

int fat16_write_file(struct fat16_volume *vol, const char *name,
                     const uint8_t *data, size_t len) {
    char base[21], ext[4];
    uint8_t short_name[11];
    uint8_t exact[11];
    int base_len, ext_len, name_len, records;
    uint32_t cluster_bytes, need, first, prev, cur, written;
    uint8_t *entry;
    uint16_t sum;

    if (!vol || !name_ok(name) || (!data && len)) return -1;
    cluster_bytes = (uint32_t)vol->sectors_per_cluster * vol->bytes_per_sector;
    need = (uint32_t)((len + cluster_bytes - 1) / cluster_bytes);
    if (need == 0) need = 1;
    if (need > 64 || len > 64 * cluster_bytes) return -1;

    split_name(name, base, &base_len, ext, &ext_len);
    /* The exact 8.3 spelling is the identity of this file. Calling
     * make_short_name() on a rewrite would see the old entry as taken and
     * hand back DEVINF~1.TXT, creating a second file instead of replacing. */
    for (int i = 0; i < 11; i++) exact[i] = ' ';
    for (int i = 0; i < base_len && i < 8; i++) exact[i] = (uint8_t)base[i];
    for (int i = 0; i < ext_len && i < 3; i++) exact[8 + i] = (uint8_t)ext[i];
    memcpy(short_name, exact, 11);

    entry = find_short_entry(vol, exact);
    if (entry) {
        /* Replace in place. Refuse a rewrite that would need more clusters
         * than the first publication reserved: growing a file in a FAT the
         * host may be reading right now is not safe. */
        first = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8);
        if (!first || file_capacity(vol, first) < len) return -1;
        cur = first;
        written = 0;
        while (written < len) {
            uint8_t *dst = vol->data +
                (size_t)cluster_sector(vol, cur) * vol->bytes_per_sector;
            uint32_t chunk = (uint32_t)len - written;
            if (chunk > cluster_bytes) chunk = cluster_bytes;
            memset(dst, 0, cluster_bytes);
            memcpy(dst, data + written, chunk);
            written += chunk;
            {
                uint8_t *f = vol->data +
                    (size_t)vol->fat_start * vol->bytes_per_sector;
                uint16_t next;
                memcpy(&next, f + cur * 2, 2);
                cur = next;
            }
        }
        entry[28] = (uint8_t)(len & 0xFF);
        entry[29] = (uint8_t)((len >> 8) & 0xFF);
        entry[30] = (uint8_t)((len >> 16) & 0xFF);
        entry[31] = (uint8_t)((len >> 24) & 0xFF);
        umos_logf("fat16: rewrote %s (%u bytes)\n", name, (unsigned)len);
        return 0;
    }

    name_len = (int)strlen(name);
    records = (name_len + 1 + 12) / 13;
    if (vol->root_entries_used + (uint32_t)records + 1 > FAT16_ROOT_ENTRIES)
        return -1;
    if (vol->next_cluster + need - 2 > vol->total_clusters) return -1;

    first = vol->next_cluster;
    prev = 0;
    written = 0;
    for (uint32_t i = 0; i < need; i++) {
        cur = vol->next_cluster++;
        if (prev) fat_set(vol, prev, (uint16_t)cur);
        prev = cur;
        {
            uint8_t *dst = vol->data +
                (size_t)cluster_sector(vol, cur) * vol->bytes_per_sector;
            uint32_t chunk = (uint32_t)len - written;
            memset(dst, 0, cluster_bytes);
            if (chunk > cluster_bytes) chunk = cluster_bytes;
            if (chunk) memcpy(dst, data + written, chunk);
            written += chunk;
        }
    }
    fat_set(vol, prev, FAT16_EOF);

    sum = lfn_checksum(short_name);
    for (int r = records; r >= 1; r--) {
        uint8_t *e = root_entry(vol, vol->root_entries_used++);
        memset(e, 0, FAT16_ENTRY_SIZE);
        e[0] = (uint8_t)((r == records ? FAT16_LFN_LAST : 0) | r);
        e[11] = FAT16_ATTR_LFN;
        e[13] = (uint8_t)(sum & 0xFF);
        lfn_put_chars(e, name, name_len, (r - 1) * 13);
    }
    put_entry(vol, short_name, FAT16_ATTR_ARCHIVE, first, (uint32_t)len);
    umos_logf("fat16: wrote %s (%u bytes, %u cluster(s))\n",
              name, (unsigned)len, (unsigned)need);
    return 0;
}

static int path_cluster(struct fat16_volume *vol, const char *path);

int fat16_read_file(struct fat16_volume *vol, const char *name,
                    uint8_t *out, size_t cap) {
    char base[21], ext[4];
    uint8_t exact[11];
    int base_len, ext_len;
    uint8_t *entry;
    uint32_t size, first, cluster_bytes, done, cur;
    uint16_t next;

    if (!vol || !name_ok(name) || !out) return -1;
    split_name(name, base, &base_len, ext, &ext_len);
    for (int i = 0; i < 11; i++) exact[i] = ' ';
    for (int i = 0; i < base_len && i < 8; i++) exact[i] = (uint8_t)base[i];
    for (int i = 0; i < ext_len && i < 3; i++) exact[8 + i] = (uint8_t)ext[i];
    entry = find_short_entry(vol, exact);
    if (!entry) entry = find_long_entry(vol, name);
    if (!entry) return -1;
    size = (uint32_t)entry[28] | ((uint32_t)entry[29] << 8) |
           ((uint32_t)entry[30] << 16) | ((uint32_t)entry[31] << 24);
    if (size > cap) return -1;
    first = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8);
    cluster_bytes = (uint32_t)vol->sectors_per_cluster * vol->bytes_per_sector;
    cur = first;
    done = 0;
    while (done < size && cur >= 2 && cur < FAT16_EOF - 8) {
        uint8_t *src = vol->data +
            (size_t)cluster_sector(vol, cur) * vol->bytes_per_sector;
        uint32_t chunk = size - done;
        if (chunk > cluster_bytes) chunk = cluster_bytes;
        memcpy(out + done, src, chunk);
        done += chunk;
        memcpy(&next,
               vol->data + (size_t)vol->fat_start * vol->bytes_per_sector +
               cur * 2, 2);
        cur = next;
    }
    return done == size ? (int)size : -1;
}

static uint8_t *find_entry_in(struct fat16_volume *vol, uint32_t cluster,
                              const char *name);

int fat16_file_size(struct fat16_volume *vol, const char *name) {
    char base[21], ext[4];
    uint8_t exact[11];
    int base_len, ext_len;
    uint8_t *entry;
    const char *slash;
    const char *leaf;
    uint32_t cluster = 0;

    if (!vol || !name) return -1;
    while (*name == '/') name++;
    /* A picker path can point inside a folder (Install/Apps/x.qeapp). */
    slash = strrchr(name, '/');
    leaf = slash ? slash + 1 : name;
    if (slash) {
        char parent[256];
        size_t n = (size_t)(slash - name);
        if (n >= sizeof(parent)) return -1;
        memcpy(parent, name, n);
        parent[n] = '\0';
        cluster = (uint32_t)path_cluster(vol, parent);
        if ((int32_t)cluster < 0) return -1;
    }
    if (!name_ok(leaf)) return -1;
    if (slash) {
        entry = find_entry_in(vol, cluster, leaf);
    }
    else {
        split_name(leaf, base, &base_len, ext, &ext_len);
        for (int i = 0; i < 11; i++) exact[i] = ' ';
        for (int i = 0; i < base_len && i < 8; i++) exact[i] = (uint8_t)base[i];
        for (int i = 0; i < ext_len && i < 3; i++) exact[8 + i] = (uint8_t)ext[i];
        entry = find_short_entry(vol, exact);
        if (!entry) entry = find_long_entry(vol, leaf);
    }
    if (!entry) return -1;
    return (int)((uint32_t)entry[28] | ((uint32_t)entry[29] << 8) |
                 ((uint32_t)entry[30] << 16) | ((uint32_t)entry[31] << 24));
}

int fat16_file_open(struct fat16_volume *vol, const char *name,
                    struct fat16_file *out) {
    char base[21], ext[4];
    uint8_t exact[11];
    int base_len, ext_len;
    uint8_t *entry;
    uint32_t bytes, first;

    if (!vol || !name_ok(name) || !out) return -1;
    split_name(name, base, &base_len, ext, &ext_len);
    for (int i = 0; i < 11; i++) exact[i] = ' ';
    for (int i = 0; i < base_len && i < 8; i++) exact[i] = (uint8_t)base[i];
    for (int i = 0; i < ext_len && i < 3; i++) exact[8 + i] = (uint8_t)ext[i];
    entry = find_short_entry(vol, exact);
    if (!entry) entry = find_long_entry(vol, name);
    if (!entry) return -1;
    bytes = (uint32_t)entry[28] | ((uint32_t)entry[29] << 8) |
            ((uint32_t)entry[30] << 16) | ((uint32_t)entry[31] << 24);
    first = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8);
    if (!bytes || first < 2 || first >= FAT16_EOF - 8) return -1;
    out->size = bytes;
    out->offset = 0;
    out->cluster = first;
    return 0;
}

int fat16_file_next(struct fat16_volume *vol, struct fat16_file *file,
                    const uint8_t **chunk) {
    uint32_t cluster_bytes, remaining, span, cur, first, here;
    uint8_t *fat;
    uint16_t next;

    if (!vol || !file || !chunk) return -1;
    if (file->offset >= file->size) return 0;
    cur = file->cluster;
    if (cur < 2 || cur >= FAT16_EOF - 8) return -1;
    cluster_bytes = (uint32_t)vol->sectors_per_cluster * vol->bytes_per_sector;
    fat = vol->data + (size_t)vol->fat_start * vol->bytes_per_sector;
    remaining = file->size - file->offset;
    first = cur;
    span = 0;
    /* Walk on while the next cluster sits right behind this one, so a
     * contiguous file still arrives as one chunk and a scattered one as
     * exactly as many as it really has. */
    for (;;) {
        memcpy(&next, fat + cur * 2, 2);
        here = cluster_bytes;
        if (here > remaining - span) here = remaining - span;
        span += here;
        file->cluster = next;
        if (here < cluster_bytes || span >= remaining) break;  /* short tail */
        if (next < 2 || next >= FAT16_EOF - 8 || next != cur + 1) break;
        cur = next;
    }
    file->offset += span;
    *chunk = vol->data +
             (size_t)cluster_sector(vol, first) * vol->bytes_per_sector;
    return (int)span;
}

int fat16_read_sector(struct fat16_volume *vol, uint32_t lba, uint8_t *buf) {
    if (!vol || !buf || lba * vol->bytes_per_sector >= vol->size) return -1;
    memcpy(buf, vol->data + lba * vol->bytes_per_sector, vol->bytes_per_sector);
    return 0;
}

int fat16_write_sector(struct fat16_volume *vol, uint32_t lba, const uint8_t *buf) {
    if (!vol || !buf || lba * vol->bytes_per_sector >= vol->size) return -1;
    memcpy(vol->data + lba * vol->bytes_per_sector, buf, vol->bytes_per_sector);
    return 0;
}

uint32_t fat16_total_sectors(struct fat16_volume *vol) {
    return vol ? (uint32_t)(vol->size / vol->bytes_per_sector) : 0;
}

/* ---- root directory listing ---------------------------------------------- */

/* Append one UTF-16 code unit as UTF-8 (BMP only, which is all FAT16 LFN
 * stores). Surrogates and NUL terminate the name. */
static int put_utf8(char *out, size_t cap, size_t *used, uint16_t unit)
{
    uint32_t cp = unit;
    size_t need;
    char bytes[3];

    if(cp == 0 || cp == 0xFFFF) return -1;
    if(cp < 0x80) {
        bytes[0] = (char)cp;
        need = 1;
    }
    else if(cp < 0x800) {
        bytes[0] = (char)(0xC0 | (cp >> 6));
        bytes[1] = (char)(0x80 | (cp & 0x3F));
        need = 2;
    }
    else {
        bytes[0] = (char)(0xE0 | (cp >> 12));
        bytes[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (cp & 0x3F));
        need = 3;
    }
    if(*used + need >= cap) return -1;
    memcpy(out + *used, bytes, need);
    *used += need;
    out[*used] = '\0';
    return 0;
}

int fat16_list_root(struct fat16_volume *vol, fat16_entry_t *out, int max)
{
    uint8_t *base;
    uint16_t lfn[20 * 13];
    int lfn_last = 0;
    int have_lfn = 0;
    int count = 0;

    if(vol == NULL || out == NULL || max <= 0) return -1;
    base = vol->data + (size_t)vol->root_dir_start * vol->bytes_per_sector;
    memset(lfn, 0, sizeof(lfn));

    for(uint32_t index = 0; index < FAT16_ROOT_ENTRIES; index++) {
        const uint8_t *entry = base + (size_t)index * FAT16_ENTRY_SIZE;
        uint8_t attr = entry[11];

        if(entry[0] == 0x00) break;
        if(entry[0] == 0xE5) {
            have_lfn = 0;
            lfn_last = 0;
            continue;
        }
        if(attr == FAT16_ATTR_LFN) {
            int sequence = entry[0] & 0x1F;
            static const int offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22,
                                            24, 28, 30};
            if(sequence >= 1 && sequence <= 20) {
                int slot = (sequence - 1) * 13;
                for(int c = 0; c < 13; c++)
                    lfn[slot + c] = (uint16_t)(entry[offsets[c]] |
                                               (entry[offsets[c] + 1] << 8));
                if(sequence > lfn_last) lfn_last = sequence;
                have_lfn = 1;
            }
            continue;
        }
        if(attr & FAT16_ATTR_VOLUME) {
            have_lfn = 0;
            lfn_last = 0;
            continue;
        }

        if(count < max) {
            fat16_entry_t *dst = &out[count];
            size_t used = 0;
            dst->name[0] = '\0';
            dst->size = (uint32_t)entry[28] | ((uint32_t)entry[29] << 8) |
                        ((uint32_t)entry[30] << 16) |
                        ((uint32_t)entry[31] << 24);
            dst->is_dir = (attr & FAT16_ATTR_DIR) ? 1 : 0;

            if(have_lfn) {
                for(int c = 0; c < lfn_last * 13; c++)
                    if(put_utf8(dst->name, sizeof(dst->name), &used, lfn[c]) < 0)
                        break;
            }
            if(dst->name[0] == '\0') {
                /* Plain 8.3, lower-case base, trimmed spaces. */
                char base8[9];
                char ext3[4];
                int n = 0;
                memcpy(base8, entry, 8);
                base8[8] = '\0';
                memcpy(ext3, entry + 8, 3);
                ext3[3] = '\0';
                for(int i = 7; i >= 0 && base8[i] == ' '; i--) base8[i] = '\0';
                for(int i = 2; i >= 0 && ext3[i] == ' '; i--) ext3[i] = '\0';
                if(entry[12] & 0x08) {
                    for(int i = 0; base8[i]; i++)
                        if(base8[i] >= 'A' && base8[i] <= 'Z')
                            base8[i] = (char)(base8[i] + 32);
                }
                else {
                    for(int i = 0; base8[i]; i++)
                        if(base8[i] >= 'a' && base8[i] <= 'z')
                            base8[i] = (char)(base8[i] - 32);
                }
                for(int i = 0; base8[i] && n < (int)sizeof(dst->name) - 1; i++)
                    dst->name[n++] = base8[i];
                if(ext3[0] && n < (int)sizeof(dst->name) - 2) {
                    dst->name[n++] = '.';
                    for(int i = 0; ext3[i] && n < (int)sizeof(dst->name) - 1; i++)
                        dst->name[n++] = ext3[i];
                }
                dst->name[n] = '\0';
            }
            if(dst->name[0] != '\0') count++;
        }
        have_lfn = 0;
        lfn_last = 0;
    }
    return count;
}

/* ---- subdirectory listing / read (cluster-aware) -------------------------- */

static uint16_t fat_next(struct fat16_volume *vol, uint32_t cluster) {
    uint16_t v;
    memcpy(&v, vol->data + (size_t)vol->fat_start * vol->bytes_per_sector +
           cluster * 2, 2);
    return v;
}

/* Walk every live entry of a directory: the fixed root region for cluster 0,
 * otherwise the cluster chain. Stops at the 0x00 end marker. The visitor
 * returns non-zero to stop early. */
typedef int (*dir_visit_fn)(uint8_t *entry, void *ud);

static int dir_foreach(struct fat16_volume *vol, uint32_t cluster,
                       dir_visit_fn fn, void *ud) {
    uint32_t slots = (uint32_t)vol->sectors_per_cluster *
                     vol->bytes_per_sector / FAT16_ENTRY_SIZE;
    if (cluster == 0) {
        for (uint32_t i = 0; i < FAT16_ROOT_ENTRIES; i++) {
            uint8_t *e = root_entry(vol, i);
            if (e[0] == 0x00) break;
            if (fn(e, ud)) return 1;
        }
        return 0;
    }
    uint32_t cur = cluster;
    while (cur >= 2 && cur < FAT16_EOF - 8) {
        uint8_t *base = vol->data +
            (size_t)cluster_sector(vol, cur) * vol->bytes_per_sector;
        for (uint32_t s = 0; s < slots; s++) {
            uint8_t *e = base + (size_t)s * FAT16_ENTRY_SIZE;
            if (e[0] == 0x00) return 0;
            if (fn(e, ud)) return 1;
        }
        cur = fat_next(vol, cur);
    }
    return 0;
}

struct dir_list_ud {
    uint16_t lfn[20 * 13];
    int last, have, skip_dots;
    fat16_entry_t *out;
    int max, count;
};

static int dir_list_visit(uint8_t *entry, void *ud) {
    struct dir_list_ud *s = (struct dir_list_ud *)ud;
    uint8_t attr = entry[11];
    if (entry[0] == 0xE5) { s->have = 0; s->last = 0; return 0; }
    if (attr == FAT16_ATTR_LFN) {
        static const int offsets[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
        int sequence = entry[0] & 0x1F;
        if (sequence >= 1 && sequence <= 20) {
            int slot = (sequence - 1) * 13;
            for (int c = 0; c < 13; c++)
                s->lfn[slot + c] = (uint16_t)(entry[offsets[c]] |
                                              (entry[offsets[c] + 1] << 8));
            if (sequence > s->last) s->last = sequence;
            s->have = 1;
        }
        return 0;
    }
    if (attr & FAT16_ATTR_VOLUME) { s->have = 0; s->last = 0; return 0; }
    if (s->skip_dots && entry[0] == '.') { s->have = 0; s->last = 0; return 0; }

    if (s->count < s->max) {
        fat16_entry_t *dst = &s->out[s->count];
        size_t used = 0;
        dst->name[0] = '\0';
        dst->size = (uint32_t)entry[28] | ((uint32_t)entry[29] << 8) |
                    ((uint32_t)entry[30] << 16) | ((uint32_t)entry[31] << 24);
        dst->is_dir = (attr & FAT16_ATTR_DIR) ? 1 : 0;
        if (s->have) {
            for (int c = 0; c < s->last * 13; c++)
                if (put_utf8(dst->name, sizeof(dst->name), &used, s->lfn[c]) < 0)
                    break;
        }
        if (dst->name[0] == '\0') {
            char base8[9], ext3[4];
            int n = 0;
            memcpy(base8, entry, 8); base8[8] = '\0';
            memcpy(ext3, entry + 8, 3); ext3[3] = '\0';
            for (int i = 7; i >= 0 && base8[i] == ' '; i--) base8[i] = '\0';
            for (int i = 2; i >= 0 && ext3[i] == ' '; i--) ext3[i] = '\0';
            if (entry[12] & 0x08) {
                for (int i = 0; base8[i]; i++)
                    if (base8[i] >= 'A' && base8[i] <= 'Z')
                        base8[i] = (char)(base8[i] + 32);
            } else {
                for (int i = 0; base8[i]; i++)
                    if (base8[i] >= 'a' && base8[i] <= 'z')
                        base8[i] = (char)(base8[i] - 32);
            }
            for (int i = 0; base8[i] && n < (int)sizeof(dst->name) - 1; i++)
                dst->name[n++] = base8[i];
            if (ext3[0] && n < (int)sizeof(dst->name) - 2) {
                dst->name[n++] = '.';
                for (int i = 0; ext3[i] && n < (int)sizeof(dst->name) - 1; i++)
                    dst->name[n++] = ext3[i];
            }
            dst->name[n] = '\0';
        }
        if (dst->name[0] != '\0') s->count++;
    }
    s->have = 0;
    s->last = 0;
    return 0;
}

int fat16_list_dir(struct fat16_volume *vol, uint32_t cluster,
                   fat16_entry_t *out, int max) {
    struct dir_list_ud ud;
    if (vol == NULL || out == NULL || max <= 0) return -1;
    if (cluster == 0) return fat16_list_root(vol, out, max);
    memset(&ud, 0, sizeof(ud));
    ud.out = out;
    ud.max = max;
    ud.skip_dots = 1;
    dir_foreach(vol, cluster, dir_list_visit, &ud);
    return ud.count;
}

struct dir_find_ud {
    const char *name;
    uint8_t exact[11];
    uint16_t lfn[20 * 13];
    int last, have;
    uint8_t *result;
};

static int dir_find_visit(uint8_t *entry, void *ud) {
    struct dir_find_ud *s = (struct dir_find_ud *)ud;
    uint8_t attr = entry[11];
    if (entry[0] == 0xE5) { s->have = 0; s->last = 0; return 0; }
    if (attr == FAT16_ATTR_LFN) {
        static const int offsets[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
        int sequence = entry[0] & 0x1F;
        if (sequence >= 1 && sequence <= 20) {
            int slot = (sequence - 1) * 13;
            for (int c = 0; c < 13; c++)
                s->lfn[slot + c] = (uint16_t)(entry[offsets[c]] |
                                              (entry[offsets[c] + 1] << 8));
            if (sequence > s->last) s->last = sequence;
            s->have = 1;
        }
        return 0;
    }
    if (attr & FAT16_ATTR_VOLUME) { s->have = 0; s->last = 0; return 0; }
    if (entry[0] == '.') { s->have = 0; s->last = 0; return 0; }
    if (s->have) {
        char decoded[64];
        size_t used = 0;
        for (int c = 0; c < s->last * 13 && used + 1 < sizeof(decoded); c++) {
            uint16_t unit = s->lfn[c];
            if (unit == 0 || unit == 0xFFFF) break;
            decoded[used++] = unit < 0x80 ? (char)unit : '?';
        }
        decoded[used] = '\0';
        if (strcmp(decoded, s->name) == 0) { s->result = entry; return 1; }
    }
    if (memcmp(entry, s->exact, 11) == 0) { s->result = entry; return 1; }
    s->have = 0;
    s->last = 0;
    return 0;
}

static uint8_t *find_entry_in(struct fat16_volume *vol, uint32_t cluster,
                              const char *name) {
    struct dir_find_ud ud;
    char base[21], ext[4];
    int base_len, ext_len;
    if (!name_ok(name)) return NULL;
    memset(&ud, 0, sizeof(ud));
    ud.name = name;
    for (int i = 0; i < 11; i++) ud.exact[i] = ' ';
    split_name(name, base, &base_len, ext, &ext_len);
    for (int i = 0; i < base_len && i < 8; i++) ud.exact[i] = (uint8_t)base[i];
    for (int i = 0; i < ext_len && i < 3; i++) ud.exact[8 + i] = (uint8_t)ext[i];
    dir_foreach(vol, cluster, dir_find_visit, &ud);
    return ud.result;
}

int fat16_subdir_cluster(struct fat16_volume *vol, uint32_t cluster,
                         const char *name) {
    uint8_t *entry = find_entry_in(vol, cluster, name);
    if (entry == NULL || !(entry[11] & FAT16_ATTR_DIR)) return -1;
    return (int)((uint32_t)entry[26] | ((uint32_t)entry[27] << 8));
}

int fat16_read_file_in(struct fat16_volume *vol, uint32_t cluster,
                       const char *name, uint8_t *out, size_t cap) {
    uint8_t *entry = find_entry_in(vol, cluster, name);
    uint32_t size, first, cluster_bytes, done, cur;
    if (entry == NULL || (entry[11] & FAT16_ATTR_DIR) || out == NULL) return -1;
    size = (uint32_t)entry[28] | ((uint32_t)entry[29] << 8) |
           ((uint32_t)entry[30] << 16) | ((uint32_t)entry[31] << 24);
    if (size > cap) return -1;
    first = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8);
    cluster_bytes = (uint32_t)vol->sectors_per_cluster * vol->bytes_per_sector;
    cur = first;
    done = 0;
    while (done < size && cur >= 2 && cur < FAT16_EOF - 8) {
        uint8_t *src = vol->data +
            (size_t)cluster_sector(vol, cur) * vol->bytes_per_sector;
        uint32_t chunk = size - done;
        if (chunk > cluster_bytes) chunk = cluster_bytes;
        memcpy(out + done, src, chunk);
        done += chunk;
        cur = fat_next(vol, cur);
    }
    return done == size ? (int)size : -1;
}

/* Resolve "/a/b" to a directory cluster (0 = root). Returns -1 if any
 * component is missing or is not a directory. */
static int path_cluster(struct fat16_volume *vol, const char *path) {
    uint32_t cluster = 0;
    const char *p = path;
    if (p == NULL) return -1;
    while (*p == '/') p++;
    while (*p != '\0') {
        char comp[64];
        size_t n = 0;
        while (p[n] != '\0' && p[n] != '/' && n < sizeof(comp) - 1) {
            comp[n] = p[n];
            n++;
        }
        comp[n] = '\0';
        if (n > 0) {
            int next = fat16_subdir_cluster(vol, cluster, comp);
            if (next < 0) return -1;
            cluster = (uint32_t)next;
        }
        p += n;
        while (*p == '/') p++;
    }
    return (int)cluster;
}

int fat16_list_path(struct fat16_volume *vol, const char *path,
                    fat16_entry_t *out, int max) {
    int cluster = path_cluster(vol, path);
    if (cluster < 0) return -1;
    return fat16_list_dir(vol, (uint32_t)cluster, out, max);
}

int fat16_read_path(struct fat16_volume *vol, const char *path,
                    uint8_t *out, size_t cap) {
    char parent[256];
    const char *slash;
    int cluster;
    if (path == NULL) return -1;
    slash = strrchr(path, '/');
    if (slash == NULL) return fat16_read_file_in(vol, 0, path, out, cap);
    if (slash == path) {
        cluster = 0;
    } else {
        size_t n = (size_t)(slash - path);
        if (n >= sizeof(parent)) return -1;
        memcpy(parent, path, n);
        parent[n] = '\0';
        cluster = path_cluster(vol, parent);
        if (cluster < 0) return -1;
    }
    return fat16_read_file_in(vol, (uint32_t)cluster, slash + 1, out, cap);
}
