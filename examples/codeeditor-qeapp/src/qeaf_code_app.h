/* qeaf_code_app.h - application state machine for the CodeEditor port.
 *
 * The MRE original (src/code_ui.h + src/code_input.h) mixed state, painting
 * and VM input in two headers. The port keeps the same screens and actions but
 * separates them: this module owns the state and calls a small UI HAL, while
 * qeaf_code_buffer/store/hl do the actual work. Nothing here talks to MRE or
 * to the OS directly, so the same code runs in host tests and, later, behind a
 * QEAPP runtime adapter.
 */
#ifndef QEAF_CODE_APP_H
#define QEAF_CODE_APP_H

#include <stdint.h>

#include "qeaf_code_buffer.h"
#include "qeaf_code_platform.h"
#include "qeaf_code_store.h"

typedef enum {
    QEAF_APP_SPLASH = 0,
    QEAF_APP_LIST,
    QEAF_APP_OPTIONS,
    QEAF_APP_VIEW,
    QEAF_APP_EDIT,
    QEAF_APP_EDITMENU,
    QEAF_APP_DELETE,
    QEAF_APP_SETTINGS,
    QEAF_APP_NAME,
    QEAF_APP_SYMBOL,
    QEAF_APP_FIND,
    QEAF_APP_DETAILS,
    QEAF_APP_ABOUT
} qeaf_app_state_t;

#define QEAF_STATUS_MAX 48
#define QEAF_SPLASH_TICKS 90

/* Screen metrics are logical pixels. The MRE original was 240x320; the phone
 * shell scales the same layout up, so keeping one set of numbers is enough. */
#define QEAF_SCREEN_W 240
#define QEAF_SCREEN_H 320
#define QEAF_CHROME_H 16
#define QEAF_STATUS_H 14
#define QEAF_CELL_W 6
#define QEAF_LINE_H 10
#define QEAF_LIST_ROWH 18

typedef struct {
    qeaf_app_state_t state;
    qeaf_app_state_t settings_parent;

    qeaf_store_t store;
    qeaf_buffer_t edit;
    qeaf_code_ui_t ui;
    void *ui_ctx;

    int list_sel;
    int list_top;
    int opt_sel;
    int edit_menu_sel;
    int settings_sel;
    int confirm_sel;
    int view_idx;
    int view_scroll;
    int view_col;
    int about_scroll;
    int detail_idx;

    int edit_idx;          /* slot being edited, -1 = new file */
    int edit_scroll;       /* first visible line */
    int edit_col;          /* horizontal scroll in characters */
    int edit_case;
    int splash_ticks;
    int blink_ticks;
    int blink_on;
    int loaded;
    int dirty;
    int quit;

    int name_editing;      /* 1 = rename, 0 = new file */
    int name_slot;
    int name_ext;
    char name_base[QEAF_NAME_MAX];

    char find_query[QEAF_NAME_MAX];
    int find_upper;
    int sym_page;
    int sym_sel;

    char status[QEAF_STATUS_MAX];
    int status_ticks;

    /* One role byte per source byte, refreshed when the editor is drawn. The
     * MRE original kept the same buffer as a file-scope g_hl[]. */
    uint8_t hl[QEAF_CODE_LEN];
} qeaf_code_app_t;

void qeaf_app_init(qeaf_code_app_t *a, const qeaf_store_io_t *io, void *io_ctx,
                   const char *dir);
void qeaf_app_set_ui(qeaf_code_app_t *a, const qeaf_code_ui_t *ui, void *ctx);
void qeaf_app_start(qeaf_code_app_t *a);
void qeaf_app_tick(qeaf_code_app_t *a);
void qeaf_app_draw(qeaf_code_app_t *a);
void qeaf_app_key(qeaf_code_app_t *a, qeaf_code_key_t key);
void qeaf_app_text(qeaf_code_app_t *a, const char *utf8);

/* Helpers exposed for tests and for a future QEAPP adapter. */
int qeaf_app_open_editor(qeaf_code_app_t *a, int index);
int qeaf_app_save(qeaf_code_app_t *a);
const char *qeaf_app_status(const qeaf_code_app_t *a);
const char *qeaf_app_state_name(qeaf_app_state_t state);

#endif /* QEAF_CODE_APP_H */
