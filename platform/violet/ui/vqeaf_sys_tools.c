/* SPDX-License-Identifier: MIT */
/* vqeaf_sys_tools.c - native system utilities for the violet shell. */

#include "vqeaf_sys_tools.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#endif

static vqeaf_mounts_fn g_mounts_provider;
static vqeaf_search_fn g_search_provider;
static vqeaf_processes_fn g_processes_provider;
static vqeaf_cleanup_scan_fn g_cleanup_provider;
static char g_quarantine[VQEAF_SYS_PATH_MAX];

void vqeaf_sys_set_mounts_provider(vqeaf_mounts_fn fn) { g_mounts_provider = fn; }
void vqeaf_sys_set_search_provider(vqeaf_search_fn fn) { g_search_provider = fn; }
void vqeaf_sys_set_processes_provider(vqeaf_processes_fn fn) { g_processes_provider = fn; }
void vqeaf_sys_set_cleanup_scan_provider(vqeaf_cleanup_scan_fn fn) { g_cleanup_provider = fn; }
const char *vqeaf_sys_quarantine_dir(void) { return g_quarantine; }

static int contains_ci(const char *hay, const char *needle)
{
    if(hay == NULL || needle == NULL || needle[0] == '\0') return 0;
    while(hay[0] != '\0') {
        size_t i = 0;
        while(needle[i] != '\0' && hay[i] != '\0' &&
              tolower((unsigned char)hay[i]) ==
              tolower((unsigned char)needle[i]))
            i++;
        if(needle[i] == '\0') return 1;
        hay++;
    }
    return 0;
}

static int ends_with_ci(const char *value, const char *suffix)
{
    size_t n, m;
    if(value == NULL || suffix == NULL) return 0;
    n = strlen(value);
    m = strlen(suffix);
    if(m == 0 || n < m) return 0;
    return contains_ci(value + n - m, suffix);
}

int vqeaf_sys_signal_allowed(int pid)
{
    if(pid <= 1) return 0;
#if defined(__linux__)
    if(pid == (int)getpid()) return 0;
#endif
    return 1;
}

int vqeaf_sys_is_ntfs(const char *fstype, const char *opts)
{
    if(fstype == NULL) return 0;
    if(contains_ci(fstype, "ntfs")) return 1;
    if((strcmp(fstype, "fuseblk") == 0 || strcmp(fstype, "fuse") == 0) &&
       opts != NULL && contains_ci(opts, "ntfs"))
        return 1;
    return 0;
}

int vqeaf_sys_cleanup_candidate(const char *name)
{
    static const char *const suffixes[] = {
        ".tmp", ".temp", ".log", ".bak", ".old", ".part", ".swp", ".cache"
    };
    static const char *const folders[] = {
        "cache", ".cache", "thumbnails", ".thumbnails", "Crash Reports"
    };
    size_t i, n;

    if(name == NULL || name[0] == '\0') return 0;
    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
       strcmp(name, ".qeaf-trash") == 0)
        return 0;
    n = strlen(name);
    if(name[n - 1] == '~') return 1;
    for(i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++)
        if(ends_with_ci(name, suffixes[i])) return 1;
    for(i = 0; i < sizeof(folders) / sizeof(folders[0]); i++)
        if(strcmp(name, folders[i]) == 0) return 1;
    return 0;
}

#if defined(__linux__)

#define VQEAF_SYS_SCAN_LIMIT 20000

