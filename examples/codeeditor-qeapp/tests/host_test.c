/* host_test.c - host-side proof for the CodeEditor QEAPP port.
 *
 * Builds the portable core (buffer, tokenizer, store, app) against an
 * in-memory storage HAL and a capturing UI HAL. It never touches MRE, LVGL or
 * the device, so it can run on the build machine:
 *
 *   gcc -std=c99 -Wall -Wextra -I src src/qeaf_code_*.c \
 *       tests/host_test.c -o build/host_test
 */
#include <stdio.h>
#include <string.h>

#include "qeaf_code_app.h"
#include "qeaf_code_buffer.h"
#include "qeaf_code_hl.h"
#include "qeaf_code_store.h"
#include "qeaf_code_util.h"

static int failures;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if(!(cond)) {                                                         \
            printf("FAIL line %d: %s\n", __LINE__, msg);                      \
            failures++;                                                       \
        }                                                                     \
    } while(0)

/* ---- in-memory storage HAL ---------------------------------------------- */

#define MEMFS_MAX 24

typedef struct {
    char path[QEAF_PATH_MAX];
    char text[QEAF_CODE_LEN];
    int len;
} memfile_t;

typedef struct {
    memfile_t files[MEMFS_MAX];
    int count;
} memfs_t;

static const char *base_name(const char *path)
{
    const char *p = path, *b = path;
    while(*p != '\0') {
        if(*p == '/' || *p == '\\') b = p + 1;
        p++;
    }
    return b;
}

static memfile_t *mem_find(memfs_t *fs, const char *path)
{
    int i;
    for(i = 0; i < fs->count; i++)
        if(strcmp(fs->files[i].path, path) == 0) return &fs->files[i];
    return NULL;
}

static int mem_list(void *ctx, const char *dir, char names[][QEAF_NAME_MAX],
                    int max)
{
    memfs_t *fs = (memfs_t *)ctx;
    int i, n = 0, dl = qeaf_strlen(dir);

    for(i = 0; i < fs->count && n < max; i++) {
        const char *p = fs->files[i].path;
        const char *rest;
        if(dl > 0 && (strncmp(p, dir, (size_t)dl) != 0 || p[dl] != '/'))
            continue;
        rest = p + (dl > 0 ? dl + 1 : 0);
        if(strchr(rest, '/') != NULL) continue;
        qeaf_copy_trunc(names[n], base_name(rest), QEAF_NAME_MAX);
        n++;
    }
    return n;
}

static int mem_read(void *ctx, const char *path, char *out, int cap)
{
    memfs_t *fs = (memfs_t *)ctx;
    memfile_t *f = mem_find(fs, path);
    int n;
    if(f == NULL) return -1;
    n = f->len < cap - 1 ? f->len : cap - 1;
    memcpy(out, f->text, (size_t)n);
    out[n] = '\0';
    return n;
}

static int mem_write(void *ctx, const char *path, const char *text, int len)
{
    memfs_t *fs = (memfs_t *)ctx;
    memfile_t *f = mem_find(fs, path);
    if(f == NULL) {
        if(fs->count >= MEMFS_MAX) return -1;
        f = &fs->files[fs->count++];
        qeaf_copy_trunc(f->path, path, QEAF_PATH_MAX);
    }
    if(len > QEAF_CODE_LEN - 1) len = QEAF_CODE_LEN - 1;
    memcpy(f->text, text, (size_t)len);
    f->text[len] = '\0';
    f->len = len;
    return 0;
}

static int mem_remove(void *ctx, const char *path)
{
    memfs_t *fs = (memfs_t *)ctx;
    int i;
    for(i = 0; i < fs->count; i++) {
        if(strcmp(fs->files[i].path, path) == 0) {
            int j;
            for(j = i; j < fs->count - 1; j++) fs->files[j] = fs->files[j + 1];
            fs->count--;
            return 0;
        }
    }
    return -1;
}

static int mem_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    (void)path;
    return 0;
}

