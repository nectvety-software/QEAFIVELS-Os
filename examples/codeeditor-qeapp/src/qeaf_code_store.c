#include "qeaf_code_store.h"
#include "qeaf_code_util.h"

#include <string.h>

static const char ext_names[QEAF_EXT_N][8] = {
    ".C", ".H", ".TXT", ".HTML", ".CSS", ".JS", ".PY"
};

const char *qeaf_ext_name(int ext)
{
    if(ext < 0 || ext >= QEAF_EXT_N) return ext_names[0];
    return ext_names[ext];
}

int qeaf_ext_from_name(const char *name)
{
    int i, n = qeaf_strlen(name);
    for(i = 1; i < QEAF_EXT_N; i++) {
        int el = qeaf_strlen(ext_names[i]);
        if(n >= el) {
            int k, same = 1;
            for(k = 0; k < el; k++) {
                char a = name[n - el + k];
                char b = ext_names[i][k];
                if(a >= 'a' && a <= 'z') a = (char)(a - 32);
                if(a != b) { same = 0; break; }
            }
            if(same) return i;
        }
    }
    return QEAF_EXT_C;
}

int qeaf_store_lang_for(const qeaf_doc_t *doc)
{
    if(doc == NULL) return QEAF_LANG_NONE;
    switch(doc->meta.ext) {
    case QEAF_EXT_C:
    case QEAF_EXT_H:   return QEAF_LANG_C;
    case QEAF_EXT_HTML:return QEAF_LANG_HTML;
    case QEAF_EXT_CSS: return QEAF_LANG_CSS;
    case QEAF_EXT_JS:  return QEAF_LANG_JS;
    case QEAF_EXT_PY:  return QEAF_LANG_PY;
    default:           return QEAF_LANG_NONE;
    }
}

static int join_path(const char *dir, const char *name, char *out, int cap)
{
    int n = 0;
    if(cap <= 0) return -1;
    if(dir != NULL && dir[0] != '\0') {
        while(dir[n] != '\0' && n < cap - 1) {
            out[n] = dir[n];
            n++;
        }
        if(n < cap - 1) out[n++] = '/';
    }
    {
        int i = 0;
        while(name[i] != '\0' && n < cap - 1) out[n++] = name[i++];
    }
    out[n] = '\0';
    return 0;
}

static int doc_name_taken(const qeaf_store_t *s, const char *name, int skip)
{
    int i;
    for(i = 0; i < s->count; i++) {
        if(i == skip) continue;
        if(strcmp(s->docs[i].meta.name, name) == 0) return 1;
    }
    return 0;
}

static void build_name(const char *base, int ext, int suffix, char *out,
                       int cap)
{
    char clean[QEAF_NAME_MAX];
    int n = 0, i = 0;

    while(base != NULL && base[i] != '\0' && n < QEAF_BASE_MAX) {
        char c = base[i++];
        if(c >= 'a' && c <= 'z') c = (char)(c - 32);
        if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
             c == '-'))
            c = '_';
        clean[n++] = c;
    }
    if(n == 0) {
        clean[n++] = 'U';
        clean[n++] = 'N';
        clean[n++] = 'T';
        clean[n++] = 'I';
        clean[n++] = 'T';
        clean[n++] = 'L';
        clean[n++] = 'E';
        clean[n++] = 'D';
    }
    clean[n] = '\0';
    if(suffix > 0) {
        char num[4];
        int p = qeaf_int_str(suffix, num);
        if(n + p > QEAF_BASE_MAX) n = QEAF_BASE_MAX - p;
        clean[n] = '\0';
        memcpy(clean + n, num, (size_t)p);
        n += p;
        clean[n] = '\0';
    }
    qeaf_copy_trunc(out, clean, cap);
    {
        int used = qeaf_strlen(out);
        qeaf_copy_trunc(out + used, qeaf_ext_name(ext), cap - used);
    }
}

