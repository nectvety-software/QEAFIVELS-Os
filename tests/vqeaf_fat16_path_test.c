/* SPDX-License-Identifier: MIT */
/* Exercises the cluster-aware FAT16 browsing API on a real in-RAM volume:
 * the same code path the file manager's rescue tab uses on device.
 * Subdirectory files are placed the way a PC places them (the directory
 * entry, LFN records included, is moved into the directory cluster), because
 * the phone only ever reads what the host wrote there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/fat16.h"

static int g_failures;
#define CHECK(cond, msg) do { \
    if(cond) printf("ok   %s\n", (msg)); \
    else { printf("FAIL %s\n", (msg)); g_failures++; } \
} while(0)

static struct fat16_volume vol;

static int find_entry(const fat16_entry_t *list, int n, const char *name)
{
    for(int i = 0; i < n; i++)
        if(strcmp(list[i].name, name) == 0) return i;
    return -1;
}

static uint8_t *dir_base(uint32_t cluster)
{
    return vol.data + (size_t)(vol.data_start +
                               (cluster - 2) * vol.sectors_per_cluster) *
                      vol.bytes_per_sector;
}

/* Host-style move: copy the LFN run plus the short entry into the first free
 * slots of the directory cluster, then mark the root slots deleted. The
 * cluster chain and the size field stay exactly as the writer left them. */
static void move_root_file_into(uint32_t dir_cluster, const char *name83)
{
    uint8_t *root = vol.data +
                    (size_t)vol.root_dir_start * vol.bytes_per_sector;
    uint32_t i, start, slot;
    uint8_t *dst;

    for(i = 0; i < FAT16_ROOT_ENTRIES; i++) {
        uint8_t *e = root + i * FAT16_ENTRY_SIZE;
        if(e[0] == 0x00) break;
        if(e[0] == 0xE5 || e[11] == FAT16_ATTR_LFN ||
           (e[11] & FAT16_ATTR_VOLUME) || (e[11] & FAT16_ATTR_DIR))
            continue;
        if(memcmp(e, name83, 11) == 0) break;
    }
    if(i >= FAT16_ROOT_ENTRIES) {
        printf("FAIL move: root entry %s not found\n", name83);
        g_failures++;
        return;
    }
    start = i;
    while(start > 0 &&
          root[(start - 1) * FAT16_ENTRY_SIZE + 11] == FAT16_ATTR_LFN)
        start--;

    dst = dir_base(dir_cluster);
    slot = 2;
    while(slot * FAT16_ENTRY_SIZE <
              (uint32_t)(vol.sectors_per_cluster * vol.bytes_per_sector) &&
          dst[slot * FAT16_ENTRY_SIZE] != 0x00 &&
          dst[slot * FAT16_ENTRY_SIZE] != 0xE5)
        slot++;
    for(uint32_t e = start; e <= i; e++) {
        memcpy(dst + slot * FAT16_ENTRY_SIZE, root + e * FAT16_ENTRY_SIZE,
               FAT16_ENTRY_SIZE);
        root[e * FAT16_ENTRY_SIZE] = 0xE5;
        slot++;
    }
}

/* Splices BIG.BIN's chain: the second cluster's payload moves to a fresh
 * cluster and the FAT points there instead. The file's bytes are unchanged,
 * its physical runs are not - the shape a 20 MiB image has when the host
 * wrote it in several goes. */