static qeaf_store_io_t mem_io(void)
{
    qeaf_store_io_t io;
    io.list = mem_list;
    io.read = mem_read;
    io.write = mem_write;
    io.mkdir = mem_mkdir;
    io.remove = mem_remove;
    return io;
}

/* ---- capturing UI HAL --------------------------------------------------- */

typedef struct {
    char log[16384];
    int len;
    int texts;
    int rects;
} capture_t;

static void cap_add(capture_t *c, const char *s)
{
    int n = qeaf_strlen(s);
    if(c->len + n >= (int)sizeof(c->log) - 1) return;
    memcpy(c->log + c->len, s, (size_t)n);
    c->len += n;
    c->log[c->len] = '\0';
}

static void cap_clear(void *ctx)
{
    capture_t *c = (capture_t *)ctx;
    c->len = 0;
    c->log[0] = '\0';
    c->texts = 0;
    c->rects = 0;
}

static void cap_text(void *ctx, int x, int y, const char *utf8, int role)
{
    capture_t *c = (capture_t *)ctx;
    char head[32];
    int n = qeaf_append_str(head, (int)sizeof(head), 0, "@");
    n = qeaf_append_int(head, (int)sizeof(head), n, x);
    n = qeaf_append_str(head, (int)sizeof(head), n, ",");
    n = qeaf_append_int(head, (int)sizeof(head), n, y);
    n = qeaf_append_str(head, (int)sizeof(head), n, "|");
    n = qeaf_append_int(head, (int)sizeof(head), n, role);
    n = qeaf_append_str(head, (int)sizeof(head), n, "|");
    (void)n;
    cap_add(c, head);
    cap_add(c, utf8);
    cap_add(c, "\n");
    c->texts++;
}

static void cap_rect(void *ctx, int x, int y, int w, int h, int role)
{
    capture_t *c = (capture_t *)ctx;
    (void)x; (void)y; (void)w; (void)h; (void)role;
    c->rects++;
}

static void cap_flush(void *ctx)
{
    (void)ctx;
}

static int cap_measure(void *ctx, const char *utf8)
{
    (void)ctx;
    return qeaf_strlen(utf8) * QEAF_CELL_W;
}

static int log_has(const capture_t *c, const char *needle)
{
    return strstr(c->log, needle) != NULL;
}

static qeaf_code_ui_t cap_ui(void)
{
    qeaf_code_ui_t ui;
    ui.clear = cap_clear;
    ui.text = cap_text;
    ui.rect = cap_rect;
    ui.flush = cap_flush;
    ui.measure = cap_measure;
    return ui;
}

/* ---- buffer tests ------------------------------------------------------- */