int vqeaf_sys_mounts(vqeaf_mount_t *out, int max, int ntfs_only)
{
    FILE *file;
    char line[1024];
    int count = 0;

    if(g_mounts_provider != NULL)
        return g_mounts_provider(out, max, ntfs_only);
    if(out == NULL || max <= 0) return -EINVAL;
    file = fopen("/proc/mounts", "r");
    if(file == NULL) return -errno;

    while(fgets(line, sizeof(line), file) != NULL && count < max) {
        char dev[256], path[VQEAF_SYS_MOUNT_PATH], type[32], opts[512];
        struct statvfs info;

        if(sscanf(line, "%255s %191s %31s %511s", dev, path, type, opts) != 4)
            continue;
        if(ntfs_only && !vqeaf_sys_is_ntfs(type, opts)) continue;
        memset(&out[count], 0, sizeof(out[count]));
        snprintf(out[count].path, sizeof(out[count].path), "%s", path);
        snprintf(out[count].fstype, sizeof(out[count].fstype), "%s", type);
        if(statvfs(path, &info) == 0) {
            out[count].total_bytes = (uint64_t)info.f_blocks * info.f_frsize;
            out[count].free_bytes = (uint64_t)info.f_bavail * info.f_frsize;
        }
        count++;
    }
    fclose(file);
    return count;
}

static int hit_cmp(const void *a, const void *b)
{
    const vqeaf_search_hit_t *left = a;
    const vqeaf_search_hit_t *right = b;
    return strcmp(left->path, right->path);
}

static void search_dir(const char *path, const char *needle,
                       vqeaf_search_hit_t *out, int max, int *hits,
                       int depth, int max_depth, uint64_t *scanned)
{
    DIR *dir;
    struct dirent *entry;

    if(depth > max_depth || *hits >= max || *scanned >= VQEAF_SYS_SCAN_LIMIT)
        return;
    dir = opendir(path);
    if(dir == NULL) return;

    while((entry = readdir(dir)) != NULL) {
        char full[VQEAF_SYS_PATH_MAX];
        struct stat info;
        int match;

        if(strcmp(entry->d_name, ".") == 0 ||
           strcmp(entry->d_name, "..") == 0)
            continue;
        if(snprintf(full, sizeof(full), "%s/%s", path, entry->d_name) >=
           (int)sizeof(full))
            continue;
        if(lstat(full, &info) != 0) continue;
        (*scanned)++;
        /* A symlink can escape the selected volume or form a loop. */
        if(S_ISLNK(info.st_mode)) continue;
        match = contains_ci(entry->d_name, needle);
        if(match && *hits < max) {
            snprintf(out[*hits].path, sizeof(out[*hits].path), "%s", full);
            out[*hits].size = S_ISDIR(info.st_mode) ? 0 :
                              (uint64_t)info.st_size;
            out[*hits].is_dir = S_ISDIR(info.st_mode) ? 1 : 0;
            (*hits)++;
        }
        if(S_ISDIR(info.st_mode) && depth < max_depth &&
           *scanned < VQEAF_SYS_SCAN_LIMIT)
            search_dir(full, needle, out, max, hits,
                       depth + 1, max_depth, scanned);
        if(*hits >= max || *scanned >= VQEAF_SYS_SCAN_LIMIT) break;
    }
    closedir(dir);
}

int vqeaf_sys_search(const char *root, const char *needle,
                     vqeaf_search_hit_t *out, int max,
                     int max_depth, uint64_t *scanned)
{
    struct stat info;
    int hits = 0;

    if(g_search_provider != NULL)
        return g_search_provider(root, needle, out, max, max_depth, scanned);
    if(scanned != NULL) *scanned = 0;
    if(root == NULL || needle == NULL || needle[0] == '\0' ||
       out == NULL || max <= 0 || root[0] != '/')
        return -EINVAL;
    if(lstat(root, &info) != 0) return -errno;
    if(!S_ISDIR(info.st_mode)) return -ENOTDIR;
    if(max_depth < 0) max_depth = 0;
    if(max_depth > 12) max_depth = 12;

    search_dir(root, needle, out, max, &hits, 0, max_depth, scanned);
    if(hits > 1)
        qsort(out, (size_t)hits, sizeof(out[0]), hit_cmp);
    return hits;
}

static int proc_cmp(const void *a, const void *b)
{
    const vqeaf_process_t *left = a;
    const vqeaf_process_t *right = b;
    return left->pid - right->pid;
}

