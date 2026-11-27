/* SPDX-License-Identifier: MIT */
/* vqeaf_ime.c - responsive independent input-method window (see vqeaf_ime.h).
 *
 * The key grid is built from weighted rows (vqeaf_weighted_row/vqeaf_weight) so
 * keys stretch proportionally with the window, exactly like layout_weight.
 * Split mode renders two half-grids separated by a centre gap; one-handed and
 * floating shrink/anchor the window; extract covers the screen with an expanded
 * editor. Showing the IME resizes the attached app content by H in sync with
 * the slide-up (adjustResize / imePadding).
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf_ime.h"
#include "vqeaf_theme.h"
#include <vqeaf/clipboard.h>
#include "vqeaf_pages.h"
#include "vqeaf_fonts.h"
#include "violet.h"
#include <vqeaf/settings.h>

/* ---- responsive metrics (pure) ------------------------------------------ */

#define IME_KEY_GAP_DP 6
#define IME_WINDOW_PAD_DP 6
#define IME_SUGGEST_H_DP 32

void vqeaf_ime_metrics(int32_t w, int32_t h, int32_t dpi,
                       vqeaf_ime_mode_t mode, vqeaf_ime_metrics_t *out)
{
    if(out == NULL || w <= 0 || h <= 0 || dpi <= 0) return;
    out->screen_w = w; out->screen_h = h; out->dpi = dpi;
    int32_t sw = (w < h ? w : h) * 160 / dpi;
    out->sw_dp = sw;
    int32_t land = w > h;
    out->landscape = land;
    int32_t number = (sw >= VQEAF_IME_SW600_DP) || land;
    out->number_row = number;
    int32_t rows = 4 + number;
    out->rows = rows;

    int32_t row_dp = sw >= VQEAF_IME_SW720_DP ? 60 :
                     (sw >= VQEAF_IME_SW600_DP ? 56 : 52);
    int32_t row_gap = IME_KEY_GAP_DP * dpi / 160;
    int32_t padding = IME_WINDOW_PAD_DP * dpi / 160;
    int32_t row_h = row_dp * dpi / 160;
    int32_t suggest_h = IME_SUGGEST_H_DP * dpi / 160;
    int32_t chrome_h = 2 * padding + suggest_h + row_gap;
    int32_t h_raw = rows * row_h + (rows - 1) * row_gap + chrome_h;
    int32_t minp = land ? VQEAF_IME_LANDSCAPE_MIN_PCT : VQEAF_IME_PORTRAIT_MIN_PCT;
    int32_t maxp = land ? VQEAF_IME_LANDSCAPE_MAX_PCT : VQEAF_IME_PORTRAIT_MAX_PCT;
    int32_t h_min = h * minp / 100, h_max = h * maxp / 100;
    int32_t H = h_raw;
    if(H < h_min) H = h_min;
    if(H > h_max) H = h_max;
    row_h = (H - (rows - 1) * row_gap - chrome_h) / rows;
    if(row_h < 1) row_h = 1;
    out->height = H;
    out->row_h = row_h;
    out->gap = 64 * dpi / 160;
    out->row_gap = row_gap;
    out->column_gap = row_gap;
    out->padding = padding;

    int32_t width = w, x = 0, y = h - H;
    switch(mode) {
    case VQEAF_IME_MODE_ONE_HAND_LEFT:
        width = w * VQEAF_IME_ONE_HAND_PCT / 100; x = 0; break;
    case VQEAF_IME_MODE_ONE_HAND_RIGHT:
        width = w * VQEAF_IME_ONE_HAND_PCT / 100; x = w - width; break;
    case VQEAF_IME_MODE_FLOATING:
        width = w * VQEAF_IME_FLOATING_PCT / 100;
        x = (w - width) / 2;
        y = h - H - 24 * dpi / 160;
        if(y < 0) y = 0;
        break;
    case VQEAF_IME_MODE_EXTRACT:
        width = w; H = h; x = 0; y = 0; out->height = H; break;
    case VQEAF_IME_MODE_SPLIT:
    case VQEAF_IME_MODE_NORMAL:
    default:
        break;
    }
    out->width = width; out->x = x; out->y = y;
    out->key_font = row_h >= 120 ? 34 :
                    (row_h >= 100 ? 28 : (row_h >= 56 ? 24 : 20));
}

/* ---- key model ----------------------------------------------------------- */

enum { IME_CHAR = 0, IME_SHIFT, IME_BACKSPACE, IME_ENTER, IME_MODE, IME_SPACE,
       IME_FILLER, IME_EMOJI, IME_EMOJI_MODE };

typedef struct {
    const char *label;
    int weight;      /* doubled so 1.5 -> 3 */
    int action;
    char ch;
    int side;        /* 0 both, 1 left, 2 right (split) */
} ime_key_t;

#define K(l, w, a, c, s) { l, w, a, c, s }

