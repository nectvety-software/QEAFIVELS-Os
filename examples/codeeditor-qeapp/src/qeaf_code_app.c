#include "qeaf_code_app.h"

#include "qeaf_code_hl.h"
#include "qeaf_code_util.h"

#include <string.h>

#define EDIT_TOP   QEAF_CHROME_H
#define EDIT_ROWS  ((QEAF_SCREEN_H - QEAF_CHROME_H - 30) / QEAF_LINE_H)
#define EDIT_COLS  ((QEAF_SCREEN_W - 24) / QEAF_CELL_W)
#define LIST_ROWS  ((QEAF_SCREEN_H - QEAF_CHROME_H - QEAF_STATUS_H - 4) / QEAF_LIST_ROWH)
#define MENU_ROWH  16

static const char option_labels[14][16] = {
    "Open file", "Edit code", "New file", "New folder", "Details", "Rename",
    "Duplicate", "Settings...", "Save copy", "Copy text", "Paste text",
    "Delete", "Rescan folder", "About"
};

static const char editmenu_labels[13][16] = {
    "Save", "Undo", "Cut", "Copy", "Paste", "Select all", "Delete",
    "Indent", "Outdent", "Symbols", "Find...", "Settings...", "Key page"
};

static const char symbol_pages[3][32] = {
    "{}[]()<>;:,.=+",
    "-*/%&|^!~?\"'#",
    "@$_\\`0123456789"
};

const char *qeaf_app_state_name(qeaf_app_state_t state)
{
    switch(state) {
    case QEAF_APP_SPLASH:   return "splash";
    case QEAF_APP_LIST:     return "list";
    case QEAF_APP_OPTIONS:  return "options";
    case QEAF_APP_VIEW:     return "view";
    case QEAF_APP_EDIT:     return "edit";
    case QEAF_APP_EDITMENU: return "editmenu";
    case QEAF_APP_DELETE:   return "delete";
    case QEAF_APP_SETTINGS: return "settings";
    case QEAF_APP_NAME:     return "name";
    case QEAF_APP_SYMBOL:   return "symbol";
    case QEAF_APP_FIND:     return "find";
    case QEAF_APP_DETAILS:  return "details";
    case QEAF_APP_ABOUT:    return "about";
    default:                return "unknown";
    }
}

static void status_set(qeaf_code_app_t *a, const char *s)
{
    qeaf_copy_trunc(a->status, s != NULL ? s : "", QEAF_STATUS_MAX);
    a->status_ticks = 120;
}

const char *qeaf_app_status(const qeaf_code_app_t *a)
{
    return a->status;
}

static const qeaf_doc_t *cur_doc(const qeaf_code_app_t *a)
{
    return qeaf_store_doc(&a->store, a->list_sel);
}

static int cur_slot(const qeaf_code_app_t *a)
{
    return (a->list_sel >= 0 && a->list_sel < a->store.count) ? a->list_sel : -1;
}

static int edit_readonly(const qeaf_code_app_t *a)
{
    const qeaf_doc_t *d = qeaf_store_doc(&a->store, a->edit_idx);
    return a->store.readonly || (d != NULL && d->meta.trunc);
}

/* ---- line geometry ------------------------------------------------------ */

static int text_lines(const char *s, int len)
{
    int n = 1, i;
    for(i = 0; i < len; i++) if(s[i] == '\n') n++;
    return n;
}

static int line_start_at(const char *s, int len, int line)
{
    int i = 0, n = 0;
    if(line <= 0) return 0;
    while(i < len) {
        if(s[i] == '\n') {
            n++;
            if(n == line) return i + 1;
        }
        i++;
    }
    return len;
}

static int line_end_at(const char *s, int len, int start)
{
    int i = start;
    while(i < len && s[i] != '\n') i++;
    return i;
}

static void caret_line_col(const qeaf_buffer_t *b, int *line, int *col)
{
    int i, l = 0, ls = 0;
    for(i = 0; i < b->cur && i < b->len; i++) {
        if(b->text[i] == '\n') { l++; ls = i + 1; }
    }
    *line = l;
    *col = b->cur - ls;
}

static void scroll_to_caret(qeaf_code_app_t *a)
{
    int line, col;
    caret_line_col(&a->edit, &line, &col);
    if(line < a->edit_scroll) a->edit_scroll = line;
    if(line >= a->edit_scroll + EDIT_ROWS)
        a->edit_scroll = line - EDIT_ROWS + 1;
    if(col < a->edit_col) a->edit_col = col;
    if(col >= a->edit_col + EDIT_COLS - 1)
        a->edit_col = col - EDIT_COLS + 2;
    if(a->edit_scroll < 0) a->edit_scroll = 0;
    if(a->edit_col < 0) a->edit_col = 0;
}

/* ---- drawing helpers ---------------------------------------------------- */

static void ui_clear(qeaf_code_app_t *a)
{
    if(a->ui.clear) a->ui.clear(a->ui_ctx);
}

static void ui_text(qeaf_code_app_t *a, int x, int y, const char *s, int role)
{
    if(a->ui.text && s != NULL) a->ui.text(a->ui_ctx, x, y, s, role);
}

static void ui_rect(qeaf_code_app_t *a, int x, int y, int w, int h, int role)
{
    if(a->ui.rect) a->ui.rect(a->ui_ctx, x, y, w, h, role);
}

static void ui_flush(qeaf_code_app_t *a)
{
    if(a->ui.flush) a->ui.flush(a->ui_ctx);
}

static int text_w(const char *s)
{
    return qeaf_strlen(s) * QEAF_CELL_W;
}

static void draw_chrome(qeaf_code_app_t *a, const char *title,
                        const char *left, const char *right)
{
    ui_rect(a, 0, 0, QEAF_SCREEN_W, QEAF_CHROME_H, QEAF_UI_ACCENT);
    ui_text(a, 4, 4, title, QEAF_UI_TITLE);
    ui_rect(a, 0, QEAF_SCREEN_H - QEAF_STATUS_H, QEAF_SCREEN_W,
            QEAF_STATUS_H, QEAF_UI_ACCENT);
    ui_text(a, 4, QEAF_SCREEN_H - QEAF_STATUS_H + 3, left, QEAF_UI_TITLE);
    if(right != NULL && right[0] != '\0')
        ui_text(a, QEAF_SCREEN_W - text_w(right) - 4,
                QEAF_SCREEN_H - QEAF_STATUS_H + 3, right, QEAF_UI_TITLE);
}

static void draw_status_msg(qeaf_code_app_t *a)
{
    if(a->status_ticks > 0 && a->status[0] != '\0')
        ui_text(a, 4, QEAF_SCREEN_H - QEAF_STATUS_H - 12, a->status,
                QEAF_UI_WARN);
}