void qeaf_store_init(qeaf_store_t *s, const qeaf_store_io_t *io, void *ctx,
                     const char *dir)
{
    memset(s, 0, sizeof(*s));
    if(io != NULL) s->io = *io;
    s->ctx = ctx;
    qeaf_copy_trunc(s->dir, dir != NULL ? dir : "", QEAF_PATH_MAX);
    /* Without a write callback every mutation must fail; the UI then reports
     * a real refusal instead of pretending a file was created or deleted. */
    s->readonly = (io == NULL || io->write == NULL) ? 1 : 0;
    s->indent_width = 4;
    s->auto_indent = 1;
    s->syntax_hl = 1;
    s->line_nums = 1;
}

int qeaf_store_count(const qeaf_store_t *s)
{
    return s->count;
}

const qeaf_doc_t *qeaf_store_doc(const qeaf_store_t *s, int index)
{
    if(index < 0 || index >= s->count) return NULL;
    return &s->docs[index];
}

static int load_one(qeaf_store_t *s, const char *name)
{
    qeaf_doc_t *doc;
    char path[QEAF_PATH_MAX];
    int n;

    if(s->count >= QEAF_DOC_MAX) return -1;
    if(s->io.read == NULL) return -1;
    join_path(s->dir, name, path, QEAF_PATH_MAX);
    doc = &s->docs[s->count];
    memset(doc, 0, sizeof(*doc));
    n = s->io.read(s->ctx, path, doc->text, QEAF_CODE_LEN);
    if(n < 0) return -1;
    if(n > QEAF_CODE_LEN - 1) n = QEAF_CODE_LEN - 1;
    doc->text[n] = '\0';
    doc->len = n;
    doc->meta.size = n;
    doc->meta.trunc = (n >= QEAF_CODE_LEN - 1) ? 1 : 0;
    doc->meta.ext = qeaf_ext_from_name(name);
    qeaf_copy_trunc(doc->meta.name, name, QEAF_NAME_MAX);
    qeaf_copy_trunc(doc->meta.path, path, QEAF_PATH_MAX);
    doc->used = 1;
    s->count++;
    return 0;
}

int qeaf_store_rescan(qeaf_store_t *s)
{
    char names[QEAF_DOC_MAX][QEAF_NAME_MAX];
    int n, i;

    s->count = 0;
    if(s->io.list == NULL) return 0;
    n = s->io.list(s->ctx, s->dir, names, QEAF_DOC_MAX);
    if(n < 0) return -1;
    if(n > QEAF_DOC_MAX) n = QEAF_DOC_MAX;
    for(i = 0; i < n; i++) {
        if(names[i][0] == '\0') continue;
        load_one(s, names[i]);
    }
    return s->count;
}

int qeaf_store_add(qeaf_store_t *s, const char *base, int ext)
{
    char name[QEAF_NAME_MAX];
    char path[QEAF_PATH_MAX];
    qeaf_doc_t *doc;
    int suffix = 0;

    if(s->readonly) return -1;
    if(s->count >= QEAF_DOC_MAX) return -1;
    do {
        build_name(base, ext, suffix, name, QEAF_NAME_MAX);
        suffix++;
    } while(doc_name_taken(s, name, -1) && suffix < 10);
    if(doc_name_taken(s, name, -1)) return -1;

    doc = &s->docs[s->count];
    memset(doc, 0, sizeof(*doc));
    join_path(s->dir, name, path, QEAF_PATH_MAX);
    qeaf_copy_trunc(doc->meta.name, name, QEAF_NAME_MAX);
    qeaf_copy_trunc(doc->meta.path, path, QEAF_PATH_MAX);
    doc->meta.ext = ext;
    doc->used = 1;
    if(s->io.write != NULL &&
       s->io.write(s->ctx, path, "", 0) != 0) {
        memset(doc, 0, sizeof(*doc));
        return -1;
    }
    s->count++;
    return s->count - 1;
}