int vqeaf_sys_processes(vqeaf_process_t *out, int max)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    if(g_processes_provider != NULL) return g_processes_provider(out, max);
    if(out == NULL || max <= 0) return -EINVAL;
    dir = opendir("/proc");
    if(dir == NULL) return -errno;

    while((entry = readdir(dir)) != NULL && count < max) {
        char path[128], line[256];
        FILE *file;
        vqeaf_process_t item;
        size_t i, n;

        if(entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        memset(&item, 0, sizeof(item));
        item.pid = atoi(entry->d_name);
        if(item.pid <= 0) continue;

        snprintf(path, sizeof(path), "/proc/%d/comm", item.pid);
        file = fopen(path, "r");
        if(file == NULL) continue;
        if(fgets(line, sizeof(line), file) != NULL) {
            n = strlen(line);
            while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
                line[--n] = '\0';
            snprintf(item.name, sizeof(item.name), "%s", line);
        }
        fclose(file);

        snprintf(path, sizeof(path), "/proc/%d/status", item.pid);
        file = fopen(path, "r");
        if(file != NULL) {
            while(fgets(line, sizeof(line), file) != NULL) {
                if(strncmp(line, "State:", 6) == 0)
                    sscanf(line, "State: %7s", item.state);
                else if(strncmp(line, "VmRSS:", 6) == 0) {
                    unsigned long long kb = 0;
                    if(sscanf(line, "VmRSS: %llu", &kb) == 1)
                        item.rss_kb = (uint64_t)kb;
                }
            }
            fclose(file);
        }
        if(item.name[0] == '\0')
            snprintf(item.name, sizeof(item.name), "pid-%d", item.pid);
        if(item.state[0] == '\0') snprintf(item.state, sizeof(item.state), "?");
        out[count++] = item;
        (void)i;
    }
    closedir(dir);
    if(count > 1) qsort(out, (size_t)count, sizeof(out[0]), proc_cmp);
    return count;
}

int vqeaf_sys_signal_pid(int pid)
{
    if(!vqeaf_sys_signal_allowed(pid)) return -EPERM;
    if(kill(pid, SIGTERM) != 0) return -errno;
    return 0;
}

static int cleanup_cmp(const void *a, const void *b)
{
    const vqeaf_cleanup_item_t *left = a;
    const vqeaf_cleanup_item_t *right = b;
    return strcmp(left->path, right->path);
}

static void cleanup_dir(const char *path, vqeaf_cleanup_item_t *out, int max,
                        int *hits, int depth, int max_depth,
                        uint64_t *scanned, uint64_t *total)
{
    DIR *dir;
    struct dirent *entry;

    if(depth > max_depth || *hits >= max || *scanned >= VQEAF_SYS_SCAN_LIMIT)
        return;
    dir = opendir(path);
    if(dir == NULL) return;

    while((entry = readdir(dir)) != NULL) {
        char full[VQEAF_SYS_PATH_MAX];
        struct stat info;

        if(strcmp(entry->d_name, ".") == 0 ||
           strcmp(entry->d_name, "..") == 0 ||
           strcmp(entry->d_name, ".qeaf-trash") == 0)
            continue;
        if(snprintf(full, sizeof(full), "%s/%s", path, entry->d_name) >=
           (int)sizeof(full))
            continue;
        if(lstat(full, &info) != 0) continue;
        (*scanned)++;
        if(S_ISLNK(info.st_mode)) continue;
        if(vqeaf_sys_cleanup_candidate(entry->d_name)) {
            if(*hits < max) {
                snprintf(out[*hits].path, sizeof(out[*hits].path), "%s", full);
                out[*hits].size = S_ISDIR(info.st_mode) ? 0 :
                                  (uint64_t)info.st_size;
                out[*hits].is_dir = S_ISDIR(info.st_mode) ? 1 : 0;
                (*hits)++;
                if(!S_ISDIR(info.st_mode)) *total += (uint64_t)info.st_size;
            }
            /* Do not walk a candidate cache twice: its own size is reported,
             * and its children stay untouched until the user confirms. */
            continue;
        }
        if(S_ISDIR(info.st_mode) && depth < max_depth &&
           *scanned < VQEAF_SYS_SCAN_LIMIT)
            cleanup_dir(full, out, max, hits, depth + 1, max_depth,
                        scanned, total);
        if(*hits >= max || *scanned >= VQEAF_SYS_SCAN_LIMIT) break;
    }
    closedir(dir);
}