static void draw_row(qeaf_code_app_t *a, int y, int selected,
                     const char *label, int enabled)
{
    if(selected)
        ui_rect(a, 2, y, QEAF_SCREEN_W - 4, MENU_ROWH - 1, QEAF_UI_SEL);
    ui_text(a, 8, y + 3, label,
            selected ? QEAF_UI_TITLE : (enabled ? QEAF_UI_TEXT : QEAF_UI_DIM));
}

/* ---- splash ------------------------------------------------------------- */

static void draw_splash(qeaf_code_app_t *a)
{
    int w = (QEAF_SCREEN_W - 60) * a->splash_ticks / QEAF_SPLASH_TICKS;
    ui_rect(a, 0, 0, QEAF_SCREEN_W, QEAF_SCREEN_H, QEAF_UI_TEXT);
    ui_text(a, 40, 92, "CODE EDITOR", QEAF_UI_ACCENT);
    ui_text(a, 40, 108, "QEAPP native port", QEAF_UI_DIM);
    ui_rect(a, 30, 140, QEAF_SCREEN_W - 60, 4, QEAF_UI_DIM);
    if(w > 0) ui_rect(a, 30, 140, w, 4, QEAF_UI_ACCENT);
    ui_text(a, 30, 156, "Reading project folder", QEAF_UI_DIM);
    if(a->status_ticks > 0) ui_text(a, 30, 176, a->status, QEAF_UI_WARN);
}

/* ---- list --------------------------------------------------------------- */

static void draw_list(qeaf_code_app_t *a)
{
    int i, y = QEAF_CHROME_H + 4;
    draw_chrome(a, "CODE EDITOR", "OPEN", "MENU");
    if(a->list_sel < a->list_top) a->list_top = a->list_sel;
    if(a->list_sel >= a->list_top + LIST_ROWS)
        a->list_top = a->list_sel - LIST_ROWS + 1;
    if(a->list_top > a->store.count - LIST_ROWS)
        a->list_top = a->store.count - LIST_ROWS;
    if(a->list_top < 0) a->list_top = 0;
    if(a->store.count <= 0) {
        ui_text(a, 8, y + 4, "NO SOURCE FILES", QEAF_UI_DIM);
        ui_text(a, 8, y + 20, "Use MENU > New file", QEAF_UI_DIM);
    }
    for(i = 0; i < LIST_ROWS; i++) {
        int idx = a->list_top + i;
        const qeaf_doc_t *d;
        char line[QEAF_NAME_MAX + 8];
        int p = 0;
        if(idx >= a->store.count) break;
        d = qeaf_store_doc(&a->store, idx);
        if(d == NULL) continue;
        if(idx == a->list_sel)
            ui_rect(a, 2, y, QEAF_SCREEN_W - 4, QEAF_LIST_ROWH - 1, QEAF_UI_SEL);
        p = qeaf_append_int(line, (int)sizeof(line), 0, idx + 1);
        p = qeaf_append_str(line, (int)sizeof(line), p, "  ");
        p = qeaf_append_str(line, (int)sizeof(line), p, d->meta.name);
        ui_text(a, 6, y + 5, line,
                idx == a->list_sel ? QEAF_UI_TITLE : QEAF_UI_TEXT);
        {
            char size[12];
            int sp = qeaf_append_int(size, (int)sizeof(size), 0, d->meta.size);
            sp = qeaf_append_str(size, (int)sizeof(size), sp, "B");
            (void)sp;
            ui_text(a, QEAF_SCREEN_W - text_w(size) - 6, y + 5, size,
                    QEAF_UI_DIM);
        }
        y += QEAF_LIST_ROWH;
    }
    draw_status_msg(a);
}

/* ---- options ------------------------------------------------------------ */

static int option_enabled(const qeaf_code_app_t *a, int i)
{
    if(i == 2 || i == 7 || i == 12 || i == 13) return 1;
    if(i == 10) return a->edit.clip_len > 0;
    if(i == 3) return 0; /* folder support is not part of this port */
    return cur_slot(a) >= 0;
}

static void draw_options(qeaf_code_app_t *a)
{
    int i, y = 24;
    ui_rect(a, 0, 0, QEAF_SCREEN_W, QEAF_SCREEN_H, QEAF_UI_TEXT);
    ui_rect(a, 10, 20, QEAF_SCREEN_W - 20, MENU_ROWH * 14 + 16,
            QEAF_UI_DIM);
    ui_rect(a, 10, 20, QEAF_SCREEN_W - 20, 14, QEAF_UI_ACCENT);
    ui_text(a, 16, 23, "OPTIONS", QEAF_UI_TITLE);
    y = 36;
    for(i = 0; i < 14; i++) {
        draw_row(a, y, i == a->opt_sel, option_labels[i],
                 option_enabled(a, i));
        y += MENU_ROWH;
    }
    draw_chrome(a, "OPTIONS", "OK", "BACK");
    draw_status_msg(a);
}

/* ---- read view ---------------------------------------------------------- */

static void draw_text_lines(qeaf_code_app_t *a, const char *text, int len,
                            int top_line, int col_scroll, int rows,
                            int show_numbers, int lang)
{
    int line, y = EDIT_TOP + 2;
    int gutter = show_numbers ? 24 : 4;
    int cols = (QEAF_SCREEN_W - gutter) / QEAF_CELL_W;

    if(lang != QEAF_LANG_NONE) qeaf_hl_tokenize(text, lang, a->hl, QEAF_CODE_LEN);

    for(line = top_line; line < top_line + rows; line++) {
        int ls = line_start_at(text, len, line);
        int le = line_end_at(text, len, ls);
        int i;
        char buf[48];
        int p = 0;

        if(ls >= len && line > 0) break;
        if(show_numbers) {
            char num[12];
            int np = qeaf_append_int(num, (int)sizeof(num), 0, line + 1);
            (void)np;
            ui_text(a, 2, y, num, QEAF_UI_DIM);
        }
        for(i = ls + col_scroll; i < le && p < (int)sizeof(buf) - 1 &&
                                  p < cols; i++)
            buf[p++] = text[i];
        buf[p] = '\0';
        if(p > 0)
            ui_text(a, gutter, y, buf,
                    lang != QEAF_LANG_NONE ? (int)a->hl[ls + col_scroll]
                                           : QEAF_UI_TEXT);
        y += QEAF_LINE_H;
    }
}