static void test_buffer(void)
{
    qeaf_buffer_t b;
    int at;

    qeaf_buffer_init(&b, 4, 1);
    qeaf_buffer_set(&b, "int main() {\n  return 0;\n}");
    CHECK(b.len == 26, "buffer length");
    CHECK(qeaf_buffer_line_of(&b, 0) == 1, "line of start");
    CHECK(qeaf_buffer_column_of(&b, 2) == 3, "column of offset 2");

    qeaf_buffer_move(&b, 9);
    CHECK(qeaf_buffer_cursor(&b) == 9, "move right");
    qeaf_buffer_insert_char(&b, 'X');
    CHECK(strcmp(qeaf_buffer_text(&b), "int main(X) {\n  return 0;\n}") == 0,
          "insert char");
    CHECK(qeaf_buffer_undo(&b), "undo available");
    CHECK(strcmp(qeaf_buffer_text(&b), "int main() {\n  return 0;\n}") == 0,
          "undo restores text");

    qeaf_buffer_select_all(&b);
    CHECK(qeaf_buffer_has_sel(&b), "select all");
    CHECK(qeaf_buffer_copy(&b) == 26, "copy all");
    CHECK(qeaf_buffer_cut(&b) == 26, "cut all");
    CHECK(qeaf_buffer_len(&b) == 0, "cut empties buffer");
    CHECK(qeaf_buffer_paste(&b) == 26, "paste restores");
    CHECK(strcmp(qeaf_buffer_text(&b), "int main() {\n  return 0;\n}") == 0,
          "paste text");

    at = qeaf_buffer_find(&b, "return");
    CHECK(at >= 0 && at == 15, "find return");
    CHECK(qeaf_buffer_has_sel(&b), "find selects match");
    CHECK(qeaf_buffer_find(&b, "missing") == -1, "find missing");

    qeaf_buffer_set(&b, "if (x) {");
    b.cur = b.len;
    qeaf_buffer_newline(&b);
    CHECK(strcmp(qeaf_buffer_text(&b), "if (x) {\n    ") == 0,
          "newline auto indent");

    qeaf_buffer_set(&b, "a\nb\nc");
    b.sel_a = 0;
    b.sel_b = b.len;
    b.cur = b.len;
    qeaf_buffer_indent(&b);
    CHECK(strcmp(qeaf_buffer_text(&b), "    a\n    b\n    c") == 0,
          "indent selection");
    qeaf_buffer_select_all(&b);
    qeaf_buffer_outdent(&b);
    CHECK(strcmp(qeaf_buffer_text(&b), "a\nb\nc") == 0, "outdent selection");

    qeaf_buffer_set(&b, "one\ntwo\nthree");
    b.cur = 1;
    qeaf_buffer_line_move(&b, 1);
    CHECK(qeaf_buffer_cursor(&b) == 5, "line down keeps column");
    qeaf_buffer_home(&b);
    CHECK(qeaf_buffer_cursor(&b) == 4, "home goes to line start");
    qeaf_buffer_end(&b);
    CHECK(qeaf_buffer_cursor(&b) == 7, "end goes to line end");
}

/* ---- tokenizer tests ---------------------------------------------------- */

static void test_hl(void)
{
    uint8_t roles[QEAF_CODE_LEN];
    const char *c = "int x = 42; // note";
    const char *html = "<div class=\"a\">x</div>";
    const char *css = "body { color: #fff; }";
    const char *js = "function f() { return 1; }";
    const char *py = "def f():\n    # c\n    return True";

    qeaf_hl_tokenize(c, QEAF_LANG_C, roles, QEAF_CODE_LEN);
    CHECK(roles[0] == QEAF_HL_KEY, "C int keyword");
    CHECK(roles[8] == QEAF_HL_NUM && roles[9] == QEAF_HL_NUM, "C number");
    CHECK(roles[13] == QEAF_HL_CMT, "C line comment");

    qeaf_hl_tokenize(html, QEAF_LANG_HTML, roles, QEAF_CODE_LEN);
    CHECK(roles[0] == QEAF_HL_TAG, "HTML tag");
    CHECK(roles[5] == QEAF_HL_ATTR, "HTML attribute");
    CHECK(roles[11] == QEAF_HL_STR, "HTML attribute string");

    qeaf_hl_tokenize(css, QEAF_LANG_CSS, roles, QEAF_CODE_LEN);
    CHECK(roles[7] == QEAF_HL_PROP, "CSS property");
    CHECK(roles[14] == QEAF_HL_NUM, "CSS colour literal");

    qeaf_hl_tokenize(js, QEAF_LANG_JS, roles, QEAF_CODE_LEN);
    CHECK(roles[0] == QEAF_HL_KEY, "JS function keyword");
    CHECK(roles[22] == QEAF_HL_NUM, "JS number");

    qeaf_hl_tokenize(py, QEAF_LANG_PY, roles, QEAF_CODE_LEN);
    CHECK(roles[0] == QEAF_HL_KEY, "PY def keyword");
    CHECK(roles[13] == QEAF_HL_CMT, "PY comment");
    CHECK(roles[28] == QEAF_HL_KEY, "PY True keyword");

    qeaf_hl_tokenize(c, QEAF_LANG_NONE, roles, QEAF_CODE_LEN);
    CHECK(roles[0] == QEAF_HL_PLAIN, "no language leaves plain roles");
}

