#include "qeaf_code_buffer.h"
#include "qeaf_code_util.h"

#include <string.h>

static void undo_reset(qeaf_buffer_t *b)
{
    b->undo_head = 0;
    b->undo_n = 0;
}

void qeaf_buffer_init(qeaf_buffer_t *b, int indent_width, int auto_indent)
{
    memset(b, 0, sizeof(*b));
    b->indent_width = indent_width > 0 ? indent_width : 4;
    if(b->indent_width > 8) b->indent_width = 8;
    b->auto_indent = auto_indent ? 1 : 0;
    b->sel_a = -1;
    b->sel_b = -1;
    b->pref_col = -1;
    undo_reset(b);
}

void qeaf_buffer_set(qeaf_buffer_t *b, const char *text)
{
    b->len = qeaf_copy_trunc(b->text, text != NULL ? text : "",
                             QEAF_CODE_LEN);
    b->cur = 0;
    b->sel_a = -1;
    b->sel_b = -1;
    b->pref_col = -1;
    b->pref_cur = 0;
    undo_reset(b);
}

const char *qeaf_buffer_text(const qeaf_buffer_t *b)
{
    return b->text;
}

int qeaf_buffer_len(const qeaf_buffer_t *b)
{
    return b->len;
}

int qeaf_buffer_cursor(const qeaf_buffer_t *b)
{
    return b->cur;
}

int qeaf_buffer_has_sel(const qeaf_buffer_t *b)
{
    return b->sel_a >= 0 && b->sel_b >= 0 && b->sel_a != b->sel_b;
}

void qeaf_buffer_sel_range(const qeaf_buffer_t *b, int *lo, int *hi)
{
    int a = b->sel_a, c = b->sel_b;
    if(a < 0 || c < 0) {
        *lo = b->cur;
        *hi = b->cur;
        return;
    }
    if(a > c) {
        int t = a;
        a = c;
        c = t;
    }
    *lo = a;
    *hi = c;
}

void qeaf_buffer_clear_sel(qeaf_buffer_t *b)
{
    b->sel_a = -1;
    b->sel_b = -1;
}

void qeaf_buffer_select_all(qeaf_buffer_t *b)
{
    if(b->len <= 0) return;
    b->sel_a = 0;
    b->sel_b = b->len;
    b->cur = b->len;
}

void qeaf_buffer_undo_push(qeaf_buffer_t *b)
{
    qeaf_copy_trunc(b->undo[b->undo_head].text, b->text, QEAF_CODE_LEN);
    b->undo[b->undo_head].len = b->len;
    b->undo[b->undo_head].cur = b->cur;
    b->undo_head = (b->undo_head + 1) % QEAF_UNDO_DEPTH;
    if(b->undo_n < QEAF_UNDO_DEPTH) b->undo_n++;
}

int qeaf_buffer_undo(qeaf_buffer_t *b)
{
    int slot;
    if(b->undo_n <= 0) return 0;
    b->undo_head = (b->undo_head - 1 + QEAF_UNDO_DEPTH) % QEAF_UNDO_DEPTH;
    b->undo_n--;
    slot = b->undo_head;
    qeaf_copy_trunc(b->text, b->undo[slot].text, QEAF_CODE_LEN);
    b->len = b->undo[slot].len;
    b->cur = b->undo[slot].cur;
    if(b->cur > b->len) b->cur = b->len;
    if(b->cur < 0) b->cur = 0;
    qeaf_buffer_clear_sel(b);
    b->pref_col = -1;
    return 1;
}

/* ---- primitives --------------------------------------------------------- */

static int field_put(qeaf_buffer_t *b, char c)
{
    int i;
    if(b->len >= QEAF_CODE_LEN - 1) return 0;
    for(i = b->len; i > b->cur; i--) b->text[i] = b->text[i - 1];
    b->text[b->cur] = c;
    b->len++;
    b->cur++;
    b->text[b->len] = '\0';
    return 1;
}

static int field_backspace(qeaf_buffer_t *b)
{
    int i;
    if(b->cur <= 0) return 0;
    for(i = b->cur - 1; i < b->len - 1; i++) b->text[i] = b->text[i + 1];
    b->len--;
    b->cur--;
    b->text[b->len] = '\0';
    return 1;
}