static void draw_view(qeaf_code_app_t *a)
{
    const qeaf_doc_t *d = qeaf_store_doc(&a->store, a->view_idx);
    int lines, rows = (QEAF_SCREEN_H - QEAF_CHROME_H - 20) / QEAF_LINE_H;
    char title[QEAF_NAME_MAX + 4];
    int p = 0;

    if(d == NULL) { a->state = QEAF_APP_LIST; return; }
    p = qeaf_append_str(title, (int)sizeof(title), p, d->meta.name);
    (void)p;
    draw_chrome(a, title, "BACK", "EDIT");
    lines = text_lines(d->text, d->len);
    if(a->view_scroll > lines - rows) a->view_scroll = lines - rows;
    if(a->view_scroll < 0) a->view_scroll = 0;
    draw_text_lines(a, d->text, d->len, a->view_scroll, a->view_col, rows, 1,
                    a->store.syntax_hl ? qeaf_store_lang_for(d)
                                       : QEAF_LANG_NONE);
    {
        char info[32];
        int q = qeaf_append_str(info, (int)sizeof(info), 0, "LINE ");
        q = qeaf_append_int(info, (int)sizeof(info), q, a->view_scroll + 1);
        q = qeaf_append_str(info, (int)sizeof(info), q, "/");
        q = qeaf_append_int(info, (int)sizeof(info), q, lines);
        (void)q;
        ui_text(a, 4, QEAF_SCREEN_H - QEAF_STATUS_H - 12, info, QEAF_UI_DIM);
    }
}

/* ---- editor ------------------------------------------------------------- */

static void draw_edit(qeaf_code_app_t *a)
{
    const qeaf_doc_t *d = qeaf_store_doc(&a->store, a->edit_idx);
    const char *text = qeaf_buffer_text(&a->edit);
    int len = qeaf_buffer_len(&a->edit);
    int line, y = EDIT_TOP + 2;
    int caret_line, caret_col;
    char title[QEAF_NAME_MAX + 8];
    int p = 0;

    p = qeaf_append_str(title, (int)sizeof(title), 0,
                        d != NULL ? d->meta.name : "NEW");
    if(a->dirty) p = qeaf_append_str(title, (int)sizeof(title), p, " *");
    (void)p;
    draw_chrome(a, title, "MENU", "SAVE");

    qeaf_hl_tokenize(text, d != NULL ? qeaf_store_lang_for(d) : QEAF_LANG_C,
                     a->hl, QEAF_CODE_LEN);
    caret_line_col(&a->edit, &caret_line, &caret_col);

    for(line = a->edit_scroll; line < a->edit_scroll + EDIT_ROWS; line++) {
        int ls = line_start_at(text, len, line);
        int le = line_end_at(text, len, ls);
        int i, p2 = 0;
        char buf[48];

        if(ls >= len && line > 0) break;

        if(a->store.line_nums) {
            char num[12];
            int np = qeaf_append_int(num, (int)sizeof(num), 0, line + 1);
            (void)np;
            ui_text(a, 2, y, num, QEAF_UI_DIM);
        }

        if(qeaf_buffer_has_sel(&a->edit)) {
            int lo, hi, vlo, vhi;
            qeaf_buffer_sel_range(&a->edit, &lo, &hi);
            vlo = lo > ls + a->edit_col ? lo : ls + a->edit_col;
            vhi = hi < ls + a->edit_col + EDIT_COLS
                      ? hi : ls + a->edit_col + EDIT_COLS;
            if(vhi > vlo)
                ui_rect(a, 24 + (vlo - ls - a->edit_col) * QEAF_CELL_W,
                        y - 1, (vhi - vlo) * QEAF_CELL_W, QEAF_LINE_H,
                        QEAF_UI_SEL);
        }

        for(i = ls + a->edit_col; i < le && p2 < (int)sizeof(buf) - 1 &&
                                  p2 < EDIT_COLS; i++)
            buf[p2++] = text[i];
        buf[p2] = '\0';
        if(p2 > 0)
            ui_text(a, 24, y, buf,
                    a->store.syntax_hl ? (int)a->hl[ls + a->edit_col]
                                       : QEAF_UI_TEXT);

        if(line == caret_line && a->blink_on &&
           caret_col >= a->edit_col && caret_col < a->edit_col + EDIT_COLS)
            ui_rect(a, 24 + (caret_col - a->edit_col) * QEAF_CELL_W, y - 1,
                    1, QEAF_LINE_H, QEAF_UI_CARET);
        y += QEAF_LINE_H;
    }

    {
        char info[48];
        int q = qeaf_append_str(info, (int)sizeof(info), 0, "LN ");
        q = qeaf_append_int(info, (int)sizeof(info), q, caret_line + 1);
        q = qeaf_append_str(info, (int)sizeof(info), q, " COL ");
        q = qeaf_append_int(info, (int)sizeof(info), q, caret_col + 1);
        q = qeaf_append_str(info, (int)sizeof(info), q, "  ");
        q = qeaf_append_str(info, (int)sizeof(info), q,
                            d != NULL ? qeaf_ext_name(d->meta.ext) : "");
        if(edit_readonly(a))
            q = qeaf_append_str(info, (int)sizeof(info), q, "  RO");
        else if(a->dirty)
            q = qeaf_append_str(info, (int)sizeof(info), q, "  MOD");
        (void)q;
        ui_text(a, 4, QEAF_SCREEN_H - 30, info, QEAF_UI_DIM);
    }
    ui_text(a, 4, QEAF_SCREEN_H - 16, "UP/DOWN LINE  OK NEWLINE  CLEAR DEL",
            QEAF_UI_DIM);
}

/* ---- editor menu -------------------------------------------------------- */

static int editmenu_enabled(const qeaf_code_app_t *a, int i)
{
    int rw = !edit_readonly(a);
    switch(i) {
    case 0:  return 1;
    case 1:  return a->edit.undo_n > 0;
    case 2:  return rw && (qeaf_buffer_has_sel(&a->edit) ||
                           qeaf_buffer_len(&a->edit) > 0);
    case 3:  return qeaf_buffer_len(&a->edit) > 0;
    case 4:  return rw && a->edit.clip_len > 0;
    case 5:  return qeaf_buffer_len(&a->edit) > 0;
    case 6:  return rw && (qeaf_buffer_has_sel(&a->edit) ||
                           qeaf_buffer_cursor(&a->edit) <
                               qeaf_buffer_len(&a->edit));
    case 7:
    case 8:  return rw;
    default: return 1;
    }
}

static void draw_editmenu(qeaf_code_app_t *a)
{
    int i, y = 36;
    ui_rect(a, 0, 0, QEAF_SCREEN_W, QEAF_SCREEN_H, QEAF_UI_TEXT);
    ui_rect(a, 10, 20, QEAF_SCREEN_W - 20, MENU_ROWH * 13 + 16, QEAF_UI_DIM);
    ui_rect(a, 10, 20, QEAF_SCREEN_W - 20, 14, QEAF_UI_ACCENT);
    ui_text(a, 16, 23, "MENU", QEAF_UI_TITLE);
    for(i = 0; i < 13; i++) {
        draw_row(a, y, i == a->edit_menu_sel, editmenu_labels[i],
                 editmenu_enabled(a, i));
        y += MENU_ROWH;
    }
    draw_chrome(a, "MENU", "OK", "BACK");
    draw_status_msg(a);
}