/* ---- store tests -------------------------------------------------------- */

static void test_store(void)
{
    memfs_t fs;
    qeaf_store_io_t io = mem_io();
    qeaf_store_t store;
    char out[QEAF_PATH_MAX];
    int i;

    memset(&fs, 0, sizeof(fs));
    qeaf_store_init(&store, &io, &fs, "PROJ");
    CHECK(qeaf_store_count(&store) == 0, "empty store");

    i = qeaf_store_add(&store, "main", QEAF_EXT_C);
    CHECK(i == 0, "add file");
    CHECK(strcmp(qeaf_store_doc(&store, 0)->meta.name, "MAIN.C") == 0,
          "FAT-style name");
    CHECK(qeaf_store_update(&store, 0, "int main(void){return 0;}") == 0,
          "update file");

    CHECK(qeaf_store_rescan(&store) == 1, "rescan finds one file");
    CHECK(strcmp(qeaf_store_doc(&store, 0)->text,
                 "int main(void){return 0;}") == 0, "rescan reads text");
    CHECK(qeaf_store_lang_for(qeaf_store_doc(&store, 0)) == QEAF_LANG_C,
          "C language");

    CHECK(qeaf_store_rename(&store, 0, "app", QEAF_EXT_JS) == 0, "rename");
    CHECK(strcmp(qeaf_store_doc(&store, 0)->meta.name, "APP.JS") == 0,
          "renamed file");
    CHECK(qeaf_store_lang_for(qeaf_store_doc(&store, 0)) == QEAF_LANG_JS,
          "JS language");
    CHECK(qeaf_store_rename(&store, 0, "app", QEAF_EXT_JS) == 0,
          "rename to same name");
    CHECK(mem_find(&fs, "PROJ/APP.JS") != NULL, "same-name rename keeps file");

    CHECK(qeaf_store_duplicate(&store, 0) == 1, "duplicate");
    CHECK(qeaf_store_count(&store) == 2, "duplicate count");
    CHECK(strstr(qeaf_store_doc(&store, 1)->meta.name, "COPY") != NULL,
          "duplicate name");

    CHECK(qeaf_store_export(&store, 0, "EXPORT", out, QEAF_PATH_MAX) == 0,
          "export");
    CHECK(strcmp(out, "EXPORT/APP.JS") == 0, "export path");
    CHECK(mem_find(&fs, "EXPORT/APP.JS") != NULL, "export written");

    CHECK(qeaf_store_delete(&store, 1) == 0, "delete");
    CHECK(qeaf_store_count(&store) == 1, "delete count");
    CHECK(qeaf_ext_from_name("HELLO.PY") == QEAF_EXT_PY, "extension parse");
    CHECK(strcmp(qeaf_ext_name(QEAF_EXT_HTML), ".HTML") == 0, "extension name");

    i = qeaf_store_add(&store, "verylongname", QEAF_EXT_TXT);
    CHECK(i == 1, "add long base");
    CHECK(strcmp(qeaf_store_doc(&store, 1)->meta.name, "VERYLONG.TXT") == 0,
          "base name truncated to 8.3");
}

/* ---- app tests ---------------------------------------------------------- */

static qeaf_code_app_t app;
static memfs_t appfs;
static capture_t capture;

static void app_boot(void)
{
    qeaf_store_io_t io = mem_io();
    qeaf_code_ui_t ui = cap_ui();
    memset(&appfs, 0, sizeof(appfs));
    memset(&capture, 0, sizeof(capture));
    qeaf_app_init(&app, &io, &appfs, "PROJ");
    qeaf_app_set_ui(&app, &ui, &capture);
    qeaf_app_start(&app);
}