static const ime_key_t row_num[] = {
    K("1",2,IME_CHAR,'1',1), K("2",2,IME_CHAR,'2',1), K("3",2,IME_CHAR,'3',1),
    K("4",2,IME_CHAR,'4',1), K("5",2,IME_CHAR,'5',1), K("6",2,IME_CHAR,'6',2),
    K("7",2,IME_CHAR,'7',2), K("8",2,IME_CHAR,'8',2), K("9",2,IME_CHAR,'9',2),
    K("0",2,IME_CHAR,'0',2),
};
static const ime_key_t row1[] = {
    K("q",2,IME_CHAR,'q',1), K("w",2,IME_CHAR,'w',1), K("e",2,IME_CHAR,'e',1),
    K("r",2,IME_CHAR,'r',1), K("t",2,IME_CHAR,'t',1), K("y",2,IME_CHAR,'y',2),
    K("u",2,IME_CHAR,'u',2), K("i",2,IME_CHAR,'i',2), K("o",2,IME_CHAR,'o',2),
    K("p",2,IME_CHAR,'p',2),
};
static const ime_key_t row2[] = {
    K("",1,IME_FILLER,0,1),
    K("a",2,IME_CHAR,'a',1), K("s",2,IME_CHAR,'s',1), K("d",2,IME_CHAR,'d',1),
    K("f",2,IME_CHAR,'f',1), K("g",2,IME_CHAR,'g',1), K("h",2,IME_CHAR,'h',2),
    K("j",2,IME_CHAR,'j',2), K("k",2,IME_CHAR,'k',2), K("l",2,IME_CHAR,'l',2),
    K("",1,IME_FILLER,0,2),
};
static const ime_key_t row3[] = {
    K("SHIFT",3,IME_SHIFT,0,1),
    K("z",2,IME_CHAR,'z',1), K("x",2,IME_CHAR,'x',1), K("c",2,IME_CHAR,'c',1),
    K("v",2,IME_CHAR,'v',1), K("b",2,IME_CHAR,'b',2), K("n",2,IME_CHAR,'n',2),
    K("m",2,IME_CHAR,'m',2),
    K("DEL",3,IME_BACKSPACE,0,2),
};
static const ime_key_t row4[] = {
    K("?123",3,IME_MODE,0,1), K("\xF0\x9F\x98\x80",2,IME_EMOJI_MODE,0,1),
    K(",",2,IME_CHAR,',',1), K("SPACE",7,IME_SPACE,' ',0),
    K(".",2,IME_CHAR,'.',2), K("ENTER",3,IME_ENTER,'\n',2),
};
/* Symbol layout replaces the three letter rows when ?123 is active. */
static const ime_key_t sym1[] = {
    K("1",2,IME_CHAR,'1',1), K("2",2,IME_CHAR,'2',1), K("3",2,IME_CHAR,'3',1),
    K("4",2,IME_CHAR,'4',1), K("5",2,IME_CHAR,'5',1), K("6",2,IME_CHAR,'6',2),
    K("7",2,IME_CHAR,'7',2), K("8",2,IME_CHAR,'8',2), K("9",2,IME_CHAR,'9',2),
    K("0",2,IME_CHAR,'0',2),
};
static const ime_key_t sym2[] = {
    K("@",2,IME_CHAR,'@',1), K("#",2,IME_CHAR,'#',1), K("$",2,IME_CHAR,'$',1),
    K("_",2,IME_CHAR,'_',1), K("&",2,IME_CHAR,'&',1), K("-",2,IME_CHAR,'-',2),
    K("+",2,IME_CHAR,'+',2), K("(",2,IME_CHAR,'(',2), K(")",2,IME_CHAR,')',2),
    K("/",2,IME_CHAR,'/',2),
};
static const ime_key_t sym3[] = {
    K("ABC",3,IME_MODE,0,1), K("\xF0\x9F\x98\x80",2,IME_EMOJI_MODE,0,1),
    K("*",2,IME_CHAR,'*',1), K("\"",2,IME_CHAR,'"',1), K("'",2,IME_CHAR,'\'',1),
    K(":",2,IME_CHAR,':',1), K(";",2,IME_CHAR,';',2), K("!",2,IME_CHAR,'!',2),
    K("?",2,IME_CHAR,'?',2),
    K("DEL",3,IME_BACKSPACE,0,2),
};

/* ---- emoji layout ---------------------------------------------------------
 * Real emoji glyphs (from vqeaf_emoji_28, rasterised from Segoe UI Emoji).
 * `ch` packs the index into this table; the label is the UTF-8 sequence so it
 * renders with the emoji font. lv_textarea stores the same UTF-8 bytes. */
static const char *const emoji_table[] = {
    "\xF0\x9F\x98\x80","\xF0\x9F\x98\x83","\xF0\x9F\x98\x84","\xF0\x9F\x98\x81",
    "\xF0\x9F\x98\x86","\xF0\x9F\x98\x85","\xF0\x9F\x98\x82","\xF0\x9F\xA4\xA3",
    "\xF0\x9F\x98\x8A","\xF0\x9F\x98\x87","\xF0\x9F\x99\x82","\xF0\x9F\x99\x83",
    "\xF0\x9F\x98\x89","\xF0\x9F\x98\x8D","\xF0\x9F\x98\x98","\xF0\x9F\x98\x9A",
    "\xF0\x9F\x98\x9C","\xF0\x9F\x98\xAD","\xF0\x9F\x98\xB1","\xF0\x9F\x98\xA0",
    "\xF0\x9F\x98\xA1","\xF0\x9F\x98\x8E","\xF0\x9F\xA4\x93","\xF0\x9F\x98\x8F",
    "\xF0\x9F\x98\xB4","\xF0\x9F\x98\xB5","\xF0\x9F\x98\x90","\xF0\x9F\x98\x91",
    "\xF0\x9F\x98\x96","\xF0\x9F\x98\x8B","\xF0\x9F\x98\x9B","\xF0\x9F\x98\xAC",
    "\xF0\x9F\xA4\x94","\xF0\x9F\x98\xA8","\xF0\x9F\x98\xA9","\xF0\x9F\x98\xAE",
    "\xF0\x9F\x98\xAB","\xF0\x9F\x98\xAF","\xF0\x9F\x98\x8C","\xE2\x9D\xA4",
    "\xF0\x9F\x99\x9B","\xF0\x9F\x92\x9B","\xF0\x9F\x92\x9A","\xF0\x9F\x92\x99",
    "\xF0\x9F\x92\x9C","\xF0\x9F\x92\xA5","\xF0\x9F\x92\x94","\xF0\x9F\x92\x95",
    "\xF0\x9F\x92\x9E","\xF0\x9F\x92\x93",
};
#define EMOJI_COUNT ((int)(sizeof(emoji_table)/sizeof(emoji_table[0])))

