/* qeaf_code_buffer.h - editor buffer, selection, undo and clipboard.
 *
 * Ported from the MRE original (src/code_input.h) without MRE types, keypad
 * multi-tap or drawing. The caller feeds characters (from a touch keyboard or
 * the host simulator) and control keys.
 */
#ifndef QEAF_CODE_BUFFER_H
#define QEAF_CODE_BUFFER_H

#include "qeaf_code_types.h"

typedef struct {
    char text[QEAF_CODE_LEN];
    int len;
    int cur;
    int sel_a;
    int sel_b;
    int pref_col;
    int pref_cur;
    int indent_width;
    int auto_indent;

    struct {
        char text[QEAF_CODE_LEN];
        int len;
        int cur;
    } undo[QEAF_UNDO_DEPTH];
    int undo_head;
    int undo_n;

    char clip[QEAF_CLIP_MAX];
    int clip_len;
} qeaf_buffer_t;

void qeaf_buffer_init(qeaf_buffer_t *b, int indent_width, int auto_indent);
void qeaf_buffer_set(qeaf_buffer_t *b, const char *text);
const char *qeaf_buffer_text(const qeaf_buffer_t *b);
int qeaf_buffer_len(const qeaf_buffer_t *b);
int qeaf_buffer_cursor(const qeaf_buffer_t *b);

int qeaf_buffer_has_sel(const qeaf_buffer_t *b);
void qeaf_buffer_sel_range(const qeaf_buffer_t *b, int *lo, int *hi);
void qeaf_buffer_clear_sel(qeaf_buffer_t *b);
void qeaf_buffer_select_all(qeaf_buffer_t *b);

void qeaf_buffer_undo_push(qeaf_buffer_t *b);
int qeaf_buffer_undo(qeaf_buffer_t *b);

int qeaf_buffer_insert_char(qeaf_buffer_t *b, char c);
int qeaf_buffer_insert_str(qeaf_buffer_t *b, const char *s);
int qeaf_buffer_backspace(qeaf_buffer_t *b);
int qeaf_buffer_delete_forward(qeaf_buffer_t *b);
int qeaf_buffer_delete_selection(qeaf_buffer_t *b);

int qeaf_buffer_move(qeaf_buffer_t *b, int delta);
int qeaf_buffer_line_move(qeaf_buffer_t *b, int dir);
int qeaf_buffer_home(qeaf_buffer_t *b);
int qeaf_buffer_end(qeaf_buffer_t *b);
int qeaf_buffer_newline(qeaf_buffer_t *b);
int qeaf_buffer_indent(qeaf_buffer_t *b);
int qeaf_buffer_outdent(qeaf_buffer_t *b);

int qeaf_buffer_copy(qeaf_buffer_t *b);
int qeaf_buffer_cut(qeaf_buffer_t *b);
int qeaf_buffer_paste(qeaf_buffer_t *b);

/* Case-sensitive search from the caret, wrapping once. Returns the match
 * offset and selects it, or -1 when the query is empty/not found. */
int qeaf_buffer_find(qeaf_buffer_t *b, const char *query);

int qeaf_buffer_line_of(const qeaf_buffer_t *b, int pos);
int qeaf_buffer_column_of(const qeaf_buffer_t *b, int pos);

#endif /* QEAF_CODE_BUFFER_H */