/* ---- confirm / settings / name / symbol / find -------------------------- */

static void draw_delete(qeaf_code_app_t *a)
{
    const qeaf_doc_t *d = cur_doc(a);
    char line[64];
    int p = 0;
    ui_rect(a, 0, 0, QEAF_SCREEN_W, QEAF_SCREEN_H, QEAF_UI_TEXT);
    ui_rect(a, 16, 110, QEAF_SCREEN_W - 32, 70, QEAF_UI_DIM);
    ui_text(a, 24, 118, "DELETE FILE?", QEAF_UI_WARN);
    p = qeaf_append_str(line, (int)sizeof(line), 0,
                        d != NULL ? d->meta.name : "");
    (void)p;
    ui_text(a, 24, 134, line, QEAF_UI_TEXT);
    draw_row(a, 148, a->confirm_sel == 0, "No", 1);
    draw_row(a, 164, a->confirm_sel == 1, "Yes", 1);
    draw_chrome(a, "DELETE", "OK", "BACK");
}

static void draw_settings(qeaf_code_app_t *a)
{
    char lab[40];
    int i, y = 28;
    const char names[6][16] = {
        "Auto indent", "Indent width", "Syntax colours", "Line numbers",
        "Dark mode", "Back"
    };
    draw_chrome(a, "SETTINGS", "TOGGLE", "BACK");
    for(i = 0; i < 6; i++) {
        int p = qeaf_append_str(lab, (int)sizeof(lab), 0, names[i]);
        if(i == 0) p = qeaf_append_str(lab, (int)sizeof(lab), p,
                                       a->store.auto_indent ? ": ON" : ": OFF");
        else if(i == 1) {
            p = qeaf_append_str(lab, (int)sizeof(lab), p, ": ");
            p = qeaf_append_int(lab, (int)sizeof(lab), p,
                                a->store.indent_width);
        } else if(i == 2) p = qeaf_append_str(lab, (int)sizeof(lab), p,
                                       a->store.syntax_hl ? ": ON" : ": OFF");
        else if(i == 3) p = qeaf_append_str(lab, (int)sizeof(lab), p,
                                       a->store.line_nums ? ": ON" : ": OFF");
        else if(i == 4) p = qeaf_append_str(lab, (int)sizeof(lab), p,
                                       a->store.dark_mode ? ": ON" : ": OFF");
        (void)p;
        draw_row(a, y, i == a->settings_sel, lab, 1);
        y += 20;
    }
    draw_status_msg(a);
}

static void draw_name(qeaf_code_app_t *a)
{
    char lab[QEAF_NAME_MAX + 8];
    int p = 0;
    draw_chrome(a, a->name_editing ? "RENAME" : "NEW FILE", "OK", "BACK");
    ui_text(a, 8, 34, "NAME", QEAF_UI_DIM);
    p = qeaf_append_str(lab, (int)sizeof(lab), 0, a->name_base);
    p = qeaf_append_str(lab, (int)sizeof(lab), p,
                        qeaf_ext_name(a->name_ext));
    (void)p;
    ui_rect(a, 6, 48, QEAF_SCREEN_W - 12, 16, QEAF_UI_SEL);
    ui_text(a, 10, 52, lab, QEAF_UI_TITLE);
    ui_text(a, 8, 76, "TYPE A NAME", QEAF_UI_DIM);
    ui_text(a, 8, 92, "LEFT/RIGHT: TYPE", QEAF_UI_DIM);
    ui_text(a, 8, 108, "CLEAR: BACKSPACE", QEAF_UI_DIM);
    ui_text(a, 8, 124, "OK: SAVE", QEAF_UI_DIM);
    draw_status_msg(a);
}

static void draw_symbol(qeaf_code_app_t *a)
{
    const char *page = symbol_pages[a->sym_page % 3];
    int n = qeaf_strlen(page);
    int i, y = 44;
    draw_chrome(a, "SYMBOLS", "INSERT", "BACK");
    for(i = 0; i < n; i++) {
        char cell[2];
        int cx = 8 + (i % 8) * 28;
        int cy = y + (i / 8) * 28;
        cell[0] = page[i];
        cell[1] = '\0';
        if(i == a->sym_sel)
            ui_rect(a, cx - 4, cy - 4, 24, 24, QEAF_UI_SEL);
        ui_text(a, cx, cy, cell, QEAF_UI_TEXT);
    }
    ui_text(a, 8, 280, "PAGE 1/3  LEFT/RIGHT", QEAF_UI_DIM);
}

static void draw_find(qeaf_code_app_t *a)
{
    char lab[QEAF_NAME_MAX + 8];
    int p = qeaf_append_str(lab, (int)sizeof(lab), 0, "FIND: ");
    p = qeaf_append_str(lab, (int)sizeof(lab), p, a->find_query);
    (void)p;
    draw_chrome(a, "FIND", "FIND", "BACK");
    ui_rect(a, 6, 44, QEAF_SCREEN_W - 12, 16, QEAF_UI_SEL);
    ui_text(a, 10, 48, lab, QEAF_UI_TITLE);
    ui_text(a, 8, 72, "TYPE TO SEARCH", QEAF_UI_DIM);
    ui_text(a, 8, 88, "CLEAR: BACKSPACE", QEAF_UI_DIM);
    ui_text(a, 8, 104, "OK: NEXT MATCH", QEAF_UI_DIM);
    draw_status_msg(a);
}

static void draw_details(qeaf_code_app_t *a)
{
    const qeaf_doc_t *d = qeaf_store_doc(&a->store, a->detail_idx);
    char line[64];
    int p;
    if(d == NULL) { a->state = QEAF_APP_LIST; return; }
    draw_chrome(a, "DETAILS", "BACK", "");
    p = qeaf_append_str(line, (int)sizeof(line), 0, "NAME: ");
    p = qeaf_append_str(line, (int)sizeof(line), p, d->meta.name);
    (void)p;
    ui_text(a, 8, 30, line, QEAF_UI_TEXT);
    p = qeaf_append_str(line, (int)sizeof(line), 0, "SIZE: ");
    p = qeaf_append_int(line, (int)sizeof(line), p, d->meta.size);
    p = qeaf_append_str(line, (int)sizeof(line), p, " BYTES");
    (void)p;
    ui_text(a, 8, 46, line, QEAF_UI_TEXT);
    p = qeaf_append_str(line, (int)sizeof(line), 0, "LINES: ");
    p = qeaf_append_int(line, (int)sizeof(line), p,
                        text_lines(d->text, d->len));
    (void)p;
    ui_text(a, 8, 62, line, QEAF_UI_TEXT);
    p = qeaf_append_str(line, (int)sizeof(line), 0, "TYPE: ");
    p = qeaf_append_str(line, (int)sizeof(line), p,
                        qeaf_ext_name(d->meta.ext));
    (void)p;
    ui_text(a, 8, 78, line, QEAF_UI_TEXT);
    ui_text(a, 8, 94, d->meta.trunc ? "READ ONLY: TOO LARGE" : "EDITABLE",
            d->meta.trunc ? QEAF_UI_WARN : QEAF_UI_DIM);
    ui_text(a, 8, 118, "PATH", QEAF_UI_DIM);
    ui_text(a, 8, 132, d->meta.path, QEAF_UI_DIM);
}