/* Bottom control row of the emoji panel: back to letters, space, delete. */
static const ime_key_t emo_ctrl[] = {
    K("ABC",3,IME_MODE,0,1), K("SPACE",8,IME_SPACE,' ',0),
    K("DEL",3,IME_BACKSPACE,0,2),
};

/* Three emoji rows (10 + 10 + 10). Buttons use IME_EMOJI with ch = index. */
static ime_key_t emo_rows[3][10];
static int emo_rows_built;
static void emoji_init_rows(void)
{
    if(emo_rows_built) return;
    for(int r = 0; r < 3; r++)
        for(int c = 0; c < 10; c++) {
            int idx = r * 10 + c;
            emo_rows[r][c] = (ime_key_t){ emoji_table[idx % EMOJI_COUNT], 2,
                                           IME_EMOJI, (char)idx, 0 };
        }
    emo_rows_built = 1;
}

/* ---- state --------------------------------------------------------------- */

static lv_obj_t *g_win;          /* the independent IME window on top layer */
static lv_obj_t *g_suggest;      /* deterministic local candidate strip */
static lv_obj_t *g_grid;         /* key grid container inside the window */
static lv_obj_t *g_target;       /* bound textarea */
static lv_obj_t *g_content;      /* app content to resize (adjustResize) */
static int32_t g_content_base_h;
static lv_obj_t *g_extract_area; /* extract-mode expanded editor */
static vqeaf_ime_mode_t g_mode = VQEAF_IME_MODE_NORMAL;
static int g_shift;
static int g_symbols;
static int g_emoji;
static int g_visible;
static vqeaf_ime_metrics_t g_m;
static lv_point_t g_drag_pt;
static int g_key_font;

static const lv_font_t *ime_font(int px)
{
    if(px >= 34) return &vqeaf_font_34;
    if(px >= 28) return &vqeaf_font_28;
    if(px >= 24) return &vqeaf_font_24;
    return &vqeaf_font_20;
}

static const lv_font_t *ime_key_face(int px)
{
    if(px >= 34) return &vqeaf_font_bold_34;
    if(px >= 28) return &vqeaf_font_bold_28;
    if(px >= 24) return &vqeaf_font_bold_24;
    return &vqeaf_font_bold_20;
}

static lv_color_t ime_key_color(void)
{
    return lv_color_hex(vqeaf_pages_kb_theme_key_color(
        vqeaf_pages_kb_theme_applied()));
}

static lv_color_t ime_panel_color(void)
{
    return lv_color_hex(vqeaf_pages_kb_theme_bg_color(
        vqeaf_pages_kb_theme_applied()));
}

static lv_color_t ime_accent_color(void)
{
    return lv_color_hex(vqeaf_pages_kb_theme_accent_color(
        vqeaf_pages_kb_theme_applied()));
}

/* Keep captions readable on pastel themes and dark themes alike. The OS
 * keyboard themes are runtime colours, so a fixed light text colour would
 * disappear on the light "Pastel" preset. */
static lv_color_t ime_contrast_color(lv_color_t bg)
{
    lv_color32_t c = lv_color_to_32(bg, LV_OPA_COVER);
    uint32_t lum = (c.red * 299u + c.green * 587u + c.blue * 114u) / 1000u;
    return lum > 128u ? lv_color_hex(0x101418) : lv_color_hex(0xEAF7F5);
}

static lv_color_t ime_action_key_color(lv_color_t base, const ime_key_t *k,
                                       int active)
{
    lv_color_t accent = ime_accent_color();
    if(k->action == IME_SHIFT)
        return active ? lv_color_lighten(accent, 12) : accent;
    if(k->action == IME_BACKSPACE)
        return lv_color_mix(accent, lv_color_hex(0xD95D4F), 68);
    if(k->action == IME_ENTER)
        return lv_color_mix(accent, lv_color_hex(0xE7744F), 42);
    if(k->action == IME_EMOJI_MODE)
        return lv_color_mix(base, accent, 24);
    if(k->action == IME_SPACE)
        return lv_color_mix(base, ime_panel_color(), 28);
    lv_color32_t c = lv_color_to_32(base, LV_OPA_COVER);
    uint32_t lum = (c.red * 299u + c.green * 587u + c.blue * 114u) / 1000u;
    return lum > 128u ? lv_color_darken(base, 12) :
                        lv_color_lighten(base, 18);
}

static const char *ime_key_icon(const ime_key_t *k)
{
    switch(k->action) {
    case IME_SHIFT:
        return VQEAF_SYM_ARROW_UPWARD;
    case IME_BACKSPACE: return VQEAF_SYM_BACKSPACE;
    case IME_ENTER:     return VQEAF_SYM_KEYBOARD_RETURN;
    /* Text labels are clearer on the wide bottom-row mode and space keys. */
    case IME_MODE:
    case IME_SPACE:     return NULL;
    default:            return NULL;
    }
}

static const char *ime_number_hint(const ime_key_t *k)
{
    static const char *const hints[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"
    };
    static const char letters[] = "qwertyuiop";
    if(k->action != IME_CHAR) return NULL;
    const char *found = strchr(letters, k->ch);
    return found != NULL ? hints[found - letters] : NULL;
}

/* ---- key grid ------------------------------------------------------------ */

static void ime_key_cb(lv_event_t *event);

