/* SPDX-License-Identifier: MIT */
/* vqeaf_sys_tools.h - native mounts, search, processes and cleanup.
 *
 * Everything here is read-only except the two cleanup calls, and those only
 * rename files into a quarantine folder (never unlink). The host render build
 * has no device filesystem: it returns -ENOSYS unless a gate injects a
 * provider, so a screenshot can never be mistaken for real device data.
 */
#ifndef VQEAF_SYS_TOOLS_H
#define VQEAF_SYS_TOOLS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_SYS_PATH_MAX 512
#define VQEAF_SYS_NAME_MAX 64
#define VQEAF_SYS_MOUNT_PATH 192

typedef struct {
    char path[VQEAF_SYS_MOUNT_PATH];
    char fstype[32];
    uint64_t total_bytes;
    uint64_t free_bytes;
} vqeaf_mount_t;

typedef struct {
    char path[VQEAF_SYS_PATH_MAX];
    uint64_t size;
    uint8_t is_dir;
} vqeaf_search_hit_t;

typedef struct {
    int pid;
    char name[VQEAF_SYS_NAME_MAX];
    char state[8];
    uint64_t rss_kb;
} vqeaf_process_t;

typedef struct {
    char path[VQEAF_SYS_PATH_MAX];
    uint64_t size;
    uint8_t is_dir;
} vqeaf_cleanup_item_t;

/* 1 when the filesystem type is NTFS (ntfs, ntfs3) or a FUSE mount whose
 * options say ntfs. Pure helper, also used by the host gate. */
int vqeaf_sys_is_ntfs(const char *fstype, const char *opts);
/* 1 for a temp/log/backup name the cleanup scanner is allowed to surface. */
int vqeaf_sys_cleanup_candidate(const char *name);

/* Mounts from /proc/mounts. With ntfs_only=1 only NTFS mounts are returned.
 * Returns the count, or -errno. */
int vqeaf_sys_mounts(vqeaf_mount_t *out, int max, int ntfs_only);

/* Case-insensitive substring search under root. Symlinks are skipped.
 * `scanned` receives the number of visited entries. Returns hit count or
 * -errno when root cannot be read. */
int vqeaf_sys_search(const char *root, const char *needle,
                     vqeaf_search_hit_t *out, int max,
                     int max_depth, uint64_t *scanned);

/* Live /proc processes, sorted by pid. Returns count or -errno. */
int vqeaf_sys_processes(vqeaf_process_t *out, int max);
/* Pure permission helper used by the UI and the gate: PID <= 1 and the
 * calling process may never be signalled. */
int vqeaf_sys_signal_allowed(int pid);
/* Sends SIGTERM only; refuses PID <= 1 and the calling process. */
int vqeaf_sys_signal_pid(int pid);

/* Cleanup candidates under root (read-only). Returns count or -errno. */
int vqeaf_sys_cleanup_scan(const char *root, vqeaf_cleanup_item_t *out,
                           int max, uint64_t *total);
/* Moves at most 10 selected items into root/.qeaf-trash/<stamp>/ and writes a
 * MANIFEST.TXT. Returns moved count or -errno. Nothing is deleted. */
int vqeaf_sys_cleanup_quarantine(const char *root,
                                 const vqeaf_cleanup_item_t *items,
                                 int count, char *report, size_t cap);
/* Moves items named in a previous manifest back. Returns restored count or
 * -errno. `dir` may be NULL to use the last quarantine on this device. */
int vqeaf_sys_cleanup_restore(const char *dir, char *report, size_t cap);
const char *vqeaf_sys_quarantine_dir(void);

/* Injection points for host gates. Passing NULL restores the honest default.
 * These are never set by the device UI. */
typedef int (*vqeaf_mounts_fn)(vqeaf_mount_t *, int, int);
typedef int (*vqeaf_search_fn)(const char *, const char *,
                               vqeaf_search_hit_t *, int, int, uint64_t *);
typedef int (*vqeaf_processes_fn)(vqeaf_process_t *, int);
typedef int (*vqeaf_cleanup_scan_fn)(const char *, vqeaf_cleanup_item_t *,
                                     int, uint64_t *);

void vqeaf_sys_set_mounts_provider(vqeaf_mounts_fn fn);
void vqeaf_sys_set_search_provider(vqeaf_search_fn fn);
void vqeaf_sys_set_processes_provider(vqeaf_processes_fn fn);
void vqeaf_sys_set_cleanup_scan_provider(vqeaf_cleanup_scan_fn fn);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_SYS_TOOLS_H */