static void draw_about(qeaf_code_app_t *a)
{
    const char lines[10][32] = {
        "CODE EDITOR",
        "QEAPP native port",
        "",
        "Source: MRE VXP 240x320",
        "Target: QEAPP v2 arm64",
        "",
        "Files, editor, undo,",
        "syntax colour, find,",
        "settings and export are",
        "ported. Native execution"
    };
    int i, y = 30;
    draw_chrome(a, "ABOUT", "BACK", "");
    for(i = a->about_scroll; i < 10 && y < QEAF_SCREEN_H - 20; i++) {
        ui_text(a, 8, y, lines[i], i == 0 ? QEAF_UI_ACCENT : QEAF_UI_TEXT);
        y += 12;
    }
    ui_text(a, 8, QEAF_SCREEN_H - 28, "is validation-only.", QEAF_UI_DIM);
}

/* ---- top level draw ----------------------------------------------------- */

void qeaf_app_draw(qeaf_code_app_t *a)
{
    ui_clear(a);
    switch(a->state) {
    case QEAF_APP_SPLASH:   draw_splash(a); break;
    case QEAF_APP_LIST:     draw_list(a); break;
    case QEAF_APP_OPTIONS:  draw_options(a); break;
    case QEAF_APP_VIEW:     draw_view(a); break;
    case QEAF_APP_EDIT:     draw_edit(a); break;
    case QEAF_APP_EDITMENU: draw_editmenu(a); break;
    case QEAF_APP_DELETE:   draw_delete(a); break;
    case QEAF_APP_SETTINGS: draw_settings(a); break;
    case QEAF_APP_NAME:     draw_name(a); break;
    case QEAF_APP_SYMBOL:   draw_symbol(a); break;
    case QEAF_APP_FIND:     draw_find(a); break;
    case QEAF_APP_DETAILS:  draw_details(a); break;
    case QEAF_APP_ABOUT:    draw_about(a); break;
    default: break;
    }
    ui_flush(a);
}

/* ---- lifecycle ---------------------------------------------------------- */

void qeaf_app_init(qeaf_code_app_t *a, const qeaf_store_io_t *io, void *io_ctx,
                   const char *dir)
{
    memset(a, 0, sizeof(*a));
    qeaf_store_init(&a->store, io, io_ctx, dir);
    qeaf_buffer_init(&a->edit, 4, 1);
    a->state = QEAF_APP_SPLASH;
    a->settings_parent = QEAF_APP_LIST;
    a->edit_idx = -1;
    a->name_slot = -1;
    a->name_ext = QEAF_EXT_C;
    a->blink_on = 1;
    a->splash_ticks = 0;
}

void qeaf_app_set_ui(qeaf_code_app_t *a, const qeaf_code_ui_t *ui, void *ctx)
{
    if(ui != NULL) a->ui = *ui;
    else memset(&a->ui, 0, sizeof(a->ui));
    a->ui_ctx = ctx;
}

void qeaf_app_start(qeaf_code_app_t *a)
{
    char msg[QEAF_STATUS_MAX];
    int n = qeaf_store_rescan(&a->store);
    int p;
    a->loaded = 1;
    a->state = QEAF_APP_SPLASH;
    a->splash_ticks = 0;
    a->list_sel = 0;
    a->list_top = 0;
    if(n < 0) {
        status_set(a, "STORAGE UNAVAILABLE");
        return;
    }
    p = qeaf_append_str(msg, (int)sizeof(msg), 0, "SCANNED ");
    p = qeaf_append_int(msg, (int)sizeof(msg), p, n);
    p = qeaf_append_str(msg, (int)sizeof(msg), p, " FILES");
    (void)p;
    status_set(a, msg);
}

void qeaf_app_tick(qeaf_code_app_t *a)
{
    if(a->splash_ticks < QEAF_SPLASH_TICKS) a->splash_ticks++;
    if(a->state == QEAF_APP_SPLASH && a->splash_ticks >= QEAF_SPLASH_TICKS)
        a->state = QEAF_APP_LIST;
    a->blink_ticks++;
    if((a->blink_ticks & 15) == 0) a->blink_on = !a->blink_on;
    if(a->status_ticks > 0) a->status_ticks--;
}

int qeaf_app_open_editor(qeaf_code_app_t *a, int index)
{
    const qeaf_doc_t *d = qeaf_store_doc(&a->store, index);
    if(d == NULL) {
        status_set(a, "NO FILE SELECTED");
        return 0;
    }
    a->edit_idx = index;
    qeaf_buffer_set(&a->edit, d->text);
    a->edit.indent_width = a->store.indent_width;
    a->edit.auto_indent = a->store.auto_indent;
    a->edit_scroll = 0;
    a->edit_col = 0;
    a->dirty = 0;
    a->state = QEAF_APP_EDIT;
    scroll_to_caret(a);
    return 1;
}

int qeaf_app_save(qeaf_code_app_t *a)
{
    if(a->edit_idx < 0) {
        status_set(a, "NO FILE");
        return 0;
    }
    if(edit_readonly(a)) {
        status_set(a, "READ ONLY");
        return 0;
    }
    if(qeaf_store_update(&a->store, a->edit_idx,
                         qeaf_buffer_text(&a->edit)) != 0) {
        status_set(a, "SAVE FAILED");
        return 0;
    }
    a->dirty = 0;
    status_set(a, "SAVED");
    return 1;
}

static void close_editor(qeaf_code_app_t *a)
{
    int idx = a->edit_idx;
    if(a->dirty) qeaf_app_save(a);
    a->state = QEAF_APP_LIST;
    if(idx >= 0 && idx < a->store.count) a->list_sel = idx;
    a->edit_idx = -1;
    a->dirty = 0;
}

/* ---- list actions ------------------------------------------------------- */

static void name_start(qeaf_code_app_t *a, int editing, int slot)
{
    const qeaf_doc_t *d = qeaf_store_doc(&a->store, slot);
    int n, i;

    a->name_editing = editing;
    a->name_slot = slot;
    a->name_base[0] = '\0';
    if(editing && d != NULL) {
        qeaf_copy_trunc(a->name_base, d->meta.name, QEAF_NAME_MAX);
        n = qeaf_strlen(a->name_base);
        for(i = 0; i < n; i++) {
            if(a->name_base[i] == '.') { a->name_base[i] = '\0'; break; }
        }
        a->name_ext = d->meta.ext;
    } else {
        a->name_ext = QEAF_EXT_C;
    }
    a->state = QEAF_APP_NAME;
}