static void test_app_empty(void)
{
    int i;
    app_boot();
    CHECK(app.state == QEAF_APP_SPLASH, "boot starts on splash");
    CHECK(strstr(qeaf_app_status(&app), "SCANNED 0 FILES") != NULL,
          "empty scan status");
    for(i = 0; i < QEAF_SPLASH_TICKS; i++) qeaf_app_tick(&app);
    CHECK(app.state == QEAF_APP_LIST, "splash ends on list");
    qeaf_app_draw(&app);
    CHECK(log_has(&capture, "CODE EDITOR"), "list title drawn");
    CHECK(log_has(&capture, "NO SOURCE FILES"), "empty list is honest");
    CHECK(capture.texts > 0 && capture.rects > 0, "UI HAL received draw calls");
}

static void test_app_edit_flow(void)
{
    int i;
    const qeaf_doc_t *d;

    app_boot();
    for(i = 0; i < QEAF_SPLASH_TICKS; i++) qeaf_app_tick(&app);

    /* MENU > New file, name TEST with .H extension. */
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_OPTIONS, "options opened");
    qeaf_app_key(&app, QEAF_KEY_DOWN);
    qeaf_app_key(&app, QEAF_KEY_DOWN);
    CHECK(app.opt_sel == 2, "new file row");
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_NAME, "name screen");
    qeaf_app_text(&app, "test");
    CHECK(strcmp(app.name_base, "TEST") == 0, "name upper-cased");
    qeaf_app_key(&app, QEAF_KEY_RIGHT);
    CHECK(app.name_ext == QEAF_EXT_H, "extension cycles");
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_EDIT, "new file opens editor");
    CHECK(app.store.count == 1, "new file stored");
    CHECK(strcmp(qeaf_store_doc(&app.store, 0)->meta.name, "TEST.H") == 0,
          "new file name");

    qeaf_app_text(&app, "int x;");
    CHECK(app.dirty, "typing marks dirty");
    CHECK(qeaf_app_save(&app) == 1, "save succeeds");
    CHECK(!app.dirty, "save clears dirty");
    d = qeaf_store_doc(&app.store, 0);
    CHECK(d != NULL && strcmp(d->text, "int x;") == 0, "saved text");

    /* Undo through the editor menu. */
    qeaf_app_key(&app, QEAF_KEY_SOFT_LEFT);
    CHECK(app.state == QEAF_APP_EDITMENU, "editor menu");
    qeaf_app_key(&app, QEAF_KEY_DOWN);
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_EDIT, "undo returns to editor");
    CHECK(strcmp(qeaf_buffer_text(&app.edit), "int x") == 0,
          "undo removed last char");

    /* Find through the menu. */
    qeaf_app_key(&app, QEAF_KEY_SOFT_LEFT);
    for(i = 0; i < 10; i++) qeaf_app_key(&app, QEAF_KEY_DOWN);
    CHECK(app.edit_menu_sel == 10, "find row selected");
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_FIND, "find screen");
    qeaf_app_text(&app, "x");
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_EDIT, "find returns to editor");
    CHECK(qeaf_buffer_has_sel(&app.edit), "find selects match");

    /* Symbol palette inserts a character. */
    qeaf_app_key(&app, QEAF_KEY_SOFT_LEFT);
    for(i = 0; i < 9; i++) qeaf_app_key(&app, QEAF_KEY_DOWN);
    CHECK(app.edit_menu_sel == 9, "symbols row selected");
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_SYMBOL, "symbol palette");
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_EDIT, "symbol returns to editor");

    qeaf_app_key(&app, QEAF_KEY_BACK);
    CHECK(app.state == QEAF_APP_LIST, "back closes editor");
    CHECK(app.store.count == 1, "store still has one file");

    /* Settings toggles auto indent. */
    qeaf_app_key(&app, QEAF_KEY_SOFT_RIGHT);
    CHECK(app.state == QEAF_APP_SETTINGS, "settings opened");
    CHECK(app.store.auto_indent == 1, "auto indent default");
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.store.auto_indent == 0, "auto indent toggled");
    qeaf_app_key(&app, QEAF_KEY_BACK);
    CHECK(app.state == QEAF_APP_LIST, "settings back");

    /* Read view. */
    qeaf_app_key(&app, QEAF_KEY_OK);
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_VIEW, "view opened");
    qeaf_app_draw(&app);
    CHECK(log_has(&capture, "TEST.H"), "view title");
    qeaf_app_key(&app, QEAF_KEY_BACK);
    CHECK(app.state == QEAF_APP_LIST, "view back");

    /* Delete with confirmation. */
    qeaf_app_key(&app, QEAF_KEY_OK);
    for(i = 0; i < 11; i++) qeaf_app_key(&app, QEAF_KEY_DOWN);
    CHECK(app.opt_sel == 11, "delete row");
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_DELETE, "delete confirm");
    qeaf_app_key(&app, QEAF_KEY_DOWN);
    CHECK(app.confirm_sel == 1, "yes selected");
    qeaf_app_key(&app, QEAF_KEY_OK);
    CHECK(app.state == QEAF_APP_LIST, "delete returns to list");
    CHECK(app.store.count == 0, "file deleted");
}