int vqeaf_sys_cleanup_scan(const char *root, vqeaf_cleanup_item_t *out,
                           int max, uint64_t *total)
{
    struct stat info;
    int hits = 0;
    uint64_t scanned = 0, bytes = 0;

    if(g_cleanup_provider != NULL)
        return g_cleanup_provider(root, out, max, total);
    if(total != NULL) *total = 0;
    if(root == NULL || out == NULL || max <= 0 || root[0] != '/')
        return -EINVAL;
    if(lstat(root, &info) != 0) return -errno;
    if(!S_ISDIR(info.st_mode)) return -ENOTDIR;

    cleanup_dir(root, out, max, &hits, 0, 8, &scanned, &bytes);
    if(hits > 1) qsort(out, (size_t)hits, sizeof(out[0]), cleanup_cmp);
    if(total != NULL) *total = bytes;
    return hits;
}

static int path_under_root(const char *root, const char *path)
{
    size_t n;
    if(root == NULL || path == NULL || root[0] != '/' || path[0] != '/')
        return 0;
    n = strlen(root);
    while(n > 1 && root[n - 1] == '/') n--;
    if(strlen(path) <= n || strncmp(path, root, n) != 0 || path[n] != '/')
        return 0;
    if(contains_ci(path, "/.qeaf-trash/")) return 0;
    return 1;
}

int vqeaf_sys_cleanup_quarantine(const char *root,
                                 const vqeaf_cleanup_item_t *items,
                                 int count, char *report, size_t cap)
{
    char dir[VQEAF_SYS_PATH_MAX], trash[VQEAF_SYS_PATH_MAX];
    char manifest[VQEAF_SYS_PATH_MAX];
    static unsigned stamp;
    size_t n;
    int moved = 0, failed = 0;
    FILE *file;

    if(report != NULL && cap > 0) report[0] = '\0';
    if(root == NULL || items == NULL || count <= 0 || count > 10)
        return -EINVAL;
    n = strlen(root);
    while(n > 1 && root[n - 1] == '/') n--;
    if(snprintf(trash, sizeof(trash), "%.*s/.qeaf-trash", (int)n, root) >=
       (int)sizeof(trash))
        return -ENAMETOOLONG;
    if(mkdir(trash, 0700) != 0 && errno != EEXIST) return -errno;
    if(snprintf(dir, sizeof(dir), "%s/%lld-%d-%u", trash,
                (long long)time(NULL), (int)getpid(), stamp++) >=
       (int)sizeof(dir))
        return -ENAMETOOLONG;
    if(mkdir(dir, 0700) != 0) return -errno;

    snprintf(manifest, sizeof(manifest), "%s/MANIFEST.TXT", dir);
    file = fopen(manifest, "w");
    if(file == NULL) return -errno;

    for(int i = 0; i < count; i++) {
        const char *base;
        char dest[VQEAF_SYS_PATH_MAX];

        if(!path_under_root(root, items[i].path)) { failed++; continue; }
        base = strrchr(items[i].path, '/');
        base = base == NULL ? items[i].path : base + 1;
        if(snprintf(dest, sizeof(dest), "%s/%02d_%s", dir, i, base) >=
           (int)sizeof(dest)) { failed++; continue; }
        if(rename(items[i].path, dest) != 0) { failed++; continue; }
        fprintf(file, "%s\t%s\n", dest, items[i].path);
        moved++;
    }
    fclose(file);
    if(moved > 0)
        snprintf(g_quarantine, sizeof(g_quarantine), "%s", dir);
    if(report != NULL && cap > 0)
        snprintf(report, cap,
                 "Đã chuyển %d mục vào %s; không xoá vĩnh viễn. Lỗi: %d.",
                 moved, moved > 0 ? dir : "(không có)", failed);
    return moved;
}