static int field_del_forward(qeaf_buffer_t *b)
{
    int i;
    if(b->cur >= b->len) return 0;
    for(i = b->cur; i < b->len - 1; i++) b->text[i] = b->text[i + 1];
    b->len--;
    b->text[b->len] = '\0';
    return 1;
}

static int field_cut_range(qeaf_buffer_t *b, int lo, int hi)
{
    int i;
    if(hi <= lo || lo < 0 || hi > b->len) return 0;
    for(i = lo; i < b->len - (hi - lo); i++) b->text[i] = b->text[i + (hi - lo)];
    b->len -= (hi - lo);
    b->text[b->len] = '\0';
    b->cur = lo;
    return 1;
}

static int remove_selection(qeaf_buffer_t *b)
{
    int lo, hi;
    if(!qeaf_buffer_has_sel(b)) return 0;
    qeaf_buffer_sel_range(b, &lo, &hi);
    field_cut_range(b, lo, hi);
    qeaf_buffer_clear_sel(b);
    return 1;
}

static int line_start_of(const char *s, int pos)
{
    int i = pos > 0 ? pos : 0;
    while(i > 0 && s[i - 1] != '\n') i--;
    return i;
}

static int line_end_of(const char *s, int len, int pos)
{
    int i = pos;
    while(i < len && s[i] != '\n') i++;
    return i;
}

/* ---- public editing ----------------------------------------------------- */

int qeaf_buffer_insert_char(qeaf_buffer_t *b, char c)
{
    qeaf_buffer_undo_push(b);
    remove_selection(b);
    b->pref_col = -1;
    return field_put(b, c);
}

int qeaf_buffer_insert_str(qeaf_buffer_t *b, const char *s)
{
    int i, n = 0;
    if(s == NULL || s[0] == '\0') return 0;
    qeaf_buffer_undo_push(b);
    remove_selection(b);
    for(i = 0; s[i] != '\0'; i++) {
        if(!field_put(b, s[i])) break;
        n++;
    }
    b->pref_col = -1;
    return n;
}

int qeaf_buffer_backspace(qeaf_buffer_t *b)
{
    qeaf_buffer_undo_push(b);
    b->pref_col = -1;
    if(remove_selection(b)) return 1;
    return field_backspace(b);
}

int qeaf_buffer_delete_forward(qeaf_buffer_t *b)
{
    qeaf_buffer_undo_push(b);
    b->pref_col = -1;
    if(remove_selection(b)) return 1;
    return field_del_forward(b);
}

int qeaf_buffer_delete_selection(qeaf_buffer_t *b)
{
    if(!qeaf_buffer_has_sel(b)) return qeaf_buffer_delete_forward(b);
    qeaf_buffer_undo_push(b);
    remove_selection(b);
    b->pref_col = -1;
    return 1;
}

int qeaf_buffer_move(qeaf_buffer_t *b, int delta)
{
    if(qeaf_buffer_has_sel(b)) {
        int lo, hi;
        qeaf_buffer_sel_range(b, &lo, &hi);
        b->cur = delta < 0 ? lo : hi;
        qeaf_buffer_clear_sel(b);
        b->pref_col = -1;
        return 1;
    }
    b->cur += delta;
    if(b->cur < 0) b->cur = 0;
    if(b->cur > b->len) b->cur = b->len;
    b->pref_col = -1;
    return 1;
}

int qeaf_buffer_line_move(qeaf_buffer_t *b, int dir)
{
    int ls, col, t, lend;
    if(qeaf_buffer_has_sel(b)) qeaf_buffer_clear_sel(b);
    ls = line_start_of(b->text, b->cur);
    col = (b->pref_col >= 0 && b->pref_cur == b->cur)
              ? b->pref_col : b->cur - ls;
    if(dir < 0) {
        if(ls == 0) {
            b->cur = 0;
            b->pref_col = 0;
            b->pref_cur = 0;
            return 1;
        }
        t = line_start_of(b->text, ls - 1);
    } else {
        int e = ls;
        while(e < b->len && b->text[e] != '\n') e++;
        if(e >= b->len) {
            b->cur = b->len;
            b->pref_col = -1;
            b->pref_cur = b->len;
            return 1;
        }
        t = e + 1;
    }
    lend = line_end_of(b->text, b->len, t);
    b->cur = t + col;
    if(b->cur > lend) b->cur = lend;
    b->pref_col = col;
    b->pref_cur = b->cur;
    return 1;
}