static void fragment_big(void)
{
    uint8_t *root = vol.data +
                    (size_t)vol.root_dir_start * vol.bytes_per_sector;
    uint8_t *fat = vol.data + (size_t)vol.fat_start * vol.bytes_per_sector;
    uint32_t i, first, next, target, cbytes;
    uint32_t cluster_bytes = (uint32_t)vol.sectors_per_cluster *
                             vol.bytes_per_sector;
    uint16_t value, link;

    for(i = 0; i < FAT16_ROOT_ENTRIES; i++) {
        uint8_t *e = root + i * FAT16_ENTRY_SIZE;
        if(e[0] == 0x00) break;
        if(e[0] == 0xE5 || e[11] == FAT16_ATTR_LFN ||
           (e[11] & FAT16_ATTR_VOLUME) || (e[11] & FAT16_ATTR_DIR))
            continue;
        if(memcmp(e, "BIG     BIN", 11) == 0) break;
    }
    if(i >= FAT16_ROOT_ENTRIES) {
        printf("FAIL fragment: BIG.BIN not found\n");
        g_failures++;
        return;
    }
    first = (uint32_t)root[i * FAT16_ENTRY_SIZE + 26] |
            ((uint32_t)root[i * FAT16_ENTRY_SIZE + 27] << 8);
    memcpy(&value, fat + first * 2, 2);
    next = value;
    target = vol.next_cluster++;
    cbytes = cluster_bytes;
    memcpy(vol.data + (size_t)(vol.data_start +
                               (target - 2) * vol.sectors_per_cluster) *
                      vol.bytes_per_sector,
           vol.data + (size_t)(vol.data_start +
                               (next - 2) * vol.sectors_per_cluster) *
                      vol.bytes_per_sector,
           cbytes);
    memcpy(&value, fat + next * 2, 2);   /* what followed the moved cluster */
    link = (uint16_t)target;
    memcpy(fat + target * 2, &value, 2);
    memcpy(fat + first * 2, &link, 2);
    printf("note fragmentation: chain %u -> %u -> %u (was %u -> %u)\n",
           (unsigned)first, (unsigned)target, (unsigned)value,
           (unsigned)first, (unsigned)next);
}

