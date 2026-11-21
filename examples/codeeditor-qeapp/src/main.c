/* main.c - QEAPP native ABI v2 adapter for the CodeEditor port.
 *
 * The portable core (qeaf_code_*) knows nothing about the OS. This file is the
 * only translation unit that includes qeaf_api.h: it maps the two HALs onto
 * the frozen ABI v2 call table.
 *
 * What ABI v2 can do today:
 *   draw_text, log_utf8, request_permission, read_file, free.
 * What it cannot do yet:
 *   fill a rectangle, write a file, list a directory.
 *
 * Consequences, kept honest instead of simulated:
 *   - the 240x320 layout is centred 1:1 on the 720x1560 canvas; the ABI has no
 *     glyph scaling, so stretching coordinates would only letter-space text;
 *   - ui rect/clear/flush are no-ops, so selection and caret highlights are
 *     absent rather than painted with fake pixels;
 *   - storage is read-only: the store sees one real file, and every mutator
 *     reports failure because there is no write_file call to back it.
 *
 * Storage path: the host simulator seeds files by path. The adapter reads the
 * app-scoped path below and accepts no other path. On the device the same
 * relative path is resolved inside the app's storage scope by the loader.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "qeaf_api.h"
#include "qeaf_native_abi.h"

#include "qeaf_code_app.h"

#define QEAF_CODE_DIR   "apps/org.vqeaf.codeeditor/@codeeditor"
#define QEAF_CODE_FILE  QEAF_CODE_DIR "/MAIN.C"
#define QEAF_CODE_LEAF  "MAIN.C"

/* ABI v2 input contract used by this module (the ABI defines the event shape
 * but no key map). QEAF_INPUT_KEY codes 1..10 are qeaf_code_key_t values;
 * printable ASCII (32..126) is inserted as text; a touch press is OK. */
#define QEAF_INPUT_ASCII_MIN 32u
#define QEAF_INPUT_ASCII_MAX 126u

/* Fake display is 720x1560 (docs/QEAPP_NATIVE_ABI_V2.md). */
#define QEAF_CANVAS_W 720
#define QEAF_CANVAS_H 1560
#define QEAF_VIEW_X   ((QEAF_CANVAS_W - QEAF_SCREEN_W) / 2)
#define QEAF_VIEW_Y   ((QEAF_CANVAS_H - QEAF_SCREEN_H) / 2)

typedef struct {
    const qeaf_app_api_v2 *api;
    qeaf_code_app_t app;
    int started;
} code_module_t;

static code_module_t g_module;

/* ---- UI HAL ------------------------------------------------------------- */

static void ui_clear(void *ctx)
{
    (void)ctx; /* no fill primitive in ABI v2 */
}

static void ui_text(void *ctx, int x, int y, const char *utf8, int role)
{
    code_module_t *m = (code_module_t *)ctx;
    (void)role; /* no colour primitive in ABI v2 */
    if(m == NULL || m->api == NULL || utf8 == NULL) return;
    m->api->draw_text(x + QEAF_VIEW_X, y + QEAF_VIEW_Y, utf8);
}

static void ui_rect(void *ctx, int x, int y, int w, int h, int role)
{
    (void)ctx;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)role;
}

static void ui_flush(void *ctx)
{
    (void)ctx; /* draw_text is immediate; there is no frame buffer handle */
}

static int ui_measure(void *ctx, const char *utf8)
{
    (void)ctx;
    return utf8 != NULL ? (int)strlen(utf8) * QEAF_CELL_W : 0;
}

/* ---- storage HAL -------------------------------------------------------- */

static int store_list(void *ctx, const char *dir,
                      char names[][QEAF_NAME_MAX], int max)
{
    code_module_t *m = (code_module_t *)ctx;
    void *buffer = NULL;
    size_t size = 0;

    (void)dir;
    if(m == NULL || m->api == NULL || max <= 0) return 0;

    /* Probe the one real file. If the broker denies or the file is absent the
     * folder is genuinely empty; do not invent an entry. */
    if(m->api->read_file(QEAF_CODE_FILE, &buffer, &size) != 0)
        return 0;
    m->api->free(buffer);

    memcpy(names[0], QEAF_CODE_LEAF, sizeof(QEAF_CODE_LEAF));
    return 1;
}

