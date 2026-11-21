/* qeaf_code_store.h - document store with a storage HAL.
 *
 * The MRE original scans <drive>:\@codeeditor and keeps 14 files of up to
 * 1280 bytes. The port keeps the same limits but calls a small HAL instead of
 * vm_file_* so the host tests can use memory and the OS can bind real files.
 */
#ifndef QEAF_CODE_STORE_H
#define QEAF_CODE_STORE_H

#include "qeaf_code_types.h"

typedef struct {
    /* Fills names[0..n) with leaf names inside dir; returns count or -1. */
    int (*list)(void *ctx, const char *dir,
                char names[][QEAF_NAME_MAX], int max);
    /* Reads a file; returns bytes read (NUL terminated) or -1. */
    int (*read)(void *ctx, const char *path, char *out, int cap);
    /* Writes text[0..len); returns 0 or -1. */
    int (*write)(void *ctx, const char *path, const char *text, int len);
    int (*mkdir)(void *ctx, const char *path);
    int (*remove)(void *ctx, const char *path);
} qeaf_store_io_t;

typedef struct {
    char path[QEAF_PATH_MAX];
    char name[QEAF_NAME_MAX];
    int ext;
    int trunc;
    int size;
} qeaf_doc_meta_t;

typedef struct {
    char text[QEAF_CODE_LEN];
    int len;
    qeaf_doc_meta_t meta;
    int used;
} qeaf_doc_t;

typedef struct {
    qeaf_doc_t docs[QEAF_DOC_MAX];
    int count;
    qeaf_store_io_t io;
    void *ctx;
    char dir[QEAF_PATH_MAX];
    int readonly;          /* 1 when the HAL has no write callback */
    int auto_indent;
    int indent_width;
    int dark_mode;
    int syntax_hl;
    int line_nums;
} qeaf_store_t;

void qeaf_store_init(qeaf_store_t *s, const qeaf_store_io_t *io, void *ctx,
                     const char *dir);
int qeaf_store_rescan(qeaf_store_t *s);
int qeaf_store_count(const qeaf_store_t *s);
const qeaf_doc_t *qeaf_store_doc(const qeaf_store_t *s, int index);

int qeaf_store_add(qeaf_store_t *s, const char *base, int ext);
int qeaf_store_update(qeaf_store_t *s, int index, const char *text);
int qeaf_store_rename(qeaf_store_t *s, int index, const char *base, int ext);
int qeaf_store_delete(qeaf_store_t *s, int index);
int qeaf_store_duplicate(qeaf_store_t *s, int index);
int qeaf_store_export(const qeaf_store_t *s, int index, const char *dir,
                      char *out, int cap);

const char *qeaf_ext_name(int ext);
int qeaf_ext_from_name(const char *name);
int qeaf_store_lang_for(const qeaf_doc_t *doc);

#endif /* QEAF_CODE_STORE_H */