int main(void)
{
    uint8_t *data = calloc(FAT16_VOLUME_SIZE, 1);
    uint8_t big[9000];
    uint8_t out[9000];
    fat16_entry_t list[64];
    int n, apps, tools, docs, i;
    static const char note[] = "ghi chu trong Apps";
    static const char readme[] = "tep ten dai trong Apps";

    if(data == NULL) return 2;
    for(i = 0; i < (int)sizeof(big); i++) big[i] = (uint8_t)(i * 7 + 3);

    CHECK(fat16_init(&vol, data, FAT16_VOLUME_SIZE) == 0, "volume init");
    CHECK(fat16_write_file(&vol, "DEVINFO.TXT", (const uint8_t *)"violet", 6)
          == 0, "root file written");
    CHECK(fat16_write_file(&vol, "BIG.BIN", big, sizeof(big)) == 0,
          "multi-cluster root file written");
    CHECK(fat16_write_file(&vol, "NOTE.TXT", (const uint8_t *)note,
                           sizeof(note) - 1) == 0, "note written at root");
    CHECK(fat16_write_file(&vol, "readme.txt", (const uint8_t *)readme,
                           sizeof(readme) - 1) == 0, "long name written");

    apps = fat16_mkdir(&vol, "Apps");
    tools = fat16_mkdir_in(&vol, (uint32_t)apps, "Tools");
    docs = fat16_mkdir(&vol, "Documents");
    CHECK(apps >= 2 && tools >= 2 && docs >= 2, "directories created");
    move_root_file_into((uint32_t)apps, "NOTE    TXT");
    move_root_file_into((uint32_t)apps, "README  TXT");

    n = fat16_list_path(&vol, "/", list, 64);
    CHECK(n > 0, "root path lists");
    CHECK(find_entry(list, n, "Apps") >= 0 &&
          list[find_entry(list, n, "Apps")].is_dir == 1, "root shows Apps dir");
    CHECK(find_entry(list, n, "Documents") >= 0, "root shows long dir name");
    CHECK(find_entry(list, n, "DEVINFO.TXT") >= 0, "root keeps DEVINFO.TXT");
    CHECK(find_entry(list, n, "NOTE.TXT") < 0, "moved file left the root");
    CHECK(find_entry(list, n, "readme.txt") < 0, "moved long name left root");

    n = fat16_list_path(&vol, "/Apps", list, 64);
    CHECK(n == 3, "subdir lists Tools plus two moved files");
    CHECK(find_entry(list, n, "Tools") >= 0 &&
          list[find_entry(list, n, "Tools")].is_dir == 1, "subdir shows Tools");
    i = find_entry(list, n, "NOTE.TXT");
    CHECK(i >= 0 && list[i].size == sizeof(note) - 1,
          "subdir entry keeps its size");
    CHECK(find_entry(list, n, "readme.txt") >= 0,
          "subdir decodes the long name");
    CHECK(find_entry(list, n, ".") < 0 && find_entry(list, n, "..") < 0,
          "subdir listing omits dot entries");
    CHECK(fat16_list_dir(&vol, (uint32_t)apps, list, 64) == n,
          "list_dir agrees with list_path");

    CHECK(fat16_list_path(&vol, "/Apps/Tools", list, 64) == 0,
          "empty nested dir lists zero");
    CHECK(fat16_list_path(&vol, "/Documents", list, 64) == 0,
          "long-name dir resolves");
    CHECK(fat16_list_path(&vol, "/Apps/Nope", list, 64) == -1,
          "missing dir rejected");
    CHECK(fat16_list_path(&vol, "/Apps/..", list, 64) == -1,
          "dot-dot cannot escape a subdir");

    CHECK(fat16_subdir_cluster(&vol, 0, "Apps") == apps, "root subdir cluster");
    CHECK(fat16_subdir_cluster(&vol, (uint32_t)apps, "Tools") == tools,
          "nested subdir cluster");
    CHECK(fat16_subdir_cluster(&vol, 0, "Documents") == docs,
          "long-name subdir cluster");
    CHECK(fat16_subdir_cluster(&vol, (uint32_t)apps, "Missing") == -1,
          "missing subdir cluster rejected");

    memset(out, 0, sizeof(out));
    CHECK(fat16_read_path(&vol, "/Apps/NOTE.TXT", out, sizeof(out)) ==
          (int)(sizeof(note) - 1) &&
          memcmp(out, note, sizeof(note) - 1) == 0,
          "nested 8.3 file reads back");
    memset(out, 0, sizeof(out));
    CHECK(fat16_read_path(&vol, "/Apps/readme.txt", out, sizeof(out)) ==
          (int)(sizeof(readme) - 1) &&
          memcmp(out, readme, sizeof(readme) - 1) == 0,
          "nested long-name file reads back");
    CHECK(fat16_read_file_in(&vol, (uint32_t)apps, "NOTE.TXT", out,
                             sizeof(out)) == (int)(sizeof(note) - 1),
          "read_file_in agrees with read_path");
    CHECK(fat16_read_path(&vol, "/DEVINFO.TXT", out, sizeof(out)) == 6,
          "root file still reads through path API");
    memset(out, 0, sizeof(out));
    CHECK(fat16_read_path(&vol, "/BIG.BIN", out, sizeof(out)) ==
          (int)sizeof(big) && memcmp(out, big, sizeof(big)) == 0,
          "multi-cluster chain reads back intact");
    CHECK(fat16_read_path(&vol, "/Apps/Tools/NOTE.TXT", out, sizeof(out)) == -1,
          "file absent from nested dir rejected");
    CHECK(fat16_read_path(&vol, "/Nope/x.txt", out, sizeof(out)) == -1,
          "missing parent rejected");
    CHECK(fat16_read_path(&vol, "/Apps/../DEVINFO.TXT", out, sizeof(out)) == -1,
          "dot-dot cannot escape on read");

    /* The flash verb now streams BOOTIMG.BIN by cluster chain, so a
     * fragmented file must read back byte for byte and arrive in more than
     * one run (one write per run on device). */
    fragment_big();
    {
        struct fat16_file file;
        const uint8_t *chunk;
        size_t got = 0;
        int runs = 0, step;

        CHECK(fat16_file_open(&vol, "BIG.BIN", &file) == 0 &&
              file.size == sizeof(big), "stream: opens the fragmented file");
        memset(out, 0, sizeof(out));
        while((step = fat16_file_next(&vol, &file, &chunk)) > 0) {
            memcpy(out + got, chunk, (size_t)step);
            got += (size_t)step;
            runs++;
        }
        CHECK(step == 0 && got == sizeof(big),
              "stream: walks the chain to the last byte");
        CHECK(memcmp(out, big, sizeof(big)) == 0,
              "stream: bytes match what was written");
        CHECK(runs > 1, "stream: fragmented chain arrives in several runs");
        CHECK(fat16_read_path(&vol, "/BIG.BIN", out, sizeof(out)) ==
              (int)sizeof(big) && memcmp(out, big, sizeof(big)) == 0,
              "fragmented file still reads through the path API");
    }

    printf(g_failures == 0 ? "ALL OK\n" : "%d FAILURES\n", g_failures);
    free(data);
    return g_failures == 0 ? 0 : 1;
}