static void name_commit(qeaf_code_app_t *a)
{
    int n;
    if(a->name_base[0] == '\0') {
        status_set(a, "NAME REQUIRED");
        return;
    }
    if(a->store.readonly) {
        status_set(a, "READ ONLY");
        return;
    }
    if(a->name_editing) {
        if(qeaf_store_rename(&a->store, a->name_slot, a->name_base,
                             a->name_ext) < 0) {
            status_set(a, "RENAME FAILED");
            return;
        }
        status_set(a, "RENAMED");
        a->state = QEAF_APP_LIST;
        a->list_sel = a->name_slot;
        return;
    }
    n = qeaf_store_add(&a->store, a->name_base, a->name_ext);
    if(n < 0) {
        status_set(a, "CREATE FAILED");
        return;
    }
    a->list_sel = n;
    status_set(a, "CREATED");
    qeaf_app_open_editor(a, n);
}

static void copy_doc_text(qeaf_code_app_t *a, int slot)
{
    const qeaf_doc_t *d = qeaf_store_doc(&a->store, slot);
    int n;
    if(d == NULL) return;
    n = d->len;
    if(n > QEAF_CLIP_MAX - 1) n = QEAF_CLIP_MAX - 1;
    memcpy(a->edit.clip, d->text, (size_t)n);
    a->edit.clip[n] = '\0';
    a->edit.clip_len = n;
    status_set(a, "COPIED");
}

static void paste_into_doc(qeaf_code_app_t *a, int slot)
{
    const qeaf_doc_t *d = qeaf_store_doc(&a->store, slot);
    char tmp[QEAF_CODE_LEN];
    int n, i, room;
    if(d == NULL) { status_set(a, "NO FILE SELECTED"); return; }
    if(a->edit.clip_len <= 0) { status_set(a, "CLIPBOARD EMPTY"); return; }
    if(d->meta.trunc) { status_set(a, "READ ONLY"); return; }
    n = d->len;
    room = QEAF_CODE_LEN - 1 - n;
    if(room <= 0) { status_set(a, "FILE FULL"); return; }
    memcpy(tmp, d->text, (size_t)n);
    for(i = 0; i < a->edit.clip_len && i < room; i++) tmp[n + i] = a->edit.clip[i];
    tmp[n + i] = '\0';
    if(qeaf_store_update(&a->store, slot, tmp) != 0) {
        status_set(a, "PASTE FAILED");
        return;
    }
    status_set(a, "PASTED");
}

static void option_apply(qeaf_code_app_t *a)
{
    int slot = cur_slot(a);
    switch(a->opt_sel) {
    case 0:
        if(slot >= 0) {
            a->view_idx = slot;
            a->view_scroll = 0;
            a->view_col = 0;
            a->state = QEAF_APP_VIEW;
        }
        break;
    case 1:
        if(slot >= 0) qeaf_app_open_editor(a, slot);
        break;
    case 2:
        name_start(a, 0, -1);
        break;
    case 3:
        status_set(a, "FOLDERS NOT SUPPORTED");
        a->state = QEAF_APP_LIST;
        break;
    case 4:
        if(slot >= 0) { a->detail_idx = slot; a->state = QEAF_APP_DETAILS; }
        break;
    case 5:
        if(slot >= 0) name_start(a, 1, slot);
        break;
    case 6:
        if(slot >= 0) {
            int n = qeaf_store_duplicate(&a->store, slot);
            if(n >= 0) { a->list_sel = n; status_set(a, "DUPLICATED"); }
            else status_set(a, a->store.readonly ? "READ ONLY" : "DUPLICATE FAILED");
            a->state = QEAF_APP_LIST;
        }
        break;
    case 7:
        a->settings_parent = QEAF_APP_LIST;
        a->settings_sel = 0;
        a->state = QEAF_APP_SETTINGS;
        break;
    case 8:
        if(slot >= 0) {
            char path[QEAF_PATH_MAX];
            if(qeaf_store_export(&a->store, slot, "EXPORT", path,
                                 QEAF_PATH_MAX) == 0)
                status_set(a, "COPY SAVED");
            else
                status_set(a, a->store.readonly ? "READ ONLY" : "EXPORT FAILED");
            a->state = QEAF_APP_LIST;
        }
        break;
    case 9:
        if(slot >= 0) { copy_doc_text(a, slot); a->state = QEAF_APP_LIST; }
        break;
    case 10:
        if(slot >= 0) { paste_into_doc(a, slot); a->state = QEAF_APP_LIST; }
        break;
    case 11:
        if(slot >= 0) { a->confirm_sel = 0; a->state = QEAF_APP_DELETE; }
        break;
    case 12:
        qeaf_store_rescan(&a->store);
        if(a->list_sel >= a->store.count) a->list_sel = a->store.count - 1;
        if(a->list_sel < 0) a->list_sel = 0;
        status_set(a, "RESCANNED");
        a->state = QEAF_APP_LIST;
        break;
    case 13:
        a->about_scroll = 0;
        a->state = QEAF_APP_ABOUT;
        break;
    default:
        break;
    }
}

static void editmenu_apply(qeaf_code_app_t *a)
{
    qeaf_buffer_t *b = &a->edit;
    int rw = !edit_readonly(a);
    switch(a->edit_menu_sel) {
    case 0:
        qeaf_app_save(a);
        a->state = QEAF_APP_EDIT;
        break;
    case 1:
        if(qeaf_buffer_undo(b)) { a->dirty = 1; scroll_to_caret(a); }
        a->state = QEAF_APP_EDIT;
        break;
    case 2:
        if(rw && qeaf_buffer_cut(b) > 0) a->dirty = 1;
        a->state = QEAF_APP_EDIT;
        break;
    case 3:
        qeaf_buffer_copy(b);
        a->state = QEAF_APP_EDIT;
        break;
    case 4:
        if(rw && qeaf_buffer_paste(b) > 0) { a->dirty = 1; scroll_to_caret(a); }
        a->state = QEAF_APP_EDIT;
        break;
    case 5:
        qeaf_buffer_select_all(b);
        a->state = QEAF_APP_EDIT;
        break;
    case 6:
        if(rw && qeaf_buffer_delete_selection(b)) {
            a->dirty = 1;
            scroll_to_caret(a);
        }
        a->state = QEAF_APP_EDIT;
        break;
    case 7:
        if(rw && qeaf_buffer_indent(b)) { a->dirty = 1; scroll_to_caret(a); }
        a->state = QEAF_APP_EDIT;
        break;
    case 8:
        if(rw && qeaf_buffer_outdent(b)) { a->dirty = 1; scroll_to_caret(a); }
        a->state = QEAF_APP_EDIT;
        break;
    case 9:
        a->sym_sel = 0;
        a->state = QEAF_APP_SYMBOL;
        break;
    case 10:
        a->find_query[0] = '\0';
        a->state = QEAF_APP_FIND;
        break;
    case 11:
        a->settings_parent = QEAF_APP_EDIT;
        a->settings_sel = 0;
        a->state = QEAF_APP_SETTINGS;
        break;
    case 12:
        status_set(a, "SYSTEM KEYBOARD PROVIDES TEXT");
        a->state = QEAF_APP_EDIT;
        break;
    default:
        break;
    }
}

