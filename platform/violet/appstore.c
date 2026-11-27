/* SPDX-License-Identifier: MIT */
/* platform/violet/appstore.c - durable QEAPP catalog in the p77 reserve.
 *
 * Layout (little-endian, offsets from the start of p77):
 *   header A @ 32 MiB, header B @ 32 MiB + 128 (64 B each)
 *   payload A grows up from 32 MiB + 256
 *   payload B grows down from the 64 MiB end
 *   payload: count x { u32 name_len, u32 data_len, name bytes, package bytes }
 *
 * The payload length is the real size of the catalog, so both the app count and
 * the package sizes are bounded only by the reserve (~16 MiB per slot), not by
 * a fixed 4 MiB slot or an 8-app table. A save writes the payload first,
 * fsyncs, then writes the header (the commit point) and verifies both by CRC.
 * The other slot is never touched, so a power loss leaves the previous catalog
 * readable.
 */
#include "appstore.h"

#include "channel/fs/fat16.h"
#include "core/log.h"
#include "violet.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef APPSTORE_PATH
#ifdef VQEAF_RAM_ONLY_DIAGNOSTICS
#define APPSTORE_PATH "/tmp/qeaf-appstore.bin"
#else
#define APPSTORE_PATH "/dev/mmcblk0p77"
#endif
#endif
#define APPSTORE_MAGIC "QEAFAPP4"
#define APPSTORE_VERSION 4u
#define APPSTORE_MAGIC_V3 "QEAFAPP3"
#define APPSTORE_VERSION_V3 3u
/* v2/v3 catalogs remain readable and are migrated on the next save. */
#define APPSTORE_MAGIC_V2 "QEAFAPP2"
#define APPSTORE_VERSION_V2 2u
#ifdef VQEAF_RAM_ONLY_DIAGNOSTICS
#define APPSTORE_BASE 0u
#define APPSTORE_END (16u * 1024u * 1024u)
#else
#define APPSTORE_BASE (32u * 1024u * 1024u)
#define APPSTORE_END (64u * 1024u * 1024u)
#endif
#define APPSTORE_HDR_A APPSTORE_BASE
#define APPSTORE_HDR_B (APPSTORE_BASE + 128u)
#define APPSTORE_DATA (APPSTORE_BASE + 256u)
#define APPSTORE_SLOT_MAX ((APPSTORE_END - APPSTORE_DATA) / 2u)
#define APPSTORE_PAYLOAD_A APPSTORE_DATA
#define APPSTORE_PAYLOAD_B (APPSTORE_END - APPSTORE_SLOT_MAX)

typedef struct __attribute__((packed)) {
    char magic[8];
    uint32_t version;
    uint32_t seq;
    uint32_t count;
    uint32_t payload_len;
    uint32_t payload_off;
    uint32_t crc;
    uint32_t header_size;
    uint8_t pad[32];
} appstore_hdr_t;

static struct fat16_volume *g_vol;
static void (*g_lock)(void);
static void (*g_unlock)(void);
static int g_restoring;

void appstore_set_volume(struct fat16_volume *vol, void (*lock)(void),
                         void (*unlock)(void))
{
    g_vol = vol;
    g_lock = lock;
    g_unlock = unlock;
}

void appstore_set_restoring(int restoring)
{
    g_restoring = restoring;
}

