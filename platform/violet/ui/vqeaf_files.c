/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_files.c - read-only file manager provider. */
#define _GNU_SOURCE

#include "vqeaf_files.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __linux__
#include <dirent.h>
#endif

static int has_ext(const char *name, const char *ext)
{
    size_t length = strlen(name);
    size_t elen = strlen(ext);
    if(length <= elen + 1) return 0;
    if(name[length - elen - 1] != '.') return 0;
    for(size_t i = 0; i < elen; i++) {
        char a = name[length - elen + i];
        char b = ext[i];
        if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if(b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if(a != b) return 0;
    }
    return 1;
}

vqeaf_file_kind_t vqeaf_files_kind(const char *name)
{
    static const struct { const char *ext; vqeaf_file_kind_t kind; } map[] = {
        { "json", VQEAF_FILE_JSON }, { "md", VQEAF_FILE_MARKDOWN },
        { "markdown", VQEAF_FILE_MARKDOWN }, { "log", VQEAF_FILE_LOG },
        { "txt", VQEAF_FILE_TEXT }, { "text", VQEAF_FILE_TEXT },
        { "ini", VQEAF_FILE_TEXT }, { "cfg", VQEAF_FILE_TEXT },
        { "conf", VQEAF_FILE_TEXT }, { "yaml", VQEAF_FILE_TEXT },
        { "yml", VQEAF_FILE_TEXT }, { "csv", VQEAF_FILE_TEXT },
        { "xml", VQEAF_FILE_TEXT }, { "sh", VQEAF_FILE_TEXT },
        { "py", VQEAF_FILE_TEXT }, { "c", VQEAF_FILE_TEXT },
        { "h", VQEAF_FILE_TEXT }, { "qe", VQEAF_FILE_TEXT },
        { "git", VQEAF_FILE_TEXT }, { "raw", VQEAF_FILE_RAW },
        { "png", VQEAF_FILE_IMAGE }, { "jpg", VQEAF_FILE_IMAGE },
        { "jpeg", VQEAF_FILE_IMAGE }, { "bmp", VQEAF_FILE_IMAGE },
        { "gif", VQEAF_FILE_IMAGE }, { "webp", VQEAF_FILE_IMAGE },
        { "mp3", VQEAF_FILE_AUDIO }, { "wav", VQEAF_FILE_AUDIO },
        { "ogg", VQEAF_FILE_AUDIO }, { "flac", VQEAF_FILE_AUDIO },
        { "m4a", VQEAF_FILE_AUDIO }, { "mp4", VQEAF_FILE_VIDEO },
        { "qvf", VQEAF_FILE_VIDEO },
        { "mkv", VQEAF_FILE_VIDEO }, { "webm", VQEAF_FILE_VIDEO },
        { "avi", VQEAF_FILE_VIDEO }, { "mov", VQEAF_FILE_VIDEO },
        { "pdf", VQEAF_FILE_PDF }, { "zip", VQEAF_FILE_ARCHIVE },
        { "qeapp", VQEAF_FILE_QEAPP }, { "bin", VQEAF_FILE_BINARY },
        { "elf", VQEAF_FILE_BINARY }, { "so", VQEAF_FILE_BINARY },
        { "img", VQEAF_FILE_BINARY },
    };

    if(name == NULL) return VQEAF_FILE_UNKNOWN;
    for(size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if(has_ext(name, map[i].ext)) return map[i].kind;
    if(strcmp(name, "init") == 0) return VQEAF_FILE_BINARY;
    return VQEAF_FILE_UNKNOWN;
}

const char *vqeaf_files_kind_name(vqeaf_file_kind_t kind)
{
    switch(kind) {
    case VQEAF_FILE_TEXT:     return "Văn bản";
    case VQEAF_FILE_JSON:     return "JSON";
    case VQEAF_FILE_MARKDOWN: return "Markdown";
    case VQEAF_FILE_LOG:      return "Log";
    case VQEAF_FILE_RAW:      return "Raw / hex";
    case VQEAF_FILE_IMAGE:    return "Ảnh";
    case VQEAF_FILE_AUDIO:    return "Âm thanh";
    case VQEAF_FILE_VIDEO:    return "Video";
    case VQEAF_FILE_PDF:      return "PDF";
    case VQEAF_FILE_ARCHIVE:  return "Kho lưu trữ";
    case VQEAF_FILE_QEAPP:    return "QEAPP";
    case VQEAF_FILE_BINARY:   return "Nhị phân";
    default:                  return "Không rõ";
    }
}

const char *vqeaf_files_kind_key(vqeaf_file_kind_t kind)
{
    switch(kind) {
    case VQEAF_FILE_TEXT:     return "text";
    case VQEAF_FILE_JSON:     return "json";
    case VQEAF_FILE_MARKDOWN: return "markdown";
    case VQEAF_FILE_LOG:      return "log";
    case VQEAF_FILE_RAW:      return "raw";
    case VQEAF_FILE_IMAGE:    return "png/jpg/bmp/gif/webp";
    case VQEAF_FILE_AUDIO:    return "mp3/wav/ogg/flac/m4a";
    case VQEAF_FILE_VIDEO:    return "mp4/mkv/webm/avi/mov/qvf";
    case VQEAF_FILE_PDF:      return "pdf";
    case VQEAF_FILE_ARCHIVE:  return "zip";
    case VQEAF_FILE_QEAPP:    return "qeapp";
    case VQEAF_FILE_BINARY:   return "bin/elf/so/img";
    default:                  return "?";
    }
}

#ifdef __linux__
int vqeaf_files_read(const char *path, uint8_t *out, size_t cap)
{
    FILE *file;
    size_t count;

    if(path == NULL || out == NULL || cap == 0) return -EINVAL;
    file = fopen(path, "rb");
    if(file == NULL) return -errno;
    count = fread(out, 1, cap, file);
    if(ferror(file)) {
        fclose(file);
        return -EIO;
    }
    fclose(file);
    return (int)count;
}
#else
#include "vqeaf_host_media.h"

int vqeaf_files_read(const char *path, uint8_t *out, size_t cap)
{
    const char *sample = NULL;
    size_t length;

    if(path == NULL || out == NULL || cap == 0) return -EINVAL;
    for(size_t i = 0; i < sizeof(host_media) / sizeof(host_media[0]); i++) {
        if(strcmp(path, host_media[i].path) == 0) {
            length = host_media[i].len < cap ? host_media[i].len : cap;
            memcpy(out, host_media[i].data, length);
            return (int)length;
        }
    }
    if(strcmp(path, "/VERSION") == 0) sample = "VQEAFIVELS OS 0.3.0\n";
    else if(strcmp(path, "/etc/hostname") == 0) sample = "violet\n";
    else if(strcmp(path, "/etc/qeaf/radio-firmware-status.txt") == 0)
        sample = "WCN3990 firmware: absent\n/blobs not shipped.\n";
    else if(strcmp(path, "/run/qeaf-sync/last-pull.txt") == 0)
        sample = "last pull: SCREEN.RAW\nstatus: ok\n";
    if(sample == NULL) return -ENOENT;
    length = strlen(sample);
    if(length > cap) length = cap;
    memcpy(out, sample, length);
    return (int)length;
}
#endif

int vqeaf_files_join(const char *base, const char *name, char *out, size_t cap)
{
    int written;

    if(base == NULL || name == NULL || out == NULL || cap == 0) return -1;
    if(base[0] != '/') return -1;
    if(strcmp(name, "..") == 0) {
        vqeaf_files_parent(base, out, cap);
        return 0;
    }
    if(name[0] == '/' || strstr(name, "../") != NULL ||
       strcmp(name, ".") == 0)
        return -1;
    if(strcmp(base, "/") == 0)
        written = snprintf(out, cap, "/%s", name);
    else
        written = snprintf(out, cap, "%s/%s", base, name);
    return written > 0 && (size_t)written < cap ? 0 : -1;
}

void vqeaf_files_parent(const char *path, char *out, size_t cap)
{
    size_t length;
    char *slash;

    if(path == NULL || out == NULL || cap == 0) return;
    if(path[0] != '/' || strcmp(path, "/") == 0) {
        snprintf(out, cap, "/");
        return;
    }
    snprintf(out, cap, "%s", path);
    length = strlen(out);
    slash = strrchr(out, '/');
    if(slash == NULL || slash == out)
        snprintf(out, cap, "/");
    else
        *slash = '\0';
    (void)length;
}

#ifdef __linux__

static int entry_cmp(const void *a, const void *b)
{
    const vqeaf_file_t *left = a;
    const vqeaf_file_t *right = b;

    if(strcmp(left->name, "..") == 0) return -1;
    if(strcmp(right->name, "..") == 0) return 1;
    if(left->is_dir != right->is_dir) return right->is_dir - left->is_dir;
    return strcmp(left->name, right->name);
}

int vqeaf_files_list(const char *path, vqeaf_file_t *out, int max)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    if(path == NULL || out == NULL || max <= 0 || path[0] != '/')
        return -EINVAL;
    dir = opendir(path);
    if(dir == NULL) return -errno;

    if(strcmp(path, "/") != 0 && count < max) {
        snprintf(out[count].name, sizeof(out[count].name), "..");
        out[count].size = 0;
        out[count].is_dir = 1;
        count++;
    }

    while((entry = readdir(dir)) != NULL && count < max) {
        char full[512];
        struct stat info;

        if(strcmp(entry->d_name, ".") == 0 ||
           strcmp(entry->d_name, "..") == 0)
            continue;
        if(snprintf(full, sizeof(full), "%s/%s", path, entry->d_name) >=
           (int)sizeof(full))
            continue;
        if(lstat(full, &info) != 0) continue;
        snprintf(out[count].name, sizeof(out[count].name), "%s",
                 entry->d_name);
        out[count].is_dir = S_ISDIR(info.st_mode) ? 1 : 0;
        out[count].size = out[count].is_dir ? 0 : (uint32_t)info.st_size;
        count++;
    }
    closedir(dir);

    if(count > 1)
        qsort(out, (size_t)count, sizeof(out[0]), entry_cmp);
    return count;
}

#else /* host render harness: deterministic tree, no host filesystem access */

typedef struct {
    const char *path;
    const vqeaf_file_t *entries;
    int count;
} fake_dir_t;

static const vqeaf_file_t root_entries[] = {
    { "bin", 0, 1 }, { "dev", 0, 1 }, { "etc", 0, 1 },
    { "media", 0, 1 }, { "proc", 0, 1 }, { "run", 0, 1 },
    { "sys", 0, 1 }, { "tmp", 0, 1 }, { "init", 6536368u, 0 },
    { "VERSION", 6u, 0 },
};
static const vqeaf_file_t etc_entries[] = {
    { "qeaf", 0, 1 }, { "hostname", 8u, 0 },
};
static const vqeaf_file_t qeaf_entries[] = {
    { "radio-firmware-status.txt", 0u, 0 },
};
static const vqeaf_file_t run_entries[] = {
    { "qeaf-lun.img", 33554432u, 0 }, { "qeaf-sync", 0, 1 },
};
static const vqeaf_file_t sync_entries[] = {
    { "last-pull.txt", 42u, 0 },
};
static const vqeaf_file_t media_entries[] = {
    { "sub", 0, 1 }, { "anim.gif", 44u, 0 }, { "clip.mp4", 236u, 0 },
    { "clip.qvf", 1469u, 0 },
    { "doc.pdf", 300u, 0 }, { "logo.png", 1174u, 0 }, { "note.txt", 39u, 0 },
    { "pattern.bmp", 822u, 0 }, { "photo.jpg", 938u, 0 }, { "song.mp3", 278u, 0 },
    { "song.lrc", 138u, 0 },
    { "tone.wav", 16044u, 0 },
};
static const vqeaf_file_t media_sub_entries[] = {
    { "readme.txt", 39u, 0 },
};

static const fake_dir_t fake_dirs[] = {
    { "/", root_entries, (int)(sizeof(root_entries) / sizeof(root_entries[0])) },
    { "/etc", etc_entries, (int)(sizeof(etc_entries) / sizeof(etc_entries[0])) },
    { "/etc/qeaf", qeaf_entries,
      (int)(sizeof(qeaf_entries) / sizeof(qeaf_entries[0])) },
    { "/run", run_entries, (int)(sizeof(run_entries) / sizeof(run_entries[0])) },
    { "/run/qeaf-sync", sync_entries,
      (int)(sizeof(sync_entries) / sizeof(sync_entries[0])) },
    { "/media", media_entries,
      (int)(sizeof(media_entries) / sizeof(media_entries[0])) },
    { "/media/sub", media_sub_entries,
      (int)(sizeof(media_sub_entries) / sizeof(media_sub_entries[0])) },
};

int vqeaf_files_list(const char *path, vqeaf_file_t *out, int max)
{
    const fake_dir_t *dir = NULL;
    int count;

    if(path == NULL || out == NULL || max <= 0 || path[0] != '/')
        return -EINVAL;
    for(size_t i = 0; i < sizeof(fake_dirs) / sizeof(fake_dirs[0]); i++) {
        if(strcmp(fake_dirs[i].path, path) == 0) {
            dir = &fake_dirs[i];
            break;
        }
    }
    if(dir == NULL) return -ENOENT;

    count = 0;
    if(strcmp(path, "/") != 0 && count < max) {
        snprintf(out[count].name, sizeof(out[count].name), "..");
        out[count].size = 0u;
        out[count].is_dir = 1;
        count++;
    }
    for(int i = 0; i < dir->count && count < max; i++) out[count++] = dir->entries[i];
    return count;
}

#endif /* __linux__ */