static int store_read(void *ctx, const char *path, char *out, int cap)
{
    code_module_t *m = (code_module_t *)ctx;
    void *buffer = NULL;
    size_t size = 0;
    int n;

    if(m == NULL || m->api == NULL || path == NULL || out == NULL || cap <= 0)
        return -1;
    if(strcmp(path, QEAF_CODE_FILE) != 0)
        return -1;
    if(m->api->read_file(QEAF_CODE_FILE, &buffer, &size) != 0)
        return -1;

    n = (size > (size_t)(cap - 1)) ? cap - 1 : (int)size;
    if(n > 0) memcpy(out, buffer, (size_t)n);
    out[n] = '\0';
    m->api->free(buffer);
    return n;
}

/* No write_file in ABI v2: the store stays read-only. */

void *create(const qeaf_app_api_v2 *api)
{
    if(api == NULL || api->abi_version != VQEAF_APP_API_V2_VERSION)
        return NULL;
    memset(&g_module, 0, sizeof(g_module));
    g_module.api = api;
    api->log_utf8("CodeEditor: create");
    return &g_module;
}

int start(void *handle)
{
    code_module_t *m = (code_module_t *)handle;
    if(m == NULL || m->api == NULL) return -1;

    qeaf_store_io_t io;
    qeaf_code_ui_t ui;

    io.list = store_list;
    io.read = store_read;
    io.write = NULL;
    io.mkdir = NULL;
    io.remove = NULL;

    ui.clear = ui_clear;
    ui.text = ui_text;
    ui.rect = ui_rect;
    ui.flush = ui_flush;
    ui.measure = ui_measure;

    qeaf_app_init(&m->app, &io, m, QEAF_CODE_DIR);
    qeaf_app_set_ui(&m->app, &ui, m);
    qeaf_app_start(&m->app);
    m->started = 1;

    if(m->app.store.count == 1)
        m->api->log_utf8("CodeEditor: 1 source file, read-only storage");
    else
        m->api->log_utf8("CodeEditor: 0 source files, read-only storage");

    qeaf_app_draw(&m->app);
    return 0;
}

int pause(void *handle)
{
    code_module_t *m = (code_module_t *)handle;
    if(m == NULL || !m->started) return -1;
    return 0;
}

int resume(void *handle)
{
    code_module_t *m = (code_module_t *)handle;
    if(m == NULL || !m->started) return -1;
    qeaf_app_draw(&m->app);
    return 0;
}

int stop(void *handle)
{
    code_module_t *m = (code_module_t *)handle;
    if(m == NULL) return -1;
    return 0;
}

void destroy(void *handle)
{
    code_module_t *m = (code_module_t *)handle;
    if(m != NULL) {
        m->started = 0;
        m->api = NULL;
    }
}

void on_input(void *handle, const qeaf_input_event_t *event)
{
    code_module_t *m = (code_module_t *)handle;
    if(m == NULL || m->api == NULL || event == NULL) return;

    if(event->type == QEAF_INPUT_KEY) {
        if(!event->pressed) return;
        if(event->code >= 1u && event->code <= 10u) {
            qeaf_app_key(&m->app, (qeaf_code_key_t)event->code);
        } else if(event->code >= QEAF_INPUT_ASCII_MIN &&
                  event->code <= QEAF_INPUT_ASCII_MAX) {
            char text[2];
            text[0] = (char)event->code;
            text[1] = '\0';
            qeaf_app_text(&m->app, text);
        } else {
            return;
        }
    } else if(event->type == QEAF_INPUT_TOUCH) {
        if(!event->pressed) return;
        qeaf_app_key(&m->app, QEAF_KEY_OK);
    } else {
        return;
    }

    qeaf_app_draw(&m->app);
}