int qeaf_store_update(qeaf_store_t *s, int index, const char *text)
{
    qeaf_doc_t *doc;
    int n;
    if(index < 0 || index >= s->count) return -1;
    if(s->readonly) return -1;
    doc = &s->docs[index];
    if(doc->meta.trunc) return -1;
    n = qeaf_copy_trunc(doc->text, text, QEAF_CODE_LEN);
    doc->len = n;
    doc->meta.size = n;
    if(s->io.write != NULL &&
       s->io.write(s->ctx, doc->meta.path, doc->text, n) != 0)
        return -1;
    return 0;
}

int qeaf_store_rename(qeaf_store_t *s, int index, const char *base, int ext)
{
    char name[QEAF_NAME_MAX];
    char path[QEAF_PATH_MAX];
    char old[QEAF_PATH_MAX];
    qeaf_doc_t *doc;
    int suffix = 0;

    if(index < 0 || index >= s->count) return -1;
    if(s->readonly) return -1;
    doc = &s->docs[index];
    do {
        build_name(base, ext, suffix, name, QEAF_NAME_MAX);
        suffix++;
    } while(doc_name_taken(s, name, index) && suffix < 10);
    if(doc_name_taken(s, name, index)) return -1;
    join_path(s->dir, name, path, QEAF_PATH_MAX);
    qeaf_copy_trunc(old, doc->meta.path, QEAF_PATH_MAX);
    if(strcmp(path, old) == 0) {
        qeaf_copy_trunc(doc->meta.name, name, QEAF_NAME_MAX);
        doc->meta.ext = ext;
        return index;
    }
    if(s->io.write != NULL &&
       s->io.write(s->ctx, path, doc->text, doc->len) != 0)
        return -1;
    if(s->io.remove != NULL) s->io.remove(s->ctx, old);
    qeaf_copy_trunc(doc->meta.name, name, QEAF_NAME_MAX);
    qeaf_copy_trunc(doc->meta.path, path, QEAF_PATH_MAX);
    doc->meta.ext = ext;
    return index;
}

int qeaf_store_delete(qeaf_store_t *s, int index)
{
    int i;
    if(index < 0 || index >= s->count) return -1;
    if(s->readonly) return -1;
    if(s->io.remove != NULL) s->io.remove(s->ctx, s->docs[index].meta.path);
    for(i = index; i < s->count - 1; i++) s->docs[i] = s->docs[i + 1];
    s->count--;
    memset(&s->docs[s->count], 0, sizeof(s->docs[0]));
    return 0;
}

int qeaf_store_duplicate(qeaf_store_t *s, int index)
{
    const qeaf_doc_t *src;
    char base[QEAF_NAME_MAX];
    int n, i;

    if(index < 0 || index >= s->count) return -1;
    if(s->readonly) return -1;
    src = &s->docs[index];
    qeaf_copy_trunc(base, src->meta.name, QEAF_NAME_MAX);
    n = qeaf_strlen(base);
    for(i = 0; i < n; i++) {
        if(base[i] == '.') { base[i] = '\0'; break; }
    }
    {
        char copy_base[QEAF_NAME_MAX];
        qeaf_copy_trunc(copy_base, base, QEAF_NAME_MAX - 4);
        qeaf_copy_trunc(copy_base + qeaf_strlen(copy_base), "COPY",
                        QEAF_NAME_MAX - qeaf_strlen(copy_base));
        n = qeaf_store_add(s, copy_base, src->meta.ext);
    }
    if(n < 0) return -1;
    qeaf_store_update(s, n, src->text);
    return n;
}

int qeaf_store_export(const qeaf_store_t *s, int index, const char *dir,
                      char *out, int cap)
{
    char path[QEAF_PATH_MAX];
    if(index < 0 || index >= s->count) return -1;
    if(s->readonly) return -2;
    if(s->io.mkdir != NULL) s->io.mkdir(s->ctx, dir);
    join_path(dir, s->docs[index].meta.name, path, QEAF_PATH_MAX);
    if(s->io.write == NULL ||
       s->io.write(s->ctx, path, s->docs[index].text, s->docs[index].len) != 0)
        return -2;
    if(out != NULL) qeaf_copy_trunc(out, path, cap);
    return 0;
}