int qeaf_buffer_home(qeaf_buffer_t *b)
{
    int ls = line_start_of(b->text, b->cur);
    if(b->cur == ls && ls > 0) ls = line_start_of(b->text, ls - 1);
    b->cur = ls;
    qeaf_buffer_clear_sel(b);
    b->pref_col = -1;
    return 1;
}

int qeaf_buffer_end(qeaf_buffer_t *b)
{
    int p = b->cur;
    qeaf_buffer_clear_sel(b);
    while(p < b->len && b->text[p] != '\n') p++;
    b->cur = p;
    b->pref_col = -1;
    return 1;
}

int qeaf_buffer_newline(qeaf_buffer_t *b)
{
    int ls = line_start_of(b->text, b->cur);
    int ind = 0, i = ls, extra = 0, j = b->cur;

    qeaf_buffer_undo_push(b);
    remove_selection(b);
    while(i < b->len && (b->text[i] == ' ' || b->text[i] == '\t')) {
        ind++;
        i++;
    }
    while(j > ls && (b->text[j - 1] == ' ' || b->text[j - 1] == '\t')) j--;
    if(j > ls) {
        char s = b->text[j - 1];
        if(s == '{' || s == '(' || s == ':') extra = 1;
    }
    field_put(b, '\n');
    if(b->auto_indent) {
        for(i = 0; i < ind; i++) field_put(b, ' ');
        if(extra)
            for(i = 0; i < b->indent_width; i++) field_put(b, ' ');
    }
    b->pref_col = -1;
    return 1;
}

/* Collect hard-line starts intersecting [lo,hi], at most 24. */
static int line_starts(const qeaf_buffer_t *b, int *starts, int lo, int hi)
{
    int i = line_start_of(b->text, lo);
    int n = 0;
    while(i <= hi && n < 24) {
        starts[n++] = i;
        i = line_end_of(b->text, b->len, i);
        if(i >= b->len) break;
        i++;
    }
    return n;
}

int qeaf_buffer_indent(qeaf_buffer_t *b)
{
    int starts[24];
    int n, k, i, lo, hi, p = 0, prev = 0, shift = 0;
    char scratch[QEAF_CODE_LEN];

    if(qeaf_buffer_has_sel(b)) qeaf_buffer_sel_range(b, &lo, &hi);
    else { lo = b->cur; hi = b->cur; }
    n = line_starts(b, starts, lo, hi);
    if(n <= 0) return 0;
    for(k = 0; k < n; k++) {
        for(i = prev; i < starts[k] && p < QEAF_CODE_LEN - 1; i++)
            scratch[p++] = b->text[i];
        for(i = 0; i < b->indent_width && p < QEAF_CODE_LEN - 1; i++)
            scratch[p++] = ' ';
        if(starts[k] <= b->cur) shift += b->indent_width;
        prev = starts[k];
    }
    for(i = prev; i < b->len && p < QEAF_CODE_LEN - 1; i++)
        scratch[p++] = b->text[i];
    scratch[p] = '\0';

    qeaf_buffer_undo_push(b);
    qeaf_copy_trunc(b->text, scratch, QEAF_CODE_LEN);
    b->len = p;
    b->cur += shift;
    if(b->cur > b->len) b->cur = b->len;
    qeaf_buffer_clear_sel(b);
    b->pref_col = -1;
    return 1;
}