/* When the bound editor is destroyed (page rebuild, page switch, modal), the
 * IME must forget it immediately. Without this hook a suggestion refresh or
 * keystroke dereferences freed LVGL memory - the 2026-11-26 SIGSEGV inside
 * ime_current_word -> lv_label_get_text came from exactly this stale pointer
 * while the terminal page was being rebuilt under an open keyboard. */
static void ime_target_delete_cb(lv_event_t *event)
{
    if(lv_event_get_target(event) != g_target) return;
    g_target = NULL;
    if(g_visible) vqeaf_ime_hide();
}

/* Every g_target use funnels through this: a stale (deleted) pointer is
 * cleared so the call site falls back to the "no editor" path instead of
 * dereferencing freed memory. */
static lv_obj_t *ime_target_valid(void)
{
    if(g_target == NULL) return NULL;
    if(!lv_obj_is_valid(g_target)) {
        g_target = NULL;
        return NULL;
    }
    return g_target;
}

/* A page switch or a modal cleanup can delete the whole top layer, IME
 * included. Without this hook the cached window/grid pointers dangle and the
 * next focus event resurrects a deleted object (a crash later in the gate). */
static void ime_delete_cb(lv_event_t *event)
{
    if(lv_event_get_target(event) != g_win) return;
    g_win = NULL;
    g_suggest = NULL;
    g_grid = NULL;
    g_extract_area = NULL;
    g_target = NULL;
    g_content = NULL;
    g_visible = 0;
}

/* The IME must never steal input focus from the bound editor (standard OS
 * FLAG_NOT_FOCUSABLE). Every lv_obj is CLICK_FOCUSABLE by default, so a pointer
 * press on a key would move focus off the textarea; the shell hides the IME on
 * DEFOCUSED, which made the keyboard close on the first keystroke. */
static void ime_strip_focus(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    uint32_t n = lv_obj_get_child_count(obj);
    for(uint32_t i = 0; i < n; i++) ime_strip_focus(lv_obj_get_child(obj, i));
}

/* ---- local suggestions ---------------------------------------------------- */

static const char *const ime_words[] = {
    "the", "and", "for", "with", "this", "that", "have", "from",
    "friday", "fridays", "friend", "shipping", "ship", "checkout",
    "business", "customer", "customers", "address", "billing", "plan",
    "meeting", "notes", "note", "today", "tomorrow", "week", "work",
    "tôi", "và", "của", "là", "có", "không", "ngày", "mai", "hôm", "nay",
    "tuần", "cuộc", "họp", "ghi", "chú", "việc", "cần", "làm"
};

static int ime_prefix_match(const char *word, const char *candidate)
{
    while(*word != '\0' && *candidate != '\0') {
        if(tolower((unsigned char)*word) !=
           tolower((unsigned char)*candidate)) return 0;
        word++; candidate++;
    }
    return *word == '\0';
}

static void ime_current_word(char *out, size_t out_size, uint32_t *start,
                             uint32_t *length)
{
    out[0] = '\0';
    *start = 0; *length = 0;
    if(out_size == 0) return;
    lv_obj_t *target = ime_target_valid();
    if(target == NULL) return;
    const char *text = lv_textarea_get_text(target);
    if(text == NULL) return;
    uint32_t pos = lv_textarea_get_cursor_pos(target);
    size_t text_len = strlen(text);
    if(pos > text_len) pos = (uint32_t)text_len;
    uint32_t begin = pos;
    while(begin > 0) {
        unsigned char c = (unsigned char)text[begin - 1];
        if(!isalnum(c) && c < 0x80) break;
        begin--;
    }
    size_t len = pos - begin;
    if(len >= out_size) len = out_size - 1;
    memcpy(out, text + begin, len);
    out[len] = '\0';
    *start = begin;
    *length = (uint32_t)len;
}

static void ime_replace_word(const char *candidate)
{
    lv_obj_t *target = ime_target_valid();
    if(target == NULL || candidate == NULL) return;
    char word[48];
    uint32_t start, word_len;
    ime_current_word(word, sizeof(word), &start, &word_len);
    const char *text = lv_textarea_get_text(target);
    if(text == NULL) return;
    size_t text_len = strlen(text);
    size_t replacement_len = strlen(candidate);
    if(replacement_len > SIZE_MAX - text_len - 1) return;
    size_t out_len = text_len - word_len + replacement_len;
    char *out = malloc(out_len + 1);
    if(out == NULL) return;
    memcpy(out, text, start);
    memcpy(out + start, candidate, replacement_len);
    memcpy(out + start + replacement_len, text + start + word_len,
           text_len - start - word_len + 1);
    lv_textarea_set_text(target, out);
    lv_textarea_set_cursor_pos(target, start + (uint32_t)replacement_len);
    free(out);
}

static void ime_rebuild_async(void *user_data);
static void ime_build_suggestions(void);

static void ime_suggest_cb(lv_event_t *event)
{
    const char *candidate = (const char *)lv_event_get_user_data(event);
    ime_replace_word(candidate);
    ime_build_suggestions();
}

static void ime_clipboard_cb(lv_event_t *event)
{
    lv_obj_t *target = ime_target_valid();
    if(target == NULL) return;
    intptr_t action = (intptr_t)lv_event_get_user_data(event);
    if(action == 1) {
        const char *text = lv_textarea_get_text(target);
        if(text != NULL)
            (void)vqeaf_clipboard_set(text, strlen(text));
    } else if(action == 2 && vqeaf_clipboard_has_text()) {
        const char *text = vqeaf_clipboard_peek(NULL);
        if(text != NULL) lv_textarea_add_text(target, text);
        ime_build_suggestions();
    }
}