int vqeaf_sys_cleanup_restore(const char *dir, char *report, size_t cap)
{
    char path[VQEAF_SYS_PATH_MAX], line[VQEAF_SYS_PATH_MAX];
    struct stat info;
    FILE *file;
    int restored = 0, skipped = 0;

    if(report != NULL && cap > 0) report[0] = '\0';
    if(dir == NULL) dir = g_quarantine;
    if(dir == NULL || dir[0] == '\0') return -ENOENT;
    snprintf(path, sizeof(path), "%s/MANIFEST.TXT", dir);
    file = fopen(path, "r");
    if(file == NULL) return -errno;

    while(fgets(line, sizeof(line), file) != NULL) {
        char *tab, *original;
        size_t n = strlen(line);
        while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        tab = strchr(line, '\t');
        if(tab == NULL) continue;
        *tab = '\0';
        original = tab + 1;
        if(lstat(original, &info) == 0) { skipped++; continue; }
        if(rename(line, original) == 0) restored++;
        else skipped++;
    }
    fclose(file);
    if(restored > 0 && strcmp(dir, g_quarantine) == 0)
        g_quarantine[0] = '\0';
    if(report != NULL && cap > 0)
        snprintf(report, cap, "Khôi phục %d mục; bỏ qua %d.", restored, skipped);
    return restored;
}

#else /* host render: no device filesystem access */

int vqeaf_sys_mounts(vqeaf_mount_t *out, int max, int ntfs_only)
{
    if(g_mounts_provider != NULL)
        return g_mounts_provider(out, max, ntfs_only);
    (void)out; (void)max; (void)ntfs_only;
    /* No host mount is silently presented as a device volume. */
    return 0;
}

int vqeaf_sys_search(const char *root, const char *needle,
                     vqeaf_search_hit_t *out, int max,
                     int max_depth, uint64_t *scanned)
{
    if(g_search_provider != NULL)
        return g_search_provider(root, needle, out, max, max_depth, scanned);
    (void)root; (void)needle; (void)out; (void)max;
    (void)max_depth; (void)scanned;
    return -ENOSYS;
}

int vqeaf_sys_processes(vqeaf_process_t *out, int max)
{
    if(g_processes_provider != NULL) return g_processes_provider(out, max);
    (void)out; (void)max;
    return -ENOSYS;
}

int vqeaf_sys_signal_pid(int pid)
{
    (void)pid;
    return -ENOSYS;
}

int vqeaf_sys_cleanup_scan(const char *root, vqeaf_cleanup_item_t *out,
                           int max, uint64_t *total)
{
    if(g_cleanup_provider != NULL)
        return g_cleanup_provider(root, out, max, total);
    (void)root; (void)out; (void)max; (void)total;
    return -ENOSYS;
}

int vqeaf_sys_cleanup_quarantine(const char *root,
                                 const vqeaf_cleanup_item_t *items,
                                 int count, char *report, size_t cap)
{
    (void)root; (void)items; (void)count;
    if(report != NULL && cap > 0)
        snprintf(report, cap,
                 "Máy chủ render không có thùng rác thiết bị; không có tệp nào bị di chuyển.");
    return -ENOSYS;
}

int vqeaf_sys_cleanup_restore(const char *dir, char *report, size_t cap)
{
    (void)dir;
    if(report != NULL && cap > 0)
        snprintf(report, cap,
                 "Máy chủ render không có thùng rác thiết bị; không có tệp nào được khôi phục.");
    return -ENOSYS;
}

#endif /* __linux__ */