static void settings_apply(qeaf_code_app_t *a)
{
    switch(a->settings_sel) {
    case 0:
        a->store.auto_indent = !a->store.auto_indent;
        a->edit.auto_indent = a->store.auto_indent;
        break;
    case 1:
        if(a->store.indent_width == 2) a->store.indent_width = 4;
        else if(a->store.indent_width == 4) a->store.indent_width = 8;
        else a->store.indent_width = 2;
        a->edit.indent_width = a->store.indent_width;
        break;
    case 2:
        a->store.syntax_hl = !a->store.syntax_hl;
        break;
    case 3:
        a->store.line_nums = !a->store.line_nums;
        break;
    case 4:
        a->store.dark_mode = !a->store.dark_mode;
        break;
    default:
        a->state = a->settings_parent;
        break;
    }
}

static void find_apply(qeaf_code_app_t *a)
{
    int at = qeaf_buffer_find(&a->edit, a->find_query);
    if(at < 0) {
        status_set(a, "NOT FOUND");
        return;
    }
    scroll_to_caret(a);
    status_set(a, "MATCH FOUND");
    a->state = QEAF_APP_EDIT;
}

/* ---- keys --------------------------------------------------------------- */

static void key_list(qeaf_code_app_t *a, qeaf_code_key_t key)
{
    int n = a->store.count;
    switch(key) {
    case QEAF_KEY_UP:
        if(n > 0) a->list_sel = (a->list_sel + n - 1) % n;
        break;
    case QEAF_KEY_DOWN:
        if(n > 0) a->list_sel = (a->list_sel + 1) % n;
        break;
    case QEAF_KEY_OK:
        a->opt_sel = 0;
        a->state = QEAF_APP_OPTIONS;
        break;
    case QEAF_KEY_SOFT_LEFT:
        if(cur_slot(a) >= 0) qeaf_app_open_editor(a, a->list_sel);
        break;
    case QEAF_KEY_SOFT_RIGHT:
        a->settings_parent = QEAF_APP_LIST;
        a->settings_sel = 0;
        a->state = QEAF_APP_SETTINGS;
        break;
    case QEAF_KEY_BACK:
        a->quit = 1;
        break;
    default:
        break;
    }
    if(a->list_sel < a->list_top) a->list_top = a->list_sel;
    if(a->list_sel >= a->list_top + LIST_ROWS)
        a->list_top = a->list_sel - LIST_ROWS + 1;
    if(a->list_top < 0) a->list_top = 0;
}

static void key_edit(qeaf_code_app_t *a, qeaf_code_key_t key)
{
    int rw = !edit_readonly(a);
    switch(key) {
    case QEAF_KEY_UP:
        qeaf_buffer_line_move(&a->edit, -1);
        scroll_to_caret(a);
        break;
    case QEAF_KEY_DOWN:
        qeaf_buffer_line_move(&a->edit, 1);
        scroll_to_caret(a);
        break;
    case QEAF_KEY_LEFT:
        qeaf_buffer_move(&a->edit, -1);
        scroll_to_caret(a);
        break;
    case QEAF_KEY_RIGHT:
        qeaf_buffer_move(&a->edit, 1);
        scroll_to_caret(a);
        break;
    case QEAF_KEY_OK:
        if(rw && qeaf_buffer_newline(&a->edit)) {
            a->dirty = 1;
            scroll_to_caret(a);
        }
        break;
    case QEAF_KEY_CLEAR:
        if(rw && qeaf_buffer_backspace(&a->edit)) {
            a->dirty = 1;
            scroll_to_caret(a);
        }
        break;
    case QEAF_KEY_MENU:
    case QEAF_KEY_SOFT_LEFT:
        a->edit_menu_sel = 0;
        a->state = QEAF_APP_EDITMENU;
        break;
    case QEAF_KEY_SOFT_RIGHT:
        qeaf_app_save(a);
        break;
    case QEAF_KEY_BACK:
        close_editor(a);
        break;
    default:
        break;
    }
}

static void key_name(qeaf_code_app_t *a, qeaf_code_key_t key)
{
    int n = qeaf_strlen(a->name_base);
    switch(key) {
    case QEAF_KEY_LEFT:
        a->name_ext = (a->name_ext + QEAF_EXT_N - 1) % QEAF_EXT_N;
        break;
    case QEAF_KEY_RIGHT:
        a->name_ext = (a->name_ext + 1) % QEAF_EXT_N;
        break;
    case QEAF_KEY_CLEAR:
        if(n > 0) a->name_base[n - 1] = '\0';
        break;
    case QEAF_KEY_OK:
    case QEAF_KEY_SOFT_LEFT:
        name_commit(a);
        break;
    case QEAF_KEY_BACK:
        a->state = QEAF_APP_LIST;
        break;
    default:
        break;
    }
}

static void key_symbol(qeaf_code_app_t *a, qeaf_code_key_t key)
{
    int n = qeaf_strlen(symbol_pages[a->sym_page % 3]);
    switch(key) {
    case QEAF_KEY_UP:
        if(a->sym_sel >= 8) a->sym_sel -= 8;
        break;
    case QEAF_KEY_DOWN:
        if(a->sym_sel + 8 < n) a->sym_sel += 8;
        break;
    case QEAF_KEY_LEFT:
        a->sym_page = (a->sym_page + 2) % 3;
        a->sym_sel = 0;
        break;
    case QEAF_KEY_RIGHT:
        a->sym_page = (a->sym_page + 1) % 3;
        a->sym_sel = 0;
        break;
    case QEAF_KEY_OK:
    case QEAF_KEY_SOFT_LEFT:
        if(!edit_readonly(a)) {
            char ch = symbol_pages[a->sym_page % 3][a->sym_sel];
            if(ch != '\0' && qeaf_buffer_insert_char(&a->edit, ch)) {
                a->dirty = 1;
                scroll_to_caret(a);
            }
        }
        a->state = QEAF_APP_EDIT;
        break;
    case QEAF_KEY_BACK:
        a->state = QEAF_APP_EDIT;
        break;
    default:
        break;
    }
}