static void ime_build_suggestions(void)
{
    if(g_suggest == NULL) return;
    lv_obj_clean(g_suggest);
    lv_color_t panel = ime_panel_color();
    lv_color_t fg = ime_contrast_color(panel);
    lv_obj_set_style_bg_opa(g_suggest, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(g_suggest, 0, 0);
    lv_obj_set_style_pad_all(g_suggest, 0, 0);
    lv_obj_set_style_pad_column(g_suggest, 2, 0);
    lv_obj_set_flex_flow(g_suggest, LV_FLEX_FLOW_ROW);

    char word[48];
    uint32_t start, length;
    ime_current_word(word, sizeof(word), &start, &length);
    const char *chosen[3] = { NULL, NULL, NULL };
    int found = 0;
    (void)start;
    (void)length;
    if(word[0] == '\0') {
        chosen[0] = "tôi"; chosen[1] = "và"; chosen[2] = "có";
        found = 3;
    } else {
        for(size_t i = 0; i < sizeof(ime_words) / sizeof(ime_words[0]) && found < 3; i++) {
            if(ime_prefix_match(word, ime_words[i])) chosen[found++] = ime_words[i];
        }
    }

    for(int i = 0; i < 3; i++) {
        lv_obj_t *key = vqeaf_obj(g_suggest);
        vqeaf_weight(key, 3);
        lv_obj_set_height(key, lv_pct(100));
        lv_obj_set_style_radius(key, 12, 0);
        lv_obj_set_style_bg_opa(key, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(key, lv_color_mix(panel, ime_accent_color(), 20),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, LV_STATE_PRESSED);
        const char *label = chosen[i] != NULL ? chosen[i] : (i == 0 ? "—" : "");
        lv_color_t text_color = chosen[i] != NULL ? fg :
                                lv_color_mix(fg, panel, 55);
        lv_obj_t *text = vqeaf_label(key, label, ime_font(24), text_color);
        lv_obj_center(text);
        if(chosen[i] != NULL) {
            lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(key, ime_suggest_cb, LV_EVENT_CLICKED,
                                (void *)chosen[i]);
        } else {
            lv_obj_remove_flag(key, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    const char *clip_labels[2] = { VQEAF_SYM_CONTENT_COPY,
                                   VQEAF_SYM_CONTENT_PASTE };
    for(int i = 0; i < 2; i++) {
        lv_obj_t *action = vqeaf_obj(g_suggest);
        vqeaf_weight(action, 1);
        lv_obj_set_height(action, lv_pct(100));
        lv_obj_set_style_radius(action, 12, 0);
        lv_obj_set_style_bg_color(action, ime_key_color(), 0);
        lv_obj_set_style_bg_opa(action, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(action, ime_accent_color(), LV_STATE_PRESSED);
        lv_obj_add_flag(action, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *caption = vqeaf_glyph(action, clip_labels[i], VQEAF_ICON_SM, fg);
        lv_obj_center(caption);
        lv_obj_add_event_cb(action, ime_clipboard_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)(i + 1));
    }
}

static void ime_build_row(lv_obj_t *panel, const ime_key_t *keys, int n, int side)
{
    lv_obj_t *row = vqeaf_weighted_row(panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, g_m.row_h);
    lv_obj_set_style_pad_column(row, g_m.column_gap, 0);
    for(int i = 0; i < n; i++) {
        const ime_key_t *k = &keys[i];
        if(side != 0 && k->side != 0 && k->side != side) continue;
        lv_obj_t *key = vqeaf_obj(row);
        vqeaf_weight(key, k->weight);
        lv_obj_set_height(key, lv_pct(100));
        lv_obj_set_style_radius(key, g_m.row_h >= 80 ? 20 : 16, 0);
        if(k->action == IME_FILLER) {
            lv_obj_set_style_bg_opa(key, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(key, 0, 0);
            lv_obj_remove_flag(key, LV_OBJ_FLAG_CLICKABLE);
            continue;
        }
        lv_color_t base = ime_key_color();
        const char *icon = ime_key_icon(k);
        int active_shift = k->action == IME_SHIFT && g_shift;
        int special = k->action == IME_SHIFT || k->action == IME_BACKSPACE ||
                      k->action == IME_ENTER || k->action == IME_EMOJI_MODE;
        lv_color_t key_bg = special ? ime_action_key_color(base, k, active_shift) :
                          (k->action == IME_SPACE ?
                           ime_action_key_color(base, k, 0) : base);
        lv_obj_set_style_bg_color(key, key_bg, 0);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(key, lv_color_mix(key_bg, ime_accent_color(), 46),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_border_width(key, 1, 0);
        lv_obj_set_style_border_color(key, special ? key_bg :
                                      lv_color_mix(base, ime_accent_color(), 28), 0);
        lv_obj_set_style_border_opa(key, special ? 120 : 64, 0);
        lv_obj_set_style_shadow_width(key, special ? 10 : 6, 0);
        lv_obj_set_style_shadow_ofs_y(key, 4, 0);
        lv_obj_set_style_shadow_color(key, special ? key_bg : lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(key, special ? 42 : 58, 0);
        lv_obj_set_style_shadow_width(key, 2, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_ofs_y(key, 1, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(key, 4, 0);
        lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
        char label[8];
        if(k->action == IME_CHAR && k->ch >= 'a' && k->ch <= 'z' && g_shift) {
            label[0] = (char)(k->ch - 'a' + 'A'); label[1] = 0;
        } else {
            snprintf(label, sizeof(label), "%s", k->label);
        }
        lv_color_t fg = ime_contrast_color(key_bg);
        if(icon != NULL) {
            lv_obj_t *ic = vqeaf_glyph(key, icon, VQEAF_ICON_SM, fg);
            lv_obj_center(ic);
        } else {
            const lv_font_t *face = ime_key_face(g_key_font);
            if(k->action == IME_EMOJI || k->action == IME_EMOJI_MODE)
                face = &vqeaf_emoji_28;
            if(k->action == IME_SPACE)
                face = ime_font(20);
            lv_obj_t *l = vqeaf_label(key, label, face,
                                      k->action == IME_SPACE ?
                                      lv_color_mix(fg, key_bg, 28) : fg);
            const char *hint = ime_number_hint(k);
            if(hint != NULL) lv_obj_align(l, LV_ALIGN_CENTER, 0, 6);
            else lv_obj_center(l);
            if(hint != NULL) {
                lv_obj_t *number = vqeaf_label(key, hint, ime_font(20),
                                               lv_color_mix(fg, key_bg, 42));
                lv_obj_align(number, LV_ALIGN_TOP_RIGHT, -7, -5);
            }
        }
        lv_obj_add_event_cb(key, ime_key_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)((k->action << 8) |
                                               (unsigned char)k->ch));
    }
}

static void ime_build_grid(void)
{
    if(g_grid == NULL) return;
    ime_build_suggestions();
    lv_obj_clean(g_grid);
    int split = g_mode == VQEAF_IME_MODE_SPLIT;
    /* Narrow windows keep a readable face without crowding weighted keys. */
    g_key_font = (split || g_m.width < 500) ? 24 : g_m.key_font;
    lv_obj_t *left = g_grid, *right = NULL;
    if(split) {
        lv_obj_set_flex_flow(g_grid, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(g_grid, g_m.gap, 0);
        left = vqeaf_column(g_grid);
        right = vqeaf_column(g_grid);
        int32_t panel_w = (g_m.width - 2 * g_m.padding - g_m.gap) / 2;
        lv_obj_set_width(left, panel_w);
        lv_obj_set_width(right, panel_w);
        lv_obj_set_style_pad_row(left, g_m.row_gap, 0);
        lv_obj_set_style_pad_row(right, g_m.row_gap, 0);
    } else {
        lv_obj_set_flex_flow(g_grid, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(g_grid, g_m.row_gap, 0);
    }
    int side_l = split ? 1 : 0, side_r = split ? 2 : 0;
    if(g_emoji) {
        emoji_init_rows();
        for(int r = 0; r < 3; r++) {
            ime_build_row(left, emo_rows[r], 10, side_l);
            if(split) ime_build_row(right, emo_rows[r], 10, side_r);
        }
        ime_build_row(left, emo_ctrl, 3, side_l);
        if(split) ime_build_row(right, emo_ctrl, 3, side_r);
        if(g_win != NULL) ime_strip_focus(g_win);
        return;
    }
    if(g_m.number_row && !g_symbols) {
        ime_build_row(left, row_num, 10, side_l);
        if(split) ime_build_row(right, row_num, 10, side_r);
    }
    if(g_symbols) {
        ime_build_row(left, sym1, 10, side_l);
        ime_build_row(left, sym2, 10, side_l);
        ime_build_row(left, sym3, 10, side_l);
        if(split) {
            ime_build_row(right, sym1, 10, side_r);
            ime_build_row(right, sym2, 10, side_r);
            ime_build_row(right, sym3, 10, side_r);
        }
    } else {
        ime_build_row(left, row1, 10, side_l);
        ime_build_row(left, row2, 11, side_l);
        ime_build_row(left, row3, 9, side_l);
        if(split) {
            ime_build_row(right, row1, 10, side_r);
            ime_build_row(right, row2, 11, side_r);
            ime_build_row(right, row3, 9, side_r);
        }
    }
    ime_build_row(left, row4, 6, side_l);
    if(split) ime_build_row(right, row4, 6, side_r);
    /* Rebuilt on every shift/mode/restyle: keep the whole window (keys, rows,
     * split halves, extract editor) from taking focus off the bound field. */
    if(g_win != NULL) ime_strip_focus(g_win);
}

/* Rebuilding the grid deletes the very key that is dispatching LV_EVENT_CLICKED.
 * Doing it inline left the indev holding a dangling pressed object: the next
 * release could land on a stale key, move focus away from the bound textarea
 * and make the shell hide the IME mid-typing. Defer to LVGL's async phase,
 * after the event (and its release) has finished. */
static void ime_rebuild_async(void *user_data)
{
    (void)user_data;
    if(g_visible) ime_build_grid();
}

static void ime_key_cb(lv_event_t *event)
{
    lv_obj_t *target = ime_target_valid();
    if(target == NULL) return;
    if(vqeaf_settings_get_bool("keyboard.feedback.vibration", 0))
        (void)vio_haptic_pulse(12);
    intptr_t packed = (intptr_t)lv_event_get_user_data(event);
    int action = (int)(packed >> 8);
    char ch = (char)(packed & 0xFF);
    switch(action) {
    case IME_CHAR:
        if(g_shift && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        lv_textarea_add_char(target, (uint32_t)ch);
        if(g_shift) g_shift = 0;
        break;
    case IME_SPACE: lv_textarea_add_char(target, ' '); break;
    case IME_ENTER:
        /* Single-line command/search fields use READY as the submit contract;
         * adding a newline to a one-line textarea is ignored by LVGL. */
        if(lv_textarea_get_one_line(target))
            lv_obj_send_event(target, LV_EVENT_READY, NULL);
        else
            lv_textarea_add_char(target, '\n');
        break;
    case IME_BACKSPACE: lv_textarea_delete_char(target); break;
    case IME_SHIFT:
        g_shift = !g_shift;
        lv_async_call(ime_rebuild_async, NULL);
        break;
    case IME_MODE:
        /* Emoji panel's ABC returns to letters; otherwise toggle ?123/ABC. */
        if(g_emoji) { g_emoji = 0; g_symbols = 0; }
        else g_symbols = !g_symbols;
        g_shift = 0;
        lv_async_call(ime_rebuild_async, NULL);
        break;
    case IME_EMOJI_MODE:
        g_emoji = !g_emoji;
        g_symbols = 0;
        g_shift = 0;
        lv_async_call(ime_rebuild_async, NULL);
        break;
    case IME_EMOJI:
        if((unsigned char)ch < EMOJI_COUNT)
            lv_textarea_add_text(target, emoji_table[(unsigned char)ch]);
        break;
    default: break;
    }
    if(action == IME_CHAR || action == IME_SPACE || action == IME_ENTER ||
       action == IME_BACKSPACE || action == IME_EMOJI)
        ime_build_suggestions();
}

/* ---- floating drag ------------------------------------------------------- */

static void ime_drag_cb(lv_event_t *event)
{
    if(g_mode != VQEAF_IME_MODE_FLOATING || g_win == NULL) return;
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_active();
    if(indev == NULL) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if(code == LV_EVENT_PRESSED) {
        g_drag_pt = p;
    } else if(code == LV_EVENT_PRESSING) {
        int32_t dx = p.x - g_drag_pt.x, dy = p.y - g_drag_pt.y;
        g_drag_pt = p;
        int32_t nx = lv_obj_get_x(g_win) + dx;
        int32_t ny = lv_obj_get_y(g_win) + dy;
        if(nx < 0) nx = 0;
        if(ny < 0) ny = 0;
        if(nx > g_m.screen_w - g_m.width) nx = g_m.screen_w - g_m.width;
        if(ny > g_m.screen_h - g_m.height) ny = g_m.screen_h - g_m.height;
        lv_obj_set_pos(g_win, nx, ny);
    }
}

/* ---- insets / slide ------------------------------------------------------ */

static void ime_slide_cb(void *var, int32_t v)
{
    (void)var;
    if(g_win == NULL) return;
    /* Bottom-anchored modes slide up from the screen edge and the app content
     * shrinks by the same inset so the focused field stays visible. Floating
     * and extract overlay the app, so they neither slide nor resize it. */
    if(g_mode == VQEAF_IME_MODE_FLOATING || g_mode == VQEAF_IME_MODE_EXTRACT)
        return;
    lv_obj_set_y(g_win, g_m.screen_h - v);
    if(g_content != NULL) {
        int32_t content_h = g_content_base_h - v;
        if(content_h < 1) content_h = 1;
        lv_obj_set_height(g_content, content_h);
    }
}

static void ime_hide_done_cb(lv_anim_t *anim)
{
    (void)anim;
    /* A field can be re-focused before the slide-down finishes. show() then
     * reuses this same window, so an old hide animation must never delete the
     * keyboard the user just asked for again. */
    if(g_visible) return;
    if(g_win != NULL) { lv_obj_delete(g_win); g_win = NULL; }
    g_suggest = NULL; g_grid = NULL; g_extract_area = NULL;
}

/* Stop any slide still in flight on this window before starting a new one.
 * Without this, a hide animation's completed callback can outlive a newer
 * show and delete the keyboard while the user is typing. */
static void ime_stop_slide(void)
{
    if(g_win != NULL) lv_anim_delete(g_win, ime_slide_cb);
}

static void ime_sync_extract_area(void)
{
    if(g_mode != VQEAF_IME_MODE_EXTRACT || g_win == NULL) return;
    lv_obj_t *target = ime_target_valid();
    if(g_extract_area == NULL && target != NULL) {
        g_extract_area = lv_textarea_create(g_win);
        lv_obj_set_width(g_extract_area, lv_pct(100));
        lv_textarea_set_text(g_extract_area, lv_textarea_get_text(target));
        lv_obj_set_style_bg_color(g_extract_area, lv_color_darken(ime_panel_color(), 4), 0);
        lv_obj_set_style_text_color(g_extract_area, VQEAF_C_TEXT, 0);
        lv_obj_set_style_text_font(g_extract_area,
                                   lv_obj_get_style_text_font(target, 0), 0);
        lv_obj_move_to_index(g_extract_area, 0);
    }
    if(g_extract_area == NULL) return;
    int rows = (g_emoji || g_symbols) ? 4 : g_m.rows;
    int32_t grid_h = rows * g_m.row_h + (rows - 1) * g_m.row_gap;
    int32_t suggest_h = IME_SUGGEST_H_DP * g_m.dpi / 160;
    int32_t editor_h = g_m.height - 2 * g_m.padding - 2 * g_m.row_gap -
                       suggest_h - grid_h;
    lv_obj_set_height(g_extract_area, editor_h > 1 ? editor_h : 1);
}

/* ---- public window API --------------------------------------------------- */

static void ime_compute(void)
{
    lv_display_t *d = lv_display_get_default();
    int32_t w = lv_display_get_horizontal_resolution(d);
    int32_t h = lv_display_get_vertical_resolution(d);
    vqeaf_ime_metrics(w, h, 320, g_mode, &g_m);
}

void vqeaf_ime_set_mode(vqeaf_ime_mode_t mode)
{
    g_mode = mode;
    if(g_visible) {
        ime_compute();
        if(g_win != NULL) {
            lv_obj_set_size(g_win, g_m.width, g_m.height);
            lv_obj_set_pos(g_win, g_m.x, g_m.y);
        }
        if(mode != VQEAF_IME_MODE_EXTRACT && g_extract_area != NULL) {
            lv_obj_delete(g_extract_area);
            g_extract_area = NULL;
        }
        ime_sync_extract_area();
        ime_build_grid();
    }
}

vqeaf_ime_mode_t vqeaf_ime_get_mode(void) { return g_mode; }

void vqeaf_ime_show(lv_obj_t *textarea, lv_obj_t *content)
{
    /* Focus and click events can both bubble from the terminal textarea to
     * its scroll container. Treat the second request as idempotent: taking a
     * new base height while the first animation is shrinking content causes
     * cumulative insets and the classic keyboard-open layout jump. */
    if(g_visible && g_win != NULL && g_target == textarea &&
       g_content == content)
        return;
    if(textarea != NULL) {
        const lv_font_t *base = lv_obj_get_style_text_font(textarea, 0);
        const lv_font_t *face = vqeaf_type_font_with_emoji(base);
        if(face != base) lv_obj_set_style_text_font(textarea, face, 0);
        /* Re-arm on every bind: the callback ignores events from other
         * objects, so earlier bindings stay harmless. */
        lv_obj_add_event_cb(textarea, ime_target_delete_cb, LV_EVENT_DELETE,
                            NULL);
    }
    g_target = textarea;
    g_content = content;
    if(content != NULL) {
        lv_obj_update_layout(content);
        g_content_base_h = lv_obj_get_height(content);
    }
    ime_compute();
    if(g_win == NULL) {
        g_win = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(g_win);
        lv_obj_set_style_bg_color(g_win, ime_panel_color(), 0);
        lv_obj_set_style_bg_opa(g_win, (lv_opa_t)250, 0);
        lv_obj_set_style_radius(g_win, 28, 0);
        lv_obj_set_style_border_width(g_win, 1, 0);
        lv_obj_set_style_border_color(g_win, ime_accent_color(), 0);
        lv_obj_set_style_border_opa(g_win, (lv_opa_t)72, 0);
        lv_obj_set_style_shadow_width(g_win, 18, 0);
        lv_obj_set_style_shadow_ofs_y(g_win, 8, 0);
        lv_obj_set_style_shadow_color(g_win, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(g_win, 84, 0);
        lv_obj_set_style_pad_left(g_win, g_m.padding, 0);
        lv_obj_set_style_pad_right(g_win, g_m.padding, 0);
        lv_obj_set_style_pad_top(g_win, g_m.padding, 0);
        lv_obj_set_style_pad_bottom(g_win, g_m.padding, 0);
        lv_obj_set_flex_flow(g_win, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(g_win, g_m.row_gap, 0);
        lv_obj_remove_flag(g_win, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_win, LV_OBJ_FLAG_FLOATING);
        lv_obj_add_event_cb(g_win, ime_drag_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(g_win, ime_drag_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(g_win, ime_delete_cb, LV_EVENT_DELETE, NULL);
        g_suggest = vqeaf_obj(g_win);
        lv_obj_set_width(g_suggest, lv_pct(100));
        lv_obj_set_height(g_suggest, IME_SUGGEST_H_DP * g_m.dpi / 160);
        g_grid = vqeaf_obj(g_win);
        lv_obj_set_width(g_grid, lv_pct(100));
        lv_obj_set_height(g_grid, LV_SIZE_CONTENT);
    }
    lv_obj_set_size(g_win, g_m.width, g_m.height);
    lv_obj_set_pos(g_win, g_m.x, g_m.y);
    ime_sync_extract_area();
    ime_build_grid();
    g_visible = 1;
    vqeaf_pages_terminal_ime_state(1);
    lv_obj_update_layout(g_win);
    ime_stop_slide();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_win);
    lv_anim_set_exec_cb(&a, ime_slide_cb);
    lv_anim_set_values(&a, 0, g_m.height);
    lv_anim_set_duration(&a, VQEAF_M3_MOTION_MEDIUM);
    lv_anim_set_early_apply(&a, true);
    lv_anim_start(&a);
    lv_obj_invalidate(lv_layer_top());
}

void vqeaf_ime_hide(void)
{
    if(!g_visible || g_win == NULL) { g_visible = 0; return; }
    /* Reclaim the app's space immediately and detach, so the slide-down anim
     * never touches a content object the shell may be tearing down. */
    if(g_content != NULL) {
        lv_obj_set_height(g_content, g_content_base_h);
        g_content = NULL;
    }
    g_target = NULL;
    ime_stop_slide();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_win);
    lv_anim_set_exec_cb(&a, ime_slide_cb);
    lv_anim_set_values(&a, g_m.height, 0);
    lv_anim_set_duration(&a, VQEAF_M3_MOTION_MEDIUM);
    lv_anim_set_completed_cb(&a, ime_hide_done_cb);
    lv_anim_start(&a);
    g_visible = 0;
    vqeaf_pages_terminal_ime_state(0);
}

int vqeaf_ime_visible(void) { return g_visible; }
int32_t vqeaf_ime_height(void) { return g_visible ? g_m.height : 0; }
lv_obj_t *vqeaf_ime_window(void) { return g_win; }
lv_obj_t *vqeaf_ime_grid(void) { return g_grid; }
lv_obj_t *vqeaf_ime_suggestions(void) { return g_suggest; }
int vqeaf_ime_haptics_available(void) { return vio_haptics_available(); }
int vqeaf_ime_sound_available(void) { return 0; }

void vqeaf_ime_restyle(void)
{
    if(g_win == NULL) return;
    /* Light/dark and dynamic-colour changes swap the palette underneath a
     * window that is already on screen, so refresh its chrome as well. */
    lv_obj_set_style_bg_color(g_win, ime_panel_color(), 0);
    lv_obj_set_style_border_color(g_win, ime_accent_color(), 0);
    ime_build_grid();
}
