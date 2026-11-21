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
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vqeaf_ime.h"
#include "vqeaf_theme.h"
#include "vqeaf_pages.h"
#include "vqeaf_fonts.h"
#include "violet.h"
#include <vqeaf/settings.h>

/* ---- responsive metrics (pure) ------------------------------------------ */

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

    int32_t row_dp = sw >= VQEAF_IME_SW720_DP ? 54 :
                     (sw >= VQEAF_IME_SW600_DP ? 50 : 48);
    int32_t gap = 4 * dpi / 160;
    int32_t vpad = 5 * dpi / 160;
    int32_t row_h = row_dp * dpi / 160;
    int32_t h_raw = rows * row_h + (rows - 1) * gap + 2 * vpad;
    int32_t minp = land ? VQEAF_IME_LANDSCAPE_MIN_PCT : VQEAF_IME_PORTRAIT_MIN_PCT;
    int32_t maxp = land ? VQEAF_IME_LANDSCAPE_MAX_PCT : VQEAF_IME_PORTRAIT_MAX_PCT;
    int32_t h_min = h * minp / 100, h_max = h * maxp / 100;
    int32_t H = h_raw;
    if(H < h_min) H = h_min;
    if(H > h_max) H = h_max;
    row_h = (H - (rows - 1) * gap - 2 * vpad) / rows;
    if(row_h < 1) row_h = 1;
    out->height = H;
    out->row_h = row_h;
    out->gap = 64 * dpi / 160;

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
    out->key_font = row_h >= 110 ? 28 : (row_h >= 80 ? 24 : 20);
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
    K("?123",3,IME_MODE,0,1), K(":)",2,IME_EMOJI_MODE,0,1),
    K(",",2,IME_CHAR,',',1), K("SPACE",5,IME_SPACE,' ',0),
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
    K("ABC",3,IME_MODE,0,1), K(":)",2,IME_EMOJI_MODE,0,1),
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
    if(px >= 28) return &vqeaf_font_28;
    if(px >= 24) return &vqeaf_font_24;
    return &vqeaf_font_20;
}