static void test_app_draw_all_screens(void)
{
    static const qeaf_app_state_t states[] = {
        QEAF_APP_SPLASH, QEAF_APP_LIST, QEAF_APP_OPTIONS, QEAF_APP_VIEW,
        QEAF_APP_EDIT, QEAF_APP_EDITMENU, QEAF_APP_DELETE, QEAF_APP_SETTINGS,
        QEAF_APP_NAME, QEAF_APP_SYMBOL, QEAF_APP_FIND, QEAF_APP_DETAILS,
        QEAF_APP_ABOUT
    };
    unsigned int i;

    app_boot();
    CHECK(qeaf_store_add(&app.store, "main", QEAF_EXT_C) >= 0, "draw fixture");
    CHECK(qeaf_store_update(&app.store, 0, "int main(void) {\n    return 0;\n}") == 0,
          "draw fixture text");
    CHECK(qeaf_store_rescan(&app.store) == 1, "draw fixture rescan");

    for(i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        app.state = states[i];
        app.list_sel = 0;
        app.view_idx = 0;
        app.detail_idx = 0;
        app.settings_parent = QEAF_APP_LIST;
        app.name_base[0] = '\0';
        app.find_query[0] = '\0';
        if(states[i] == QEAF_APP_EDIT || states[i] == QEAF_APP_EDITMENU ||
           states[i] == QEAF_APP_SYMBOL || states[i] == QEAF_APP_FIND) {
            CHECK(qeaf_app_open_editor(&app, 0) == 1, "draw editor fixture");
            app.state = states[i];
        }
        capture.texts = 0;
        capture.rects = 0;
        qeaf_app_draw(&app);
        CHECK(capture.texts > 0, qeaf_app_state_name(states[i]));
    }
}

static void test_app_readonly(void)
{
    char big[QEAF_CODE_LEN];
    int i;
    app_boot();
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    CHECK(qeaf_store_add(&app.store, "big", QEAF_EXT_C) >= 0, "add big file");
    CHECK(qeaf_store_update(&app.store, 0, big) == 0, "write big file");
    CHECK(qeaf_store_rescan(&app.store) == 1, "rescan big file");
    CHECK(qeaf_store_doc(&app.store, 0)->meta.trunc == 1,
          "oversized file marked read-only");
    CHECK(qeaf_app_open_editor(&app, 0) == 1, "open read-only file");
    i = qeaf_buffer_len(&app.edit);
    qeaf_app_text(&app, "zz");
    CHECK(qeaf_buffer_len(&app.edit) == i, "read-only rejects typing");
    CHECK(strstr(qeaf_app_status(&app), "READ ONLY") != NULL,
          "read-only status is honest");
}

int main(void)
{
    test_buffer();
    test_hl();
    test_store();
    test_app_empty();
    test_app_edit_flow();
    test_app_draw_all_screens();
    test_app_readonly();

    if(failures != 0) {
        printf("host tests: %d FAILED\n", failures);
        return 1;
    }
    printf("host tests: OK\n");
    return 0;
}
