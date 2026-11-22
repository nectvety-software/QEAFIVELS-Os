/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_sys_tools_test.c - host gate for the native system utilities.
 *
 * The host has no device mounts or /proc, so the default answers must be
 * empty/errors. Injected providers then prove the UI-facing filtering and
 * data plumbing without ever touching the Windows host filesystem.
 */
#include "vqeaf_sys_tools.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, msg) do { \
    if(cond) printf("ok   %s\n", msg); \
    else { printf("FAIL %s\n", msg); failures++; } \
} while(0)

static int fake_mounts(vqeaf_mount_t *out, int max, int ntfs_only)
{
    static const vqeaf_mount_t all[] = {
        { "/", "ext4", 0, 0 },
        { "/mnt/usb", "ntfs3", 100, 20 },
        { "/media/data", "fuseblk", 200, 30 },
        { "/proc", "proc", 0, 0 },
    };
    static const char *const opts[] = {
        "rw", "rw", "rw,ntfs3", "rw"
    };
    int n = 0;
    for(int i = 0; i < 4 && n < max; i++) {
        if(ntfs_only && !vqeaf_sys_is_ntfs(all[i].fstype, opts[i]))
            continue;
        out[n++] = all[i];
    }
    return n;
}

static int fake_search(const char *root, const char *needle,
                       vqeaf_search_hit_t *out, int max,
                       int max_depth, uint64_t *scanned)
{
    if(strcmp(root, "/mnt/usb") != 0 || strcmp(needle, "report") != 0 ||
       max < 2 || max_depth < 1)
        return -EINVAL;
    memset(out, 0, sizeof(out[0]) * 2);
    snprintf(out[0].path, sizeof(out[0].path), "/mnt/usb/docs/report.log");
    out[0].size = 42;
    snprintf(out[1].path, sizeof(out[1].path), "/mnt/usb/report.txt");
    out[1].size = 7;
    if(scanned != NULL) *scanned = 12;
    return 2;
}

static int fake_processes(vqeaf_process_t *out, int max)
{
    if(max < 3) return -EINVAL;
    memset(out, 0, sizeof(out[0]) * 3);
    out[0].pid = 1; strcpy(out[0].name, "vqeaf-init"); strcpy(out[0].state, "S");
    out[1].pid = 7; strcpy(out[1].name, "rescue"); strcpy(out[1].state, "R");
    out[1].rss_kb = 2048;
    out[2].pid = 31; strcpy(out[2].name, "shell"); strcpy(out[2].state, "S");
    out[2].rss_kb = 8192;
    return 3;
}

static int fake_cleanup(const char *root, vqeaf_cleanup_item_t *out,
                        int max, uint64_t *total)
{
    if(strcmp(root, "/tmp") != 0 || max < 2) return -EINVAL;
    memset(out, 0, sizeof(out[0]) * 2);
    snprintf(out[0].path, sizeof(out[0].path), "/tmp/session.tmp");
    out[0].size = 10;
    snprintf(out[1].path, sizeof(out[1].path), "/tmp/cache");
    out[1].size = 0;
    out[1].is_dir = 1;
    *total = 10;
    return 2;
}