int appstore_is_restoring(void)
{
    return g_restoring;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for(size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for(int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static int header_valid(const appstore_hdr_t *h)
{
    int v3 = memcmp(h->magic, APPSTORE_MAGIC, 8) == 0 &&
             h->version == APPSTORE_VERSION;
    int v3_old = memcmp(h->magic, APPSTORE_MAGIC_V3, 8) == 0 &&
                 h->version == APPSTORE_VERSION_V3;
    int v2 = memcmp(h->magic, APPSTORE_MAGIC_V2, 8) == 0 &&
             h->version == APPSTORE_VERSION_V2;
    if(!v3 && !v3_old && !v2) return 0;
    if(h->header_size != sizeof(appstore_hdr_t)) return 0;
    if(h->payload_len > APPSTORE_SLOT_MAX) return 0;
    if(h->payload_off < APPSTORE_DATA) return 0;
    if((uint64_t)h->payload_off + h->payload_len > APPSTORE_END) return 0;
    return 1;
}

static int read_header(int fd, uint32_t offset, appstore_hdr_t *out)
{
    memset(out, 0, sizeof(*out));
    return pread(fd, out, sizeof(*out), (off_t)offset) ==
           (ssize_t)sizeof(*out) && header_valid(out) ? 0 : -1;
}

/* Allocates the payload described by the header and verifies its CRC. */
static int read_payload(int fd, const appstore_hdr_t *h, uint8_t **out)
{
    uint8_t *payload;

    *out = NULL;
    if(h->payload_len == 0) return 0;
    payload = malloc(h->payload_len);
    if(payload == NULL) return -1;
    if(pread(fd, payload, h->payload_len, (off_t)h->payload_off) !=
       (ssize_t)h->payload_len ||
       crc32_bytes(payload, h->payload_len) != h->crc) {
        free(payload);
        return -1;
    }
    *out = payload;
    return 0;
}

/* Opens the newest valid slot (falling back to the other if the newest payload
 * is torn) and returns its header plus an allocated payload. */
static int load_latest(int fd, appstore_hdr_t *out, uint8_t **payload)
{
    appstore_hdr_t h[2];
    uint32_t off[2] = { APPSTORE_HDR_A, APPSTORE_HDR_B };
    int have[2];
    int first, second;

    have[0] = read_header(fd, off[0], &h[0]) == 0;
    have[1] = read_header(fd, off[1], &h[1]) == 0;
    if(!have[0] && !have[1]) return -1;
    if(have[0] && have[1])
        first = h[0].seq >= h[1].seq ? 0 : 1;
    else
        first = have[0] ? 0 : 1;
    second = 1 - first;

    if(read_payload(fd, &h[first], payload) == 0) {
        *out = h[first];
        return 0;
    }
    if(have[second] && read_payload(fd, &h[second], payload) == 0) {
        *out = h[second];
        return 0;
    }
    return -1;
}

/* Reads a package from the RAM volume at its exact size. Packages are no
 * longer capped at 512 KiB by the install path. */
static int read_package(const appstore_entry_t *entry, uint8_t **out,
                        size_t *size)
{
    uint8_t *buffer;
    int bytes, got;
    const char *name = entry->path != NULL ? entry->path : entry->name;

    *out = NULL;
    *size = 0;
    if(g_vol == NULL) return -1;
    if(g_lock) g_lock();
    bytes = fat16_file_size(g_vol, name);
    if(g_unlock) g_unlock();
    if(bytes <= 0 || (uint32_t)bytes > APPSTORE_SLOT_MAX) {
        vio_log("appstore: size %s = %d rejected", name, bytes);
        return -1;
    }
    buffer = malloc((size_t)bytes);
    if(buffer == NULL) {
        vio_log("appstore: malloc %d for %s failed", bytes, name);
        return -1;
    }
    if(g_lock) g_lock();
    got = fat16_read_path(g_vol, name, buffer, (size_t)bytes);
    if(g_unlock) g_unlock();
    if(got != bytes) {
        vio_log("appstore: read %s got %d of %d", entry->name, got, bytes);
        free(buffer);
        return -1;
    }
    *out = buffer;
    *size = (size_t)got;
    return 0;
}

static int write_slot(int fd, uint32_t header_off, uint32_t payload_off,
                      uint32_t seq, uint32_t count, const uint8_t *payload,
                      uint32_t len)
{
    appstore_hdr_t header, verify_header;
    uint8_t *verify_payload;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, APPSTORE_MAGIC, 8);
    header.version = APPSTORE_VERSION;
    header.seq = seq;
    header.count = count;
    header.payload_len = len;
    header.payload_off = payload_off;
    header.crc = len ? crc32_bytes(payload, len) : 0u;
    header.header_size = sizeof(header);

    /* Payload first: the header is the commit point. */
    if(len > 0 &&
       pwrite(fd, payload, len, (off_t)payload_off) != (ssize_t)len)
        return -1;
    if(fsync(fd) != 0) return -1;
    if(pwrite(fd, &header, sizeof(header), (off_t)header_off) !=
       (ssize_t)sizeof(header))
        return -1;
    if(fsync(fd) != 0) return -1;

    if(pread(fd, &verify_header, sizeof(verify_header), (off_t)header_off) !=
       (ssize_t)sizeof(verify_header))
        return -1;
    if(memcmp(&verify_header, &header, sizeof(header)) != 0) return -1;
    if(len == 0) return 0;
    verify_payload = malloc(len);
    if(verify_payload == NULL) return -1;
    if(pread(fd, verify_payload, len, (off_t)payload_off) != (ssize_t)len ||
       crc32_bytes(verify_payload, len) != header.crc) {
        free(verify_payload);
        return -1;
    }
    free(verify_payload);
    return 0;
}

int appstore_save(const appstore_entry_t *entries, int count)
{
    int fd;
    int open_flags = O_RDWR | O_CLOEXEC | O_SYNC | O_NOFOLLOW;
    appstore_hdr_t a, b, current, header;
    uint8_t *payload = NULL;
    size_t capacity = 0, used = 0;
    int have_a, have_b, target_a;
    uint32_t seq_a = 0, seq_b = 0, next_seq, header_off, payload_off;
    const appstore_hdr_t *live = NULL;

    if(count < 0) return -1;
    if(count > 0 && entries == NULL) return -1;
    for(int i = 0; i < count; i++) {
        if(entries[i].name[0] == '\0') return -1;
        for(int j = 0; j < i; j++) {
            if(strcmp(entries[i].name, entries[j].name) == 0) {
                vio_log("appstore: duplicate catalog key %s rejected",
                        entries[i].name);
                return -1;
            }
        }
    }
    for(int i = 0; i < count; i++) {
        uint8_t *package = NULL;
        size_t package_size = 0;
        size_t name_len = strlen(entries[i].name);
        size_t need;
        int owned = 1;

        size_t app_data_size = entries[i].app_data_size;

        if(name_len == 0 || name_len >= APPSTORE_NAME) goto fail;
        if(app_data_size > 64u * 1024u) {
            vio_log("appstore: data for %s is %u bytes (cap 65536)",
                    entries[i].name, (unsigned)app_data_size);
            goto fail;
        }
        if(read_package(&entries[i], &package, &package_size) != 0) {
            /* Restored apps exist only in this store; their FAT file is gone
             * after the RAM volume reset, so reuse the bytes the caller
             * already read back from the catalog. */
            if(entries[i].data != NULL && entries[i].data_size > 0) {
                package = (uint8_t *)entries[i].data;
                package_size = entries[i].data_size;
                owned = 0;
            }
            else goto fail;
        }
        need = 16 + name_len + package_size + app_data_size;
        if(need > APPSTORE_SLOT_MAX || used + need > APPSTORE_SLOT_MAX) {
            if(owned) free(package);
            vio_log("appstore: catalog needs %u bytes, slot holds %u\n",
                      (unsigned)(used + need), (unsigned)APPSTORE_SLOT_MAX);
            goto fail;
        }
        if(used + need > capacity) {
            size_t want = capacity ? capacity : 64u * 1024u;
            uint8_t *grown;
            while(want < used + need) want *= 2u;
            if(want > APPSTORE_SLOT_MAX) want = APPSTORE_SLOT_MAX;
            grown = realloc(payload, want);
            if(grown == NULL) {
                vio_log("appstore: payload realloc %u failed",
                        (unsigned)want);
                if(owned) free(package);
                goto fail;
            }
            payload = grown;
            capacity = want;
        }
        memcpy(payload + used, &(uint32_t){ (uint32_t)name_len }, 4);
        memcpy(payload + used + 4, &(uint32_t){ (uint32_t)package_size }, 4);
        memcpy(payload + used + 8, &(uint32_t){ (uint32_t)app_data_size }, 4);
        memcpy(payload + used + 12, &entries[i].granted_permissions, 4);
        memcpy(payload + used + 16, entries[i].name, name_len);
        memcpy(payload + used + 16 + name_len, package, package_size);
        if(app_data_size > 0)
            memcpy(payload + used + 16 + name_len + package_size,
                   entries[i].app_data, app_data_size);
        used += need;
        if(owned) free(package);
    }

#ifdef VQEAF_RAM_ONLY_DIAGNOSTICS
    open_flags |= O_CREAT;
#endif
    fd = open(APPSTORE_PATH, open_flags, 0600);
    if(fd < 0) {
        vio_log("appstore: cannot open %s for save\n", APPSTORE_PATH);
        goto fail;
    }
    have_a = read_header(fd, APPSTORE_HDR_A, &a) == 0;
    have_b = read_header(fd, APPSTORE_HDR_B, &b) == 0;
    if(have_a) seq_a = a.seq;
    if(have_b) seq_b = b.seq;
    if(have_a && have_b)
        live = a.seq >= b.seq ? &a : &b;
    else if(have_a)
        live = &a;
    else if(have_b)
        live = &b;

    next_seq = (seq_a > seq_b ? seq_a : seq_b) + 1u;
    /* Overwrite the older slot, so the newer catalog always survives a tear. */
    target_a = !(have_a && have_b) ? 1 : seq_a <= seq_b;
    if(target_a) {
        header_off = APPSTORE_HDR_A;
        payload_off = APPSTORE_PAYLOAD_A;
    }
    else {
        header_off = APPSTORE_HDR_B;
        payload_off = APPSTORE_PAYLOAD_B;
    }
    if(live != NULL && payload_off == live->payload_off) {
        /* Slot geometry changed (or both headers claim the same extent): use
         * the other side rather than overwriting the only valid catalog. */
        header_off = header_off == APPSTORE_HDR_A ? APPSTORE_HDR_B
                                                  : APPSTORE_HDR_A;
        payload_off = payload_off == APPSTORE_PAYLOAD_A ? APPSTORE_PAYLOAD_B
                                                        : APPSTORE_PAYLOAD_A;
    }

    if(write_slot(fd, header_off, payload_off, next_seq, (uint32_t)count,
                  payload, (uint32_t)used) != 0) {
        vio_log("appstore: slot write/verify failed\n");
        close(fd);
        goto fail;
    }
    close(fd);
    memset(&header, 0, sizeof(header));
    vio_log("appstore: saved %d app(s), seq %u, %u bytes (slot %c)\n", count,
              (unsigned)next_seq, (unsigned)used,
              header_off == APPSTORE_HDR_A ? 'A' : 'B');
    free(payload);
    return 0;

fail:
    free(payload);
    return -1;
}

int appstore_volume_has(const char *name)
{
    int bytes;

    if(g_vol == NULL || name == NULL) return 0;
    if(g_lock) g_lock();
    bytes = fat16_file_size(g_vol, name);
    if(g_unlock) g_unlock();
    return bytes > 0;
}

int appstore_info(uint32_t *seq, uint32_t *count, uint32_t *bytes, int *slot)
{
    int fd;
    appstore_hdr_t a, b;
    int have_a, have_b;
    const appstore_hdr_t *chosen;

    if(seq == NULL || count == NULL || bytes == NULL || slot == NULL)
        return -1;
    fd = open(APPSTORE_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0) return -1;
    have_a = read_header(fd, APPSTORE_HDR_A, &a) == 0;
    have_b = read_header(fd, APPSTORE_HDR_B, &b) == 0;
    close(fd);
    if(!have_a && !have_b) return -1;
    if(have_a && have_b) {
        chosen = a.seq >= b.seq ? &a : &b;
        *slot = a.seq >= b.seq ? 0 : 1;
    }
    else {
        chosen = have_a ? &a : &b;
        *slot = have_a ? 0 : 1;
    }
    *seq = chosen->seq;
    *count = chosen->count;
    *bytes = chosen->payload_len;
    return 0;
}

typedef struct {
    uint32_t name_len, pkg_len, data_len, grants;
    size_t head;
} appstore_rec_t;

/* One catalog record: name, package bytes and the optional per-app data blob.
 * v2 records have no data field, so an upgraded image still reads them. */
static int record_at(const appstore_hdr_t *h, const uint8_t *payload,
                     size_t pos, appstore_rec_t *rec)
{
    uint32_t name_len, pkg_len, data_len = 0, grants = 0;
    size_t head = h->version >= APPSTORE_VERSION ? 16u
                : h->version >= APPSTORE_VERSION_V3 ? 12u : 8u;

    if(pos + head > h->payload_len) return -1;
    memcpy(&name_len, payload + pos, 4);
    memcpy(&pkg_len, payload + pos + 4, 4);
    if(head >= 12) memcpy(&data_len, payload + pos + 8, 4);
    if(head == 16) memcpy(&grants, payload + pos + 12, 4);
    if(name_len == 0 || name_len >= APPSTORE_NAME) return -1;
    if(pos + head + name_len + pkg_len + data_len > h->payload_len) return -1;
    rec->name_len = name_len;
    rec->pkg_len = pkg_len;
    rec->data_len = data_len;
    rec->grants = grants;
    rec->head = head;
    return 0;
}

int appstore_restore(char names[][APPSTORE_NAME], int max)
{
    int fd;
    int got = 0;
    appstore_hdr_t chosen;
    uint8_t *payload = NULL;
    size_t pos = 0;

    if(names == NULL || max <= 0) return -1;
    fd = open(APPSTORE_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0) {
        vio_log("appstore: %s missing; nothing to restore\n", APPSTORE_PATH);
        return -1;
    }
    if(load_latest(fd, &chosen, &payload) != 0) {
        close(fd);
        vio_log("appstore: no valid slot\n");
        return -1;
    }
    while(got < max) {
        appstore_rec_t rec;

        if(record_at(&chosen, payload, pos, &rec) != 0) break;
        memcpy(names[got], payload + pos + rec.head, rec.name_len);
        names[got][rec.name_len] = '\0';
        got++;
        pos += rec.head + rec.name_len + rec.pkg_len + rec.data_len;
    }
    close(fd);
    free(payload);
    vio_log("appstore: seq %u, %d app(s)\n", (unsigned)chosen.seq, got);
    return got;
}

int appstore_read(const char *name, uint8_t **out, size_t *size)
{
    int fd;
    appstore_hdr_t chosen;
    uint8_t *payload = NULL;
    size_t pos = 0;
    const char *base = name;

    if(name == NULL || out == NULL || size == NULL) return -1;
    *out = NULL;
    *size = 0;
    if(name[0] == '/') base = name + 1;
    fd = open(APPSTORE_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0) return -1;
    if(load_latest(fd, &chosen, &payload) != 0) {
        close(fd);
        return -1;
    }
    while(pos < chosen.payload_len) {
        appstore_rec_t rec;

        if(record_at(&chosen, payload, pos, &rec) != 0) break;
        if(strlen(base) == rec.name_len &&
           memcmp(payload + pos + rec.head, base, rec.name_len) == 0) {
            uint8_t *copy = malloc(rec.pkg_len ? rec.pkg_len : 1);
            if(copy == NULL) break;
            memcpy(copy, payload + pos + rec.head + rec.name_len, rec.pkg_len);
            *out = copy;
            *size = rec.pkg_len;
            close(fd);
            free(payload);
            return 0;
        }
        pos += rec.head + rec.name_len + rec.pkg_len + rec.data_len;
    }
    close(fd);
    free(payload);
    return -1;
}

int appstore_read_data(const char *name, uint8_t **out, size_t *size)
{
    int fd;
    appstore_hdr_t chosen;
    uint8_t *payload = NULL;
    size_t pos = 0;
    const char *base = name;

    if(name == NULL || out == NULL || size == NULL) return -1;
    *out = NULL;
    *size = 0;
    if(name[0] == '/') base = name + 1;
    fd = open(APPSTORE_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0) return -1;
    if(load_latest(fd, &chosen, &payload) != 0) {
        close(fd);
        return -1;
    }
    while(pos < chosen.payload_len) {
        appstore_rec_t rec;

        if(record_at(&chosen, payload, pos, &rec) != 0) break;
        if(strlen(base) == rec.name_len &&
           memcmp(payload + pos + rec.head, base, rec.name_len) == 0) {
            uint8_t *copy;

            if(rec.data_len == 0) break;
            copy = malloc(rec.data_len);
            if(copy == NULL) break;
            memcpy(copy, payload + pos + rec.head + rec.name_len + rec.pkg_len,
                   rec.data_len);
            *out = copy;
            *size = rec.data_len;
            close(fd);
            free(payload);
            return 0;
        }
        pos += rec.head + rec.name_len + rec.pkg_len + rec.data_len;
    }
    close(fd);
    free(payload);
    return -1;
}


int appstore_read_grants(const char *name, uint32_t *grants)
{
    int fd;
    appstore_hdr_t chosen;
    uint8_t *payload = NULL;
    size_t pos = 0;
    const char *base = name;

    if(name == NULL || grants == NULL) return -1;
    *grants = 0u;
    if(name[0] == '/') base = name + 1;
    fd = open(APPSTORE_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0) return -1;
    if(load_latest(fd, &chosen, &payload) != 0) {
        close(fd);
        return -1;
    }
    while(pos < chosen.payload_len) {
        appstore_rec_t rec;
        if(record_at(&chosen, payload, pos, &rec) != 0) break;
        if(strlen(base) == rec.name_len &&
           memcmp(payload + pos + rec.head, base, rec.name_len) == 0) {
            *grants = rec.grants;
            close(fd);
            free(payload);
            return 0;
        }
        pos += rec.head + rec.name_len + rec.pkg_len + rec.data_len;
    }
    close(fd);
    free(payload);
    return -1;
}
