/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_files.h - read-only directory listing for Files. */
#ifndef VQEAF_FILES_H
#define VQEAF_FILES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_FILE_NAME 128

typedef struct {
    char name[VQEAF_FILE_NAME];
    uint32_t size;
    uint8_t is_dir;
} vqeaf_file_t;

typedef enum {
    VQEAF_FILE_TEXT,
    VQEAF_FILE_JSON,
    VQEAF_FILE_MARKDOWN,
    VQEAF_FILE_LOG,
    VQEAF_FILE_RAW,
    VQEAF_FILE_IMAGE,
    VQEAF_FILE_AUDIO,
    VQEAF_FILE_VIDEO,
    VQEAF_FILE_PDF,
    VQEAF_FILE_ARCHIVE,
    VQEAF_FILE_QEAPP,
    VQEAF_FILE_BINARY,
    VQEAF_FILE_UNKNOWN,
} vqeaf_file_kind_t;

/* Extension-based routing used by the file viewer. Never claims a codec is
 * present: image/audio/video/pdf are identified, not decoded here. */
vqeaf_file_kind_t vqeaf_files_kind(const char *name);
const char *vqeaf_files_kind_name(vqeaf_file_kind_t kind);
const char *vqeaf_files_kind_key(vqeaf_file_kind_t kind);

/* Reads up to cap bytes of an absolute file path. Returns the byte count or
 * -errno. */
int vqeaf_files_read(const char *path, uint8_t *out, size_t cap);

/* Optional read/list backend for virtual volumes (the QEAFRESCUE RAM LUN).
 * A backend returning a negative errno lets the normal filesystem path run. */
typedef int (*vqeaf_files_read_fn)(const char *path, uint8_t *out, size_t cap);
typedef int (*vqeaf_files_list_fn)(const char *path, vqeaf_file_t *out, int max);
void vqeaf_files_set_backends(vqeaf_files_read_fn read_fn,
                              vqeaf_files_list_fn list_fn);

/* Lists an absolute directory path. ".." is included for non-root paths.
 * Returns the count, or -errno when the directory cannot be opened. */
int vqeaf_files_list(const char *path, vqeaf_file_t *out, int max);

/* Joins `name` onto `base` into `out`; returns 0, or -1 when it would not
 * fit or the name tries to escape the root. */
int vqeaf_files_join(const char *base, const char *name, char *out, size_t cap);

/* Parent of an absolute path; "/" stays "/". */
void vqeaf_files_parent(const char *path, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_FILES_H */