int main(void)
{
    vqeaf_mount_t mounts[8];
    vqeaf_search_hit_t hits[4];
    vqeaf_process_t procs[4];
    vqeaf_cleanup_item_t items[4];
    uint64_t scanned = 0, total = 0;
    char report[256];
    int count;

    /* Pure classification. */
    CHECK(vqeaf_sys_is_ntfs("ntfs3", "rw") == 1, "ntfs3 is NTFS");
    CHECK(vqeaf_sys_is_ntfs("fuseblk", "rw,ntfs3") == 1, "fuse ntfs is NTFS");
    CHECK(vqeaf_sys_is_ntfs("fuseblk", "rw") == 0, "plain fuseblk is not NTFS");
    CHECK(vqeaf_sys_is_ntfs("ext4", "rw") == 0, "ext4 is not NTFS");
    CHECK(vqeaf_sys_cleanup_candidate("debug.log") == 1, "log is a candidate");
    CHECK(vqeaf_sys_cleanup_candidate("old.tmp") == 1, "tmp is a candidate");
    CHECK(vqeaf_sys_cleanup_candidate("cache") == 1, "cache dir is a candidate");
    CHECK(vqeaf_sys_cleanup_candidate("notes.txt") == 0, "document is not a candidate");
    CHECK(vqeaf_sys_cleanup_candidate(".qeaf-trash") == 0, "quarantine is not a candidate");
    CHECK(vqeaf_sys_signal_allowed(1) == 0, "PID 1 cannot be signalled");
    CHECK(vqeaf_sys_signal_allowed(0) == 0, "PID 0 cannot be signalled");
    CHECK(vqeaf_sys_signal_allowed(12345) == 1, "ordinary PID can be signalled");

    /* Honest host defaults: no fake device data. */
    CHECK(vqeaf_sys_mounts(mounts, 8, 1) == 0, "host reports no NTFS mount");
    CHECK(vqeaf_sys_search("/", "a", hits, 4, 4, &scanned) == -ENOSYS,
          "host search refuses without a device backend");
    CHECK(vqeaf_sys_processes(procs, 4) == -ENOSYS,
          "host process list refuses without /proc");
    CHECK(vqeaf_sys_cleanup_scan("/tmp", items, 4, &total) == -ENOSYS,
          "host cleanup scan refuses without a device backend");
    CHECK(vqeaf_sys_cleanup_quarantine("/tmp", items, 1,
                                       report, sizeof(report)) == -ENOSYS,
          "host quarantine refuses instead of deleting");
    CHECK(strstr(report, "không") != NULL, "host quarantine says nothing moved");

    /* Injected device-shaped data. */
    vqeaf_sys_set_mounts_provider(fake_mounts);
    count = vqeaf_sys_mounts(mounts, 8, 1);
    CHECK(count == 2, "NTFS filter keeps exactly two volumes");
    CHECK(count == 2 && strcmp(mounts[0].path, "/mnt/usb") == 0 &&
          strcmp(mounts[1].fstype, "fuseblk") == 0,
          "NTFS filter keeps ntfs3 and fuse ntfs only");
    vqeaf_sys_set_mounts_provider(NULL);

    vqeaf_sys_set_search_provider(fake_search);
    count = vqeaf_sys_search("/mnt/usb", "report", hits, 4, 8, &scanned);
    CHECK(count == 2 && scanned == 12, "search provider result and scan count");
    CHECK(count == 2 && strstr(hits[0].path, "report.log") != NULL &&
          hits[1].size == 7, "search hits keep backend paths and sizes");
    vqeaf_sys_set_search_provider(NULL);

    vqeaf_sys_set_processes_provider(fake_processes);
    count = vqeaf_sys_processes(procs, 4);
    CHECK(count == 3 && procs[0].pid == 1 && procs[2].rss_kb == 8192,
          "process provider data reaches the UI layer");
    vqeaf_sys_set_processes_provider(NULL);

    vqeaf_sys_set_cleanup_scan_provider(fake_cleanup);
    count = vqeaf_sys_cleanup_scan("/tmp", items, 4, &total);
    CHECK(count == 2 && total == 10, "cleanup provider data and total bytes");
    CHECK(count == 2 && items[1].is_dir == 1, "cleanup keeps directory flag");
    vqeaf_sys_set_cleanup_scan_provider(NULL);

    /* Providers are reset: the honest defaults return. */
    CHECK(vqeaf_sys_mounts(mounts, 8, 1) == 0, "provider reset restores empty mounts");
    CHECK(vqeaf_sys_quarantine_dir() == NULL ||
          vqeaf_sys_quarantine_dir()[0] == '\0', "no fake quarantine path");

    if(failures) {
        printf("vqeaf_sys_tools: %d FAIL\n", failures);
        return 1;
    }
    printf("vqeaf_sys_tools: ALL OK (6 groups)\n");
    return 0;
}