void qeaf_app_key(qeaf_code_app_t *a, qeaf_code_key_t key)
{
    if(a->state == QEAF_APP_SPLASH) {
        a->splash_ticks = QEAF_SPLASH_TICKS;
        a->state = QEAF_APP_LIST;
        return;
    }

    switch(a->state) {
    case QEAF_APP_LIST:
        key_list(a, key);
        break;
    case QEAF_APP_OPTIONS:
        if(key == QEAF_KEY_UP) a->opt_sel = (a->opt_sel + 13) % 14;
        else if(key == QEAF_KEY_DOWN) a->opt_sel = (a->opt_sel + 1) % 14;
        else if(key == QEAF_KEY_OK || key == QEAF_KEY_SOFT_LEFT)
            option_apply(a);
        else if(key == QEAF_KEY_BACK || key == QEAF_KEY_SOFT_RIGHT)
            a->state = QEAF_APP_LIST;
        break;
    case QEAF_APP_VIEW: {
        const qeaf_doc_t *d = qeaf_store_doc(&a->store, a->view_idx);
        int lines = d != NULL ? text_lines(d->text, d->len) : 0;
        int rows = (QEAF_SCREEN_H - QEAF_CHROME_H - 20) / QEAF_LINE_H;
        if(key == QEAF_KEY_UP) a->view_scroll -= 2;
        else if(key == QEAF_KEY_DOWN) a->view_scroll += 2;
        else if(key == QEAF_KEY_LEFT) a->view_col--;
        else if(key == QEAF_KEY_RIGHT) a->view_col++;
        else if(key == QEAF_KEY_SOFT_RIGHT) qeaf_app_open_editor(a, a->view_idx);
        else if(key == QEAF_KEY_OK || key == QEAF_KEY_BACK ||
                key == QEAF_KEY_SOFT_LEFT)
            a->state = QEAF_APP_LIST;
        if(a->view_scroll > lines - rows) a->view_scroll = lines - rows;
        if(a->view_scroll < 0) a->view_scroll = 0;
        if(a->view_col < 0) a->view_col = 0;
        break;
    }
    case QEAF_APP_EDIT:
        key_edit(a, key);
        break;
    case QEAF_APP_EDITMENU:
        if(key == QEAF_KEY_UP) a->edit_menu_sel = (a->edit_menu_sel + 12) % 13;
        else if(key == QEAF_KEY_DOWN) a->edit_menu_sel = (a->edit_menu_sel + 1) % 13;
        else if(key == QEAF_KEY_OK || key == QEAF_KEY_SOFT_LEFT)
            editmenu_apply(a);
        else if(key == QEAF_KEY_BACK || key == QEAF_KEY_SOFT_RIGHT)
            a->state = QEAF_APP_EDIT;
        break;
    case QEAF_APP_DELETE:
        if(key == QEAF_KEY_UP || key == QEAF_KEY_DOWN)
            a->confirm_sel = (a->confirm_sel + 1) % 2;
        else if(key == QEAF_KEY_OK || key == QEAF_KEY_SOFT_LEFT) {
            if(a->confirm_sel == 1) {
                if(qeaf_store_delete(&a->store, a->list_sel) == 0)
                    status_set(a, "DELETED");
                else if(a->store.readonly)
                    status_set(a, "READ ONLY");
                if(a->list_sel >= a->store.count && a->list_sel > 0)
                    a->list_sel--;
                if(a->list_sel < 0) a->list_sel = 0;
            }
            a->state = QEAF_APP_LIST;
        } else if(key == QEAF_KEY_BACK || key == QEAF_KEY_SOFT_RIGHT)
            a->state = QEAF_APP_LIST;
        break;
    case QEAF_APP_SETTINGS:
        if(key == QEAF_KEY_UP) a->settings_sel = (a->settings_sel + 5) % 6;
        else if(key == QEAF_KEY_DOWN) a->settings_sel = (a->settings_sel + 1) % 6;
        else if(key == QEAF_KEY_OK || key == QEAF_KEY_SOFT_LEFT)
            settings_apply(a);
        else if(key == QEAF_KEY_BACK || key == QEAF_KEY_SOFT_RIGHT)
            a->state = a->settings_parent;
        break;
    case QEAF_APP_NAME:
        key_name(a, key);
        break;
    case QEAF_APP_SYMBOL:
        key_symbol(a, key);
        break;
    case QEAF_APP_FIND:
        if(key == QEAF_KEY_CLEAR) {
            int n = qeaf_strlen(a->find_query);
            if(n > 0) a->find_query[n - 1] = '\0';
        } else if(key == QEAF_KEY_OK || key == QEAF_KEY_SOFT_LEFT)
            find_apply(a);
        else if(key == QEAF_KEY_BACK || key == QEAF_KEY_SOFT_RIGHT)
            a->state = QEAF_APP_EDIT;
        break;
    case QEAF_APP_DETAILS:
        if(key == QEAF_KEY_BACK || key == QEAF_KEY_OK ||
           key == QEAF_KEY_SOFT_LEFT || key == QEAF_KEY_SOFT_RIGHT)
            a->state = QEAF_APP_LIST;
        break;
    case QEAF_APP_ABOUT:
        if(key == QEAF_KEY_UP) a->about_scroll -= 2;
        else if(key == QEAF_KEY_DOWN) a->about_scroll += 2;
        else if(key == QEAF_KEY_BACK || key == QEAF_KEY_OK ||
                key == QEAF_KEY_SOFT_LEFT || key == QEAF_KEY_SOFT_RIGHT)
            a->state = QEAF_APP_LIST;
        if(a->about_scroll < 0) a->about_scroll = 0;
        if(a->about_scroll > 6) a->about_scroll = 6;
        break;
    default:
        break;
    }
}

void qeaf_app_text(qeaf_code_app_t *a, const char *utf8)
{
    int i;
    if(utf8 == NULL) return;
    for(i = 0; utf8[i] != '\0'; i++) {
        unsigned char c = (unsigned char)utf8[i];
        if(c < 32 || c == 127) continue;
        if(a->state == QEAF_APP_EDIT) {
            if(edit_readonly(a)) {
                status_set(a, "READ ONLY");
                continue;
            }
            if(qeaf_buffer_insert_char(&a->edit, (char)c)) {
                a->dirty = 1;
                scroll_to_caret(a);
            }
        } else if(a->state == QEAF_APP_NAME) {
            int n = qeaf_strlen(a->name_base);
            char up = (char)c;
            if(up >= 'a' && up <= 'z') up = (char)(up - 32);
            if(up == ' ') up = '_';
            if(!((up >= 'A' && up <= 'Z') || (up >= '0' && up <= '9') ||
                 up == '_' || up == '-'))
                continue;
            if(n < QEAF_BASE_MAX) {
                a->name_base[n] = up;
                a->name_base[n + 1] = '\0';
            }
        } else if(a->state == QEAF_APP_FIND) {
            int n = qeaf_strlen(a->find_query);
            if(n < QEAF_NAME_MAX - 1) {
                a->find_query[n] = (char)c;
                a->find_query[n + 1] = '\0';
            }
        }
    }
}