int qeaf_buffer_outdent(qeaf_buffer_t *b)
{
    int starts[24];
    int n, k, i, lo, hi, p = 0, prev = 0, shift = 0;
    char scratch[QEAF_CODE_LEN];

    if(qeaf_buffer_has_sel(b)) qeaf_buffer_sel_range(b, &lo, &hi);
    else { lo = b->cur; hi = b->cur; }
    n = line_starts(b, starts, lo, hi);
    if(n <= 0) return 0;
    for(k = 0; k < n; k++) {
        int skip = 0;
        for(i = prev; i < starts[k] && p < QEAF_CODE_LEN - 1; i++)
            scratch[p++] = b->text[i];
        i = starts[k];
        while(i < b->len && skip < b->indent_width && b->text[i] == ' ') {
            i++;
            skip++;
        }
        if(i < b->len && skip < b->indent_width && b->text[i] == '\t') {
            i++;
            skip++;
        }
        if(starts[k] <= b->cur && starts[k] < i) shift += skip;
        prev = i;
    }
    for(i = prev; i < b->len && p < QEAF_CODE_LEN - 1; i++)
        scratch[p++] = b->text[i];
    scratch[p] = '\0';

    qeaf_buffer_undo_push(b);
    qeaf_copy_trunc(b->text, scratch, QEAF_CODE_LEN);
    b->len = p;
    b->cur -= shift;
    if(b->cur < 0) b->cur = 0;
    if(b->cur > b->len) b->cur = b->len;
    qeaf_buffer_clear_sel(b);
    b->pref_col = -1;
    return 1;
}

int qeaf_buffer_copy(qeaf_buffer_t *b)
{
    int lo = 0, hi = b->len;
    if(qeaf_buffer_has_sel(b)) qeaf_buffer_sel_range(b, &lo, &hi);
    if(hi <= lo) return 0;
    b->clip_len = hi - lo;
    if(b->clip_len > QEAF_CLIP_MAX - 1) b->clip_len = QEAF_CLIP_MAX - 1;
    memcpy(b->clip, b->text + lo, (size_t)b->clip_len);
    b->clip[b->clip_len] = '\0';
    return b->clip_len;
}

int qeaf_buffer_cut(qeaf_buffer_t *b)
{
    int lo, hi, n;
    if(qeaf_buffer_has_sel(b)) qeaf_buffer_sel_range(b, &lo, &hi);
    else { lo = 0; hi = b->len; }
    n = hi - lo;
    if(n <= 0) return 0;
    b->clip_len = n;
    if(b->clip_len > QEAF_CLIP_MAX - 1) b->clip_len = QEAF_CLIP_MAX - 1;
    memcpy(b->clip, b->text + lo, (size_t)b->clip_len);
    b->clip[b->clip_len] = '\0';
    qeaf_buffer_undo_push(b);
    field_cut_range(b, lo, hi);
    qeaf_buffer_clear_sel(b);
    b->pref_col = -1;
    return n;
}

int qeaf_buffer_paste(qeaf_buffer_t *b)
{
    int i, n = b->clip_len;
    if(n <= 0) return 0;
    qeaf_buffer_undo_push(b);
    remove_selection(b);
    if(b->len + n > QEAF_CODE_LEN - 1) n = QEAF_CODE_LEN - 1 - b->len;
    for(i = 0; i < n; i++) field_put(b, b->clip[i]);
    b->pref_col = -1;
    return n;
}

int qeaf_buffer_find(qeaf_buffer_t *b, const char *query)
{
    int q = qeaf_strlen(query);
    int i, from = b->cur;
    if(q <= 0) return -1;
    for(i = from; i + q <= b->len; i++) {
        if(memcmp(b->text + i, query, (size_t)q) == 0) {
            b->sel_a = i;
            b->sel_b = i + q;
            b->cur = i + q;
            b->pref_col = -1;
            return i;
        }
    }
    for(i = 0; i + q <= b->len && i < from; i++) {
        if(memcmp(b->text + i, query, (size_t)q) == 0) {
            b->sel_a = i;
            b->sel_b = i + q;
            b->cur = i + q;
            b->pref_col = -1;
            return i;
        }
    }
    qeaf_buffer_clear_sel(b);
    return -1;
}

int qeaf_buffer_line_of(const qeaf_buffer_t *b, int pos)
{
    int line = 1, i;
    for(i = 0; i < pos && i < b->len; i++)
        if(b->text[i] == '\n') line++;
    return line;
}

int qeaf_buffer_column_of(const qeaf_buffer_t *b, int pos)
{
    int ls = line_start_of(b->text, pos);
    return pos - ls + 1;
}