static lv_color_t ime_key_color(void)
{
    return lv_color_hex(vqeaf_pages_kb_theme_key_color(
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

/* Special keys get a subtle tint and an accent hairline instead of another
 * all-caps text block. The caption stays for accessibility and the interaction
 * gate; the Material Icons glyph carries the meaning at a glance. */
static lv_color_t ime_special_key_color(lv_color_t base, int active)
{
    if(active) return VQEAF_C_ACCENT;
    lv_color32_t c = lv_color_to_32(base, LV_OPA_COVER);
    uint32_t lum = (c.red * 299u + c.green * 587u + c.blue * 114u) / 1000u;
    return lum > 128u ? lv_color_darken(base, 18) :
                        lv_color_lighten(base, 26);
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

/* ---- key grid ------------------------------------------------------------ */

static void ime_key_cb(lv_event_t *event);

/* A page switch or a modal cleanup can delete the whole top layer, IME
 * included. Without this hook the cached window/grid pointers dangle and the
 * next focus event resurrects a deleted object (a crash later in the gate). */
static void ime_delete_cb(lv_event_t *event)
{
    if(lv_event_get_target(event) != g_win) return;
    g_win = NULL;
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

static void ime_build_row(lv_obj_t *panel, const ime_key_t *keys, int n, int side)
{
    lv_obj_t *row = vqeaf_weighted_row(panel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, g_m.row_h);
    lv_obj_set_style_pad_column(row, 5, 0);
    for(int i = 0; i < n; i++) {
        const ime_key_t *k = &keys[i];
        if(side != 0 && k->side != 0 && k->side != side) continue;
        lv_obj_t *key = vqeaf_obj(row);
        vqeaf_weight(key, k->weight);
        lv_obj_set_height(key, lv_pct(100));
        lv_obj_set_style_radius(key, 15, 0);
        lv_obj_set_style_border_width(key, 1, 0);
        lv_obj_set_style_border_color(key, VQEAF_C_OUTLINE, 0);
        lv_obj_set_style_border_opa(key, (lv_opa_t)48, 0);
        if(k->action == IME_FILLER) {
            lv_obj_set_style_bg_opa(key, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(key, 0, 0);
            lv_obj_remove_flag(key, LV_OBJ_FLAG_CLICKABLE);
            continue;
        }
        lv_color_t base = ime_key_color();
        const char *icon = ime_key_icon(k);
        int active_shift = k->action == IME_SHIFT && g_shift;
        lv_color_t key_bg = icon == NULL ? base :
                            ime_special_key_color(base, active_shift);
        lv_obj_set_style_bg_color(key, key_bg, 0);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(key, lv_color_mix(base, VQEAF_C_ACCENT, 54),
                                  LV_STATE_PRESSED);
        lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
        char label[8];
        if(k->action == IME_CHAR && k->ch >= 'a' && k->ch <= 'z' && g_shift) {
            label[0] = (char)(k->ch - 'a' + 'A'); label[1] = 0;
        } else {
            snprintf(label, sizeof(label), "%s", k->label);
        }
        lv_color_t fg = ime_contrast_color(key_bg);
        if(icon != NULL) {
            /* Keep familiar actions as one centered symbol. Stacking a tiny
             * caption below the glyph made the compact rows feel crowded. */
            lv_obj_t *ic = vqeaf_glyph(key, icon, VQEAF_ICON_SM, fg);
            lv_obj_center(ic);
        } else {
            const lv_font_t *face = ime_font(g_key_font);
            if(k->action == IME_EMOJI)
                face = &vqeaf_emoji_28;
            lv_obj_t *l = vqeaf_label(key, label, face, fg);
            lv_obj_center(l);
        }
        lv_obj_add_event_cb(key, ime_key_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)((k->action << 8) |
                                               (unsigned char)k->ch));
    }
}

static void ime_build_grid(void)
{
    if(g_grid == NULL) return;
    lv_obj_clean(g_grid);
    int split = g_mode == VQEAF_IME_MODE_SPLIT;
    /* Narrow windows (split halves, floating) need a smaller face so the
     * SHIFT/ENTER labels fit inside their keys. */
    g_key_font = (split || g_m.width < 500) ? 20 : g_m.key_font;
    lv_obj_t *left = g_grid, *right = NULL;
    if(split) {
        lv_obj_set_flex_flow(g_grid, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(g_grid, g_m.gap, 0);
        left = vqeaf_column(g_grid);
        right = vqeaf_column(g_grid);
        int32_t panel_w = (g_m.width - 16 - g_m.gap) / 2;
        lv_obj_set_width(left, panel_w);
        lv_obj_set_width(right, panel_w);
        lv_obj_set_style_pad_row(left, 5, 0);
        lv_obj_set_style_pad_row(right, 5, 0);
    } else {
        lv_obj_set_flex_flow(g_grid, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(g_grid, 5, 0);
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
    if(g_target == NULL) return;
    if(vqeaf_settings_get_bool("keyboard.feedback.vibration", 0))
        (void)vio_haptic_pulse(12);
    intptr_t packed = (intptr_t)lv_event_get_user_data(event);
    int action = (int)(packed >> 8);
    char ch = (char)(packed & 0xFF);
    switch(action) {
    case IME_CHAR:
        if(g_shift && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        lv_textarea_add_char(g_target, (uint32_t)ch);
        if(g_shift) { g_shift = 0; lv_async_call(ime_rebuild_async, NULL); }
        break;
    case IME_SPACE: lv_textarea_add_char(g_target, ' '); break;
    case IME_ENTER: lv_textarea_add_char(g_target, '\n'); break;
    case IME_BACKSPACE: lv_textarea_delete_char(g_target); break;
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
            lv_textarea_add_text(g_target, emoji_table[(unsigned char)ch]);
        break;
    default: break;
    }
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
    if(g_content != NULL)
        lv_obj_set_height(g_content, g_content_base_h - v);
}

static void ime_hide_done_cb(lv_anim_t *anim)
{
    (void)anim;
    /* A field can be re-focused before the slide-down finishes. show() then
     * reuses this same window, so an old hide animation must never delete the
     * keyboard the user just asked for again. */
    if(g_visible) return;
    if(g_win != NULL) { lv_obj_delete(g_win); g_win = NULL; }
    g_grid = NULL; g_extract_area = NULL;
}

/* Stop any slide still in flight on this window before starting a new one.
 * Without this, a hide animation's completed callback can outlive a newer
 * show and delete the keyboard while the user is typing. */
static void ime_stop_slide(void)
{
    if(g_win != NULL) lv_anim_delete(g_win, ime_slide_cb);
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
        ime_build_grid();
    }
}

vqeaf_ime_mode_t vqeaf_ime_get_mode(void) { return g_mode; }

void vqeaf_ime_show(lv_obj_t *textarea, lv_obj_t *content)
{
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
        lv_obj_set_style_bg_color(g_win, VQEAF_C_SURFACE_CONTAINER, 0);
        lv_obj_set_style_bg_opa(g_win, (lv_opa_t)244, 0);
        lv_obj_set_style_radius(g_win, 24, 0);
        lv_obj_set_style_border_width(g_win, 1, 0);
        lv_obj_set_style_border_color(g_win, VQEAF_C_OUTLINE, 0);
        lv_obj_set_style_border_opa(g_win, (lv_opa_t)180, 0);
        lv_obj_set_style_pad_left(g_win, 8, 0);
        lv_obj_set_style_pad_right(g_win, 8, 0);
        lv_obj_set_style_pad_top(g_win, 8, 0);
        lv_obj_set_style_pad_bottom(g_win, 8, 0);
        lv_obj_set_flex_flow(g_win, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(g_win, 5, 0);
        lv_obj_remove_flag(g_win, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_win, LV_OBJ_FLAG_FLOATING);
        lv_obj_add_event_cb(g_win, ime_drag_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(g_win, ime_drag_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(g_win, ime_delete_cb, LV_EVENT_DELETE, NULL);
        g_grid = vqeaf_obj(g_win);
        lv_obj_set_width(g_grid, lv_pct(100));
        lv_obj_set_height(g_grid, LV_SIZE_CONTENT);
    }
    lv_obj_set_size(g_win, g_m.width, g_m.height);
    lv_obj_set_pos(g_win, g_m.x, g_m.y);
    if(g_mode == VQEAF_IME_MODE_EXTRACT) {
        /* Extract: expanded editor above the keys so landscape typing works. */
        if(g_extract_area == NULL && g_target != NULL) {
            g_extract_area = lv_textarea_create(g_win);
            lv_obj_set_width(g_extract_area, lv_pct(100));
            lv_obj_set_height(g_extract_area, g_m.height / 2);
            lv_textarea_set_text(g_extract_area, lv_textarea_get_text(g_target));
            lv_obj_set_style_bg_color(g_extract_area, VQEAF_C_SURFACE_2, 0);
            lv_obj_set_style_text_color(g_extract_area, VQEAF_C_TEXT, 0);
            lv_obj_move_to_index(g_extract_area, 0);  /* editor above the keys */
        }
    }
    ime_build_grid();
    g_visible = 1;
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
}

int vqeaf_ime_visible(void) { return g_visible; }
int32_t vqeaf_ime_height(void) { return g_visible ? g_m.height : 0; }
lv_obj_t *vqeaf_ime_window(void) { return g_win; }
int vqeaf_ime_haptics_available(void) { return vio_haptics_available(); }
int vqeaf_ime_sound_available(void) { return 0; }

void vqeaf_ime_restyle(void)
{
    if(g_win == NULL) return;
    /* Light/dark and dynamic-colour changes swap the palette underneath a
     * window that is already on screen, so refresh its chrome as well. */
    lv_obj_set_style_bg_color(g_win, VQEAF_C_SURFACE_CONTAINER, 0);
    lv_obj_set_style_border_color(g_win, VQEAF_C_OUTLINE, 0);
    ime_build_grid();
}
