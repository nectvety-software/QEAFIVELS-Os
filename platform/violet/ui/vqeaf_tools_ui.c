/* SPDX-License-Identifier: MIT */
/* vqeaf_tools_ui.c - taskbar, NTFS search, task manager and disk cleanup.
 *
 * The shell keeps its phone navigation model: the taskbar is a thin desktop
 * strip under the status bar, while the dock and gesture pill stay where they
 * were. Every utility page reports only what its native backend returned.
 */
#include "vqeaf_tools_ui.h"
#include "vqeaf_sys_tools.h"
#include "vqeaf_theme.h"
#include "vqeaf_fonts.h"
#include "vqeaf_ime.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

/* ---- shared layout helpers ---------------------------------------------- */

static lv_obj_t *tools_root(lv_obj_t *content)
{
    lv_obj_t *root = vqeaf_obj(content);
    lv_obj_set_size(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, VQEAF_PAD, 0);
    lv_obj_set_style_pad_row(root, 20, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    return root;
}

static lv_obj_t *tools_card(lv_obj_t *parent)
{
    lv_obj_t *card = vqeaf_card(parent, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    return card;
}

static lv_obj_t *tools_button(lv_obj_t *parent, const char *text, int32_t width,
                              lv_event_cb_t cb, void *data)
{
    lv_obj_t *button = vqeaf_card(parent, width, 58);
    lv_obj_t *label;

    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label = vqeaf_label(button, text, VQEAF_FONT_BODY, VQEAF_C_TEXT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    if(cb != NULL) vqeaf_on_click(button, cb, data);
    return button;
}

static void tools_note(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = vqeaf_label(parent, text, VQEAF_FONT_CAPTION,
                                  VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
}

static void tools_back(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *back = vqeaf_tile(parent, VQEAF_SYM_ARROW_BACK,
                                VQEAF_C_SURFACE, VQEAF_C_TEXT, 66, 22);
    vqeaf_on_click(back, cb, NULL);
}

static void tools_title(lv_obj_t *parent, const char *title, lv_event_cb_t cb)
{
    lv_obj_t *row = vqeaf_row(parent);
    /* vqeaf_row defaults to space-between; a title must sit next to its back
     * tile, otherwise it drifts to the far right of the page. */
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    tools_back(row, cb);
    vqeaf_label(row, title, VQEAF_FONT_HEADLINE, VQEAF_C_TEXT);
}

static void tools_status(lv_obj_t *parent, const char *text)
{
    lv_obj_t *card = tools_card(parent);
    lv_obj_t *label = vqeaf_label(card, text, VQEAF_FONT_CAPTION,
                                  VQEAF_C_TEXT_DIM);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
}

static void tools_format_bytes(uint64_t value, char *out, size_t cap)
{
    if(value < 1024u)
        snprintf(out, cap, "%llu B", (unsigned long long)value);
    else if(value < 1024u * 1024u)
        snprintf(out, cap, "%.1f KiB", (double)value / 1024.0);
    else if(value < 1024u * 1024u * 1024u)
        snprintf(out, cap, "%.1f MiB", (double)value / (1024.0 * 1024.0));
    else
        snprintf(out, cap, "%.2f GiB",
                 (double)value / (1024.0 * 1024.0 * 1024.0));
}

/* ---- confirmation sheet -------------------------------------------------- */

typedef void (*tools_action_fn)(void *data);

static lv_obj_t *g_confirm_scrim;
static lv_obj_t *g_confirm_card;
static tools_action_fn g_confirm_yes;
static void *g_confirm_data;

static void tools_confirm_close(void)
{
    if(g_confirm_card != NULL) {
        lv_obj_delete(g_confirm_card);
        g_confirm_card = NULL;
    }
    if(g_confirm_scrim != NULL) {
        lv_obj_delete(g_confirm_scrim);
        g_confirm_scrim = NULL;
    }
    g_confirm_yes = NULL;
    g_confirm_data = NULL;
}

static void tools_confirm_cancel_cb(lv_event_t *event)
{
    (void)event;
    tools_confirm_close();
}

static void tools_confirm_yes_cb(lv_event_t *event)
{
    tools_action_fn action = g_confirm_yes;
    void *data = g_confirm_data;
    (void)event;
    tools_confirm_close();
    if(action != NULL) action(data);
}

static void tools_confirm(const char *title, const char *body,
                          tools_action_fn yes, void *data)
{
    lv_obj_t *row, *body_label;

    tools_confirm_close();
    g_confirm_scrim = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_confirm_scrim);
    lv_obj_set_pos(g_confirm_scrim, 0, 0);
    lv_obj_set_size(g_confirm_scrim, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_confirm_scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_confirm_scrim, 170, 0);
    lv_obj_add_flag(g_confirm_scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_confirm_scrim, tools_confirm_cancel_cb,
                        LV_EVENT_CLICKED, NULL);

    /* Direct child of the top layer: on the handset, hit-testing through a
     * full-screen wrapper already broke the app info sheet once. */
    g_confirm_card = vqeaf_card(lv_layer_top(), 648, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_confirm_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(g_confirm_card, 24, 0);
    lv_obj_set_style_pad_row(g_confirm_card, 18, 0);
    lv_obj_center(g_confirm_card);
    vqeaf_label(g_confirm_card, title, VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    body_label = vqeaf_label(g_confirm_card, body, VQEAF_FONT_CAPTION,
                             VQEAF_C_TEXT_DIM);
    lv_obj_set_width(body_label, lv_pct(100));
    lv_label_set_long_mode(body_label, LV_LABEL_LONG_MODE_WRAP);

    row = vqeaf_row(g_confirm_card);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    tools_button(row, "Huỷ", 180, tools_confirm_cancel_cb, NULL);
    tools_button(row, "Xác nhận", 220, tools_confirm_yes_cb, NULL);
    g_confirm_yes = yes;
    g_confirm_data = data;
}

/* ---- taskbar ------------------------------------------------------------- */

static lv_obj_t *g_taskbar;
static lv_obj_t *g_taskbar_tasks;
static lv_obj_t *g_taskbar_buttons[5];

static const vqeaf_page_t taskbar_pages[5] = {
    VQEAF_PAGE_APPS, VQEAF_PAGE_FILES, VQEAF_PAGE_NTFS_SEARCH,
    VQEAF_PAGE_TASK_MANAGER, VQEAF_PAGE_DISK_CLEANUP
};

int vqeaf_tools_taskbar_visible(vqeaf_page_t page)
{
    if(page == VQEAF_PAGE_LOCK || page == VQEAF_PAGE_SETUP ||
       page == VQEAF_PAGE_QEAPP_NATIVE || page == VQEAF_PAGE_APPS_EDIT ||
       page == VQEAF_PAGE_TERMINAL)
        return 0;
    if(page == VQEAF_PAGE_HOME && !vqeaf_theme_fluent())
        return 0;
    return 1;
}

static void taskbar_open_cb(lv_event_t *event)
{
    int id = (int)(intptr_t)lv_event_get_user_data(event);
    if(id < 0 || id >= 5) return;
    tools_confirm_close();
    vqeaf_ui_show(taskbar_pages[id]);
}

static void taskbar_recent_cb(lv_event_t *event)
{
    vqeaf_page_t page = (vqeaf_page_t)(intptr_t)lv_event_get_user_data(event);
    vqeaf_ui_show(page);
}

static void taskbar_recent_close_cb(lv_event_t *event)
{
    vqeaf_page_t page = (vqeaf_page_t)(intptr_t)lv_event_get_user_data(event);
    vqeaf_ui_recent_close(page);
    vqeaf_tools_taskbar_refresh(vqeaf_ui_page());
}

static const char *taskbar_glyph(vqeaf_page_t page)
{
    switch(page) {
    case VQEAF_PAGE_HOME:      return VQEAF_SYM_HOME;
    case VQEAF_PAGE_APPS:      return VQEAF_SYM_APPS;
    case VQEAF_PAGE_FILES:     return VQEAF_SYM_FOLDER;
    case VQEAF_PAGE_NTFS_SEARCH: return VQEAF_SYM_SEARCH;
    case VQEAF_PAGE_TASK_MANAGER: return VQEAF_SYM_MEMORY;
    case VQEAF_PAGE_DISK_CLEANUP: return VQEAF_SYM_DELETE_SWEEP;
    case VQEAF_PAGE_SETTINGS:  return VQEAF_SYM_SETTINGS;
    case VQEAF_PAGE_TERMINAL:  return VQEAF_SYM_KEYBOARD;
    default:                   return VQEAF_SYM_OPEN_IN_NEW;
    }
}

static lv_obj_t *taskbar_button(lv_obj_t *parent, const char *glyph,
                                const char *caption, int id)
{
    lv_obj_t *button = vqeaf_obj(parent);
    lv_obj_set_size(button, 96, VQEAF_TASKBAR_H - 12);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(button, 2, 0);
    lv_obj_set_style_pad_row(button, 0, 0);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, VQEAF_C_LINE, 0);
    lv_obj_set_style_border_opa(button, LV_OPA_40, 0);
    vqeaf_glyph(button, glyph, VQEAF_ICON_SM, VQEAF_C_TEXT);
    vqeaf_label(button, caption, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    vqeaf_on_click(button, taskbar_open_cb, (void *)(intptr_t)id);
    return button;
}

void vqeaf_tools_taskbar_create(lv_obj_t *screen)
{
    static const char *const captions[5] = {
        "Menu", "QL tệp", "NTFS", "Tác vụ", "Dọn ổ"
    };
    static const char *const glyphs[5] = {
        VQEAF_SYM_APPS, VQEAF_SYM_FOLDER, VQEAF_SYM_SEARCH,
        VQEAF_SYM_MEMORY, VQEAF_SYM_DELETE_SWEEP
    };

    if(screen == NULL) return;
    g_taskbar = vqeaf_obj(screen);
    lv_obj_add_flag(g_taskbar, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_pos(g_taskbar, 0, VQEAF_STATUS_H);
    lv_obj_set_size(g_taskbar, lv_pct(100), VQEAF_TASKBAR_H);
    lv_obj_set_style_bg_color(g_taskbar, VQEAF_C_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(g_taskbar, LV_OPA_90, 0);
    lv_obj_set_style_pad_left(g_taskbar, 12, 0);
    lv_obj_set_style_pad_right(g_taskbar, 12, 0);
    lv_obj_set_style_pad_column(g_taskbar, 8, 0);
    lv_obj_set_flex_flow(g_taskbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_taskbar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for(int i = 0; i < 5; i++)
        g_taskbar_buttons[i] = taskbar_button(g_taskbar, glyphs[i],
                                              captions[i], i);

    g_taskbar_tasks = vqeaf_obj(g_taskbar);
    lv_obj_set_size(g_taskbar_tasks, 0, VQEAF_TASKBAR_H - 8);
    lv_obj_set_flex_grow(g_taskbar_tasks, 1);
    lv_obj_set_flex_flow(g_taskbar_tasks, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_taskbar_tasks, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(g_taskbar_tasks, 10, 0);
    lv_obj_set_style_pad_column(g_taskbar_tasks, 8, 0);
}

void vqeaf_tools_taskbar_refresh(vqeaf_page_t current)
{
    char count_text[32];
    int shown = 0;

    if(g_taskbar == NULL) return;
    if(vqeaf_tools_taskbar_visible(current))
        lv_obj_clear_flag(g_taskbar, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_taskbar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_taskbar, VQEAF_C_SURFACE_2, 0);
    for(int i = 0; i < 5; i++) {
        if(g_taskbar_buttons[i] == NULL) continue;
        lv_obj_set_style_border_width(g_taskbar_buttons[i],
            taskbar_pages[i] == current ? 3 : 1, 0);
        lv_obj_set_style_border_color(g_taskbar_buttons[i],
            taskbar_pages[i] == current ? VQEAF_C_ACCENT : VQEAF_C_LINE, 0);
    }
    lv_obj_clean(g_taskbar_tasks);
    for(int i = 0; i < vqeaf_ui_recent_count() && shown < 3; i++) {
        vqeaf_page_t page = vqeaf_ui_recent_page(i);
        lv_obj_t *chip = vqeaf_obj(g_taskbar_tasks);
        lv_obj_set_size(chip, 46, 44);
        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_radius(chip, 12, 0);
        lv_obj_set_style_border_width(chip, page == current ? 3 : 1, 0);
        lv_obj_set_style_border_color(chip,
            page == current ? VQEAF_C_ACCENT : VQEAF_C_LINE, 0);
        lv_obj_set_style_border_opa(chip, LV_OPA_60, 0);
        vqeaf_glyph(chip, taskbar_glyph(page), VQEAF_ICON_SM,
                    page == current ? VQEAF_C_ACCENT : VQEAF_C_TEXT_DIM);
        vqeaf_on_click(chip, taskbar_recent_cb, (void *)(intptr_t)page);
        lv_obj_add_event_cb(chip, taskbar_recent_close_cb,
                            LV_EVENT_LONG_PRESSED, (void *)(intptr_t)page);
        shown++;
    }
    snprintf(count_text, sizeof(count_text), "%d tác vụ",
             vqeaf_ui_recent_count());
    vqeaf_label(g_taskbar_tasks, count_text, VQEAF_FONT_CAPTION,
                VQEAF_C_TEXT_DIM);
}

/* ---- NTFS search --------------------------------------------------------- */

static vqeaf_mount_t g_ntfs_mounts[12];
static int g_ntfs_mount_count;
static int g_ntfs_volume;
static char g_ntfs_query[128];
static char g_ntfs_msg[256];
static vqeaf_search_hit_t g_ntfs_hits[32];
static int g_ntfs_hit_count;
static uint64_t g_ntfs_scanned;
static lv_obj_t *g_ntfs_ta;

static void ntfs_home_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_show(VQEAF_PAGE_APPS);
}

static void ntfs_volume_cb(lv_event_t *event)
{
    g_ntfs_volume = (int)(intptr_t)lv_event_get_user_data(event);
    vqeaf_ui_show(VQEAF_PAGE_NTFS_SEARCH);
}

static void ntfs_text_cb(lv_event_t *event)
{
    const char *text = g_ntfs_ta != NULL ? lv_textarea_get_text(g_ntfs_ta) : "";
    (void)event;
    snprintf(g_ntfs_query, sizeof(g_ntfs_query), "%s", text);
}

static void ntfs_focus_cb(lv_event_t *event)
{
    if(event != NULL && lv_event_get_code(event) == LV_EVENT_FOCUSED &&
       g_ntfs_ta != NULL)
        vqeaf_ime_show(g_ntfs_ta, NULL);
}

static void ntfs_search_cb(lv_event_t *event)
{
    int rc;
    (void)event;
    if(g_ntfs_ta != NULL)
        snprintf(g_ntfs_query, sizeof(g_ntfs_query), "%s",
                 lv_textarea_get_text(g_ntfs_ta));
    if(g_ntfs_query[0] == '\0') {
        snprintf(g_ntfs_msg, sizeof(g_ntfs_msg),
                 "Nhập tên tệp hoặc thư mục cần tìm.");
    }
    else if(g_ntfs_volume < 0 || g_ntfs_volume >= g_ntfs_mount_count) {
        snprintf(g_ntfs_msg, sizeof(g_ntfs_msg),
                 "Chưa có phân vùng NTFS nào được chọn.");
    }
    else {
        g_ntfs_scanned = 0;
        rc = vqeaf_sys_search(g_ntfs_mounts[g_ntfs_volume].path,
                              g_ntfs_query, g_ntfs_hits, 32, 8,
                              &g_ntfs_scanned);
        g_ntfs_hit_count = rc > 0 ? rc : 0;
        if(rc < 0)
            snprintf(g_ntfs_msg, sizeof(g_ntfs_msg),
                     "Không tìm được trên %s (mã lỗi %d).",
                     g_ntfs_mounts[g_ntfs_volume].path, -rc);
        else
            snprintf(g_ntfs_msg, sizeof(g_ntfs_msg),
                     "%d kết quả cho '%s'; đã quét %llu mục trên %s.",
                     rc, g_ntfs_query,
                     (unsigned long long)g_ntfs_scanned,
                     g_ntfs_mounts[g_ntfs_volume].path);
    }
    vqeaf_ui_show(VQEAF_PAGE_NTFS_SEARCH);
}

static void ntfs_hit_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if(index < 0 || index >= g_ntfs_hit_count) return;
    snprintf(g_ntfs_msg, sizeof(g_ntfs_msg), "Mục: %s",
             g_ntfs_hits[index].path);
    vqeaf_ui_show(VQEAF_PAGE_NTFS_SEARCH);
}

void vqeaf_tools_build_ntfs_search(lv_obj_t *content)
{
    lv_obj_t *root = tools_root(content);
    lv_obj_t *card, *row, *label;

    g_ntfs_ta = NULL;
    g_ntfs_mount_count = vqeaf_sys_mounts(g_ntfs_mounts, 12, 1);
    if(g_ntfs_mount_count < 0) g_ntfs_mount_count = 0;
    if(g_ntfs_volume >= g_ntfs_mount_count) g_ntfs_volume = 0;
    if(g_ntfs_mount_count <= 0) g_ntfs_hit_count = 0;

    tools_title(root, "Tìm kiếm NTFS", ntfs_home_cb);
    tools_note(root,
        "Chỉ tìm trên phân vùng NTFS đã được mount trên thiết bị. "
        "OS không truy cập trực tiếp đĩa Windows và không tạo kết quả giả.");

    card = tools_card(root);
    vqeaf_label(card, "Phân vùng NTFS", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    if(g_ntfs_mount_count <= 0) {
        label = vqeaf_label(card,
            "Không phát hiện phân vùng NTFS đã mount. "
            "Cắm ổ NTFS qua USB rồi quét lại; tìm kiếm sẽ không chạy khi chưa có volume.",
            VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    }
    else {
        row = vqeaf_row(card);
        lv_obj_set_style_pad_column(row, 10, 0);
        for(int i = 0; i < g_ntfs_mount_count; i++) {
            char name[80];
            snprintf(name, sizeof(name), "%s", g_ntfs_mounts[i].path);
            tools_button(row, name, 240, ntfs_volume_cb,
                         (void *)(intptr_t)i);
        }
        label = vqeaf_label(card,
            "Chạm một phân vùng để chọn làm gốc tìm kiếm.",
            VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        lv_obj_set_width(label, lv_pct(100));
    }

    card = tools_card(root);
    g_ntfs_ta = lv_textarea_create(card);
    lv_obj_set_width(g_ntfs_ta, lv_pct(100));
    lv_obj_set_height(g_ntfs_ta, 76);
    lv_obj_set_style_bg_color(g_ntfs_ta, VQEAF_C_SURFACE_2, 0);
    lv_textarea_set_one_line(g_ntfs_ta, true);
    lv_textarea_set_placeholder_text(g_ntfs_ta, "Tên tệp hoặc thư mục");
    /* The default placeholder font lacks the shell's Vietnamese glyphs. */
    lv_obj_set_style_text_font(g_ntfs_ta, VQEAF_FONT_CAPTION,
                               LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_color(g_ntfs_ta, VQEAF_C_TEXT_MUTE,
                                LV_PART_TEXTAREA_PLACEHOLDER);
    lv_textarea_set_text(g_ntfs_ta, g_ntfs_query);
    lv_obj_add_event_cb(g_ntfs_ta, ntfs_text_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_ntfs_ta, ntfs_focus_cb, LV_EVENT_FOCUSED, NULL);
    tools_button(card, "Tìm trên NTFS", lv_pct(100), ntfs_search_cb, NULL);

    tools_status(root, g_ntfs_msg[0] != '\0' ? g_ntfs_msg :
        "Chưa tìm. Kết quả chỉ xuất hiện sau khi có volume và nhóm từ khoá.");

    for(int i = 0; i < g_ntfs_hit_count; i++) {
        char size[32];
        lv_obj_t *hit = tools_card(root);
        lv_obj_set_flex_flow(hit, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(hit, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        label = vqeaf_label(hit, g_ntfs_hits[i].path, VQEAF_FONT_CAPTION,
                            VQEAF_C_TEXT);
        lv_obj_set_flex_grow(label, 1);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        tools_format_bytes(g_ntfs_hits[i].size, size, sizeof(size));
        vqeaf_label(hit, size, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        if(g_ntfs_hits[i].is_dir)
            vqeaf_glyph(hit, VQEAF_SYM_FOLDER, VQEAF_ICON_SM,
                        VQEAF_C_WARN);
        vqeaf_on_click(hit, ntfs_hit_cb, (void *)(intptr_t)i);
    }
}

/* ---- task manager -------------------------------------------------------- */

static vqeaf_process_t g_procs[48];
static int g_proc_count;
static int g_proc_selected = -1;
static char g_proc_msg[256];

static void task_home_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_show(VQEAF_PAGE_APPS);
}

static void task_reload(void)
{
    g_proc_count = vqeaf_sys_processes(g_procs, 48);
    if(g_proc_count < 0) g_proc_count = 0;
    if(g_proc_selected >= g_proc_count) g_proc_selected = -1;
}

static void task_refresh_cb(lv_event_t *event)
{
    (void)event;
    task_reload();
    snprintf(g_proc_msg, sizeof(g_proc_msg),
             "Đã đọc %d tiến trình từ /proc.", g_proc_count);
    vqeaf_ui_show(VQEAF_PAGE_TASK_MANAGER);
}

static void task_row_cb(lv_event_t *event)
{
    g_proc_selected = (int)(intptr_t)lv_event_get_user_data(event);
    vqeaf_ui_show(VQEAF_PAGE_TASK_MANAGER);
}

static void task_kill_action(void *data)
{
    int pid = (int)(intptr_t)data;
    int rc = vqeaf_sys_signal_pid(pid);
    if(rc == 0)
        snprintf(g_proc_msg, sizeof(g_proc_msg),
                 "Đã gửi SIGTERM tới PID %d.", pid);
    else
        snprintf(g_proc_msg, sizeof(g_proc_msg),
                 "Không dừng được PID %d (mã lỗi %d).", pid, -rc);
    vqeaf_ui_show(VQEAF_PAGE_TASK_MANAGER);
}

static void task_kill_cb(lv_event_t *event)
{
    char body[320];
    (void)event;
    if(g_proc_selected < 0 || g_proc_selected >= g_proc_count) {
        snprintf(g_proc_msg, sizeof(g_proc_msg), "Chưa chọn tiến trình.");
        vqeaf_ui_show(VQEAF_PAGE_TASK_MANAGER);
        return;
    }
    if(g_procs[g_proc_selected].pid <= 1) {
        snprintf(g_proc_msg, sizeof(g_proc_msg),
                 "Từ chối: không dừng PID hệ thống %d.",
                 g_procs[g_proc_selected].pid);
        vqeaf_ui_show(VQEAF_PAGE_TASK_MANAGER);
        return;
    }
    snprintf(body, sizeof(body),
             "Dừng PID %d (%s)? Chỉ gửi SIGTERM, không dùng SIGKILL. "
             "PID 1 và tiến trình hiện tại luôn bị từ chối.",
             g_procs[g_proc_selected].pid, g_procs[g_proc_selected].name);
    tools_confirm("Kết thúc tác vụ", body, task_kill_action,
                  (void *)(intptr_t)g_procs[g_proc_selected].pid);
}

void vqeaf_tools_build_task_manager(lv_obj_t *content)
{
    lv_obj_t *root = tools_root(content);
    lv_obj_t *card, *row, *label;
    char rss[32];

    task_reload();
    tools_title(root, "Quản lý tác vụ", task_home_cb);
    tools_note(root,
        "Đọc danh sách thật từ /proc. Chạm một tiến trình để chọn, "
        "rồi bấm Dừng; hệ thống không gửi tín hiệu khi chưa xác nhận.");

    card = tools_card(root);
    row = vqeaf_row(card);
    label = vqeaf_label(row, "Tiến trình", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_set_flex_grow(label, 1);
    vqeaf_glyph(row, VQEAF_SYM_REFRESH, VQEAF_ICON_SM, VQEAF_C_ACCENT);
    vqeaf_on_click(row, task_refresh_cb, NULL);

    if(g_proc_count <= 0) {
        tools_status(card, "Không đọc được danh sách /proc trên môi trường này.");
    }
    for(int i = 0; i < g_proc_count; i++) {
        lv_obj_t *entry = tools_card(root);
        lv_obj_set_flex_flow(entry, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(entry, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(entry, 10, 0);
        lv_obj_set_style_border_width(entry,
            i == g_proc_selected ? 3 : 1, 0);
        lv_obj_set_style_border_color(entry,
            i == g_proc_selected ? VQEAF_C_ACCENT : VQEAF_C_LINE, 0);
        snprintf(rss, sizeof(rss), "%llu KiB",
                 (unsigned long long)g_procs[i].rss_kb);
        label = vqeaf_label(entry, g_procs[i].name, VQEAF_FONT_BODY,
                            VQEAF_C_TEXT);
        lv_obj_set_flex_grow(label, 1);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        vqeaf_label(entry, rss, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        vqeaf_label(entry, g_procs[i].state, VQEAF_FONT_CAPTION,
                    VQEAF_C_TEXT_DIM);
        label = vqeaf_label(entry, "PID", VQEAF_FONT_CAPTION,
                            VQEAF_C_TEXT_MUTE);
        (void)label;
        snprintf(rss, sizeof(rss), "%d", g_procs[i].pid);
        vqeaf_label(entry, rss, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
        vqeaf_on_click(entry, task_row_cb, (void *)(intptr_t)i);
    }

    tools_status(root, g_proc_msg[0] != '\0' ? g_proc_msg :
        "Chưa có tiến trình nào được chọn.");
    {
        lv_obj_t *actions = vqeaf_row(root);
        tools_button(actions, "Dừng", 280, task_kill_cb, NULL);
        tools_button(actions, "Làm mới", 280, task_refresh_cb, NULL);
    }
}

/* ---- disk cleanup -------------------------------------------------------- */

static const char *g_cleanup_targets[8];
static int g_cleanup_target_count;
static int g_cleanup_target;
static vqeaf_cleanup_item_t g_cleanup_items[32];
static int g_cleanup_count;
static uint8_t g_cleanup_selected[32];
static uint64_t g_cleanup_total;
static vqeaf_cleanup_item_t g_cleanup_pending[10];
static int g_cleanup_pending_count;
static char g_cleanup_msg[256];

static void cleanup_home_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_ui_show(VQEAF_PAGE_APPS);
}

static void cleanup_build_targets(void)
{
    vqeaf_mount_t mounts[8];
    int count;

    g_cleanup_target_count = 0;
    g_cleanup_targets[g_cleanup_target_count++] = "/tmp";
    g_cleanup_targets[g_cleanup_target_count++] = "/var/cache";
    g_cleanup_targets[g_cleanup_target_count++] = "/run";
    count = vqeaf_sys_mounts(mounts, 8, 1);
    if(count > 0 && g_cleanup_target_count < 8)
        g_cleanup_targets[g_cleanup_target_count++] = mounts[0].path;
    if(g_cleanup_target >= g_cleanup_target_count) g_cleanup_target = 0;
}

static void cleanup_target_cb(lv_event_t *event)
{
    g_cleanup_target = (int)(intptr_t)lv_event_get_user_data(event);
    g_cleanup_count = 0;
    g_cleanup_total = 0;
    vqeaf_ui_show(VQEAF_PAGE_DISK_CLEANUP);
}

static void cleanup_scan_cb(lv_event_t *event)
{
    int rc;
    (void)event;
    g_cleanup_count = 0;
    g_cleanup_total = 0;
    memset(g_cleanup_selected, 0, sizeof(g_cleanup_selected));
    rc = vqeaf_sys_cleanup_scan(g_cleanup_targets[g_cleanup_target],
                                g_cleanup_items, 32, &g_cleanup_total);
    if(rc < 0) {
        g_cleanup_count = 0;
        snprintf(g_cleanup_msg, sizeof(g_cleanup_msg),
                 "Không quét được %s (mã lỗi %d).",
                 g_cleanup_targets[g_cleanup_target], -rc);
    }
    else {
        g_cleanup_count = rc;
        snprintf(g_cleanup_msg, sizeof(g_cleanup_msg),
                 "%d ứng viên trên %s; mới chỉ quét, chưa di chuyển.",
                 rc, g_cleanup_targets[g_cleanup_target]);
    }
    vqeaf_ui_show(VQEAF_PAGE_DISK_CLEANUP);
}

static void cleanup_toggle_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if(index < 0 || index >= g_cleanup_count) return;
    g_cleanup_selected[index] = g_cleanup_selected[index] ? 0 : 1;
    vqeaf_ui_show(VQEAF_PAGE_DISK_CLEANUP);
}

static void cleanup_quarantine_action(void *data)
{
    char report[320];
    int moved;
    (void)data;
    moved = vqeaf_sys_cleanup_quarantine(
        g_cleanup_targets[g_cleanup_target], g_cleanup_pending,
        g_cleanup_pending_count, report, sizeof(report));
    snprintf(g_cleanup_msg, sizeof(g_cleanup_msg), "%s", report);
    if(moved > 0) memset(g_cleanup_selected, 0, sizeof(g_cleanup_selected));
    vqeaf_ui_show(VQEAF_PAGE_DISK_CLEANUP);
}

static void cleanup_quarantine_cb(lv_event_t *event)
{
    char body[512];
    int offset = 0;
    (void)event;
    g_cleanup_pending_count = 0;
    for(int i = 0; i < g_cleanup_count &&
                    g_cleanup_pending_count < 10; i++) {
        if(g_cleanup_selected[i])
            g_cleanup_pending[g_cleanup_pending_count++] = g_cleanup_items[i];
    }
    if(g_cleanup_pending_count == 0) {
        snprintf(g_cleanup_msg, sizeof(g_cleanup_msg),
                 "Chưa chọn mục nào để đưa vào thùng rác.");
        vqeaf_ui_show(VQEAF_PAGE_DISK_CLEANUP);
        return;
    }
    offset += snprintf(body + offset, sizeof(body) - (size_t)offset,
                       "Chuyển %d mục vào thùng rác trên %s. "
                       "Không xoá vĩnh viễn; có thể khôi phục.\n",
                       g_cleanup_pending_count,
                       g_cleanup_targets[g_cleanup_target]);
    for(int i = 0; i < g_cleanup_pending_count && i < 5; i++)
        offset += snprintf(body + offset, sizeof(body) - (size_t)offset,
                           "- %s\n", g_cleanup_pending[i].path);
    if(g_cleanup_pending_count > 5)
        snprintf(body + offset, sizeof(body) - (size_t)offset,
                 "... và %d mục khác.", g_cleanup_pending_count - 5);
    tools_confirm("Dọn dẹp ổ đĩa", body, cleanup_quarantine_action, NULL);
}

static void cleanup_restore_action(void *data)
{
    char report[256];
    (void)data;
    vqeaf_sys_cleanup_restore(NULL, report, sizeof(report));
    snprintf(g_cleanup_msg, sizeof(g_cleanup_msg), "%s", report);
    vqeaf_ui_show(VQEAF_PAGE_DISK_CLEANUP);
}

static void cleanup_restore_cb(lv_event_t *event)
{
    (void)event;
    if(vqeaf_sys_quarantine_dir() == NULL || vqeaf_sys_quarantine_dir()[0] == '\0') {
        snprintf(g_cleanup_msg, sizeof(g_cleanup_msg),
                 "Chưa có mẻ dọn nào trong phiên này để khôi phục.");
        vqeaf_ui_show(VQEAF_PAGE_DISK_CLEANUP);
        return;
    }
    tools_confirm("Khôi phục",
                  "Đưa các mục trong mẻ dọn gần nhất về vị trí cũ. "
                  "Mục đã tồn tại ở vị trí cũ sẽ được bỏ qua.",
                  cleanup_restore_action, NULL);
}

void vqeaf_tools_build_disk_cleanup(lv_obj_t *content)
{
    lv_obj_t *root = tools_root(content);
    lv_obj_t *card, *row, *label;
    char bytes[32], selected[32];
    int chosen = 0;

    cleanup_build_targets();
    tools_title(root, "Dọn dẹp ổ đĩa", cleanup_home_cb);
    tools_note(root,
        "Quét trước, không tự xoá. Mỗi mẻ tối đa 10 mục và chỉ chuyển vào "
        "thùng rác có manifest để khôi phục; thư mục hệ thống không nằm trong danh sách.");

    card = tools_card(root);
    vqeaf_label(card, "Ổ cần quét", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    row = vqeaf_row(card);
    lv_obj_set_style_pad_column(row, 10, 0);
    for(int i = 0; i < g_cleanup_target_count; i++)
        tools_button(row, g_cleanup_targets[i], 210,
                     cleanup_target_cb, (void *)(intptr_t)i);
    tools_button(card, "Quét ổ", lv_pct(100), cleanup_scan_cb, NULL);

    for(int i = 0; i < g_cleanup_count; i++)
        if(g_cleanup_selected[i]) chosen++;
    tools_format_bytes(g_cleanup_total, bytes, sizeof(bytes));
    snprintf(selected, sizeof(selected), "%d/%d mục đã chọn", chosen,
             g_cleanup_count);
    card = tools_card(root);
    row = vqeaf_row(card);
    label = vqeaf_label(row, selected, VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    lv_obj_set_flex_grow(label, 1);
    snprintf(selected, sizeof(selected), "%s ứng viên", bytes);
    vqeaf_label(row, selected, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);

    if(g_cleanup_count > 0) {
        for(int i = 0; i < g_cleanup_count; i++) {
            lv_obj_t *entry = tools_card(root);
            lv_obj_set_flex_flow(entry, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(entry, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(entry, 10, 0);
            vqeaf_glyph(entry,
                g_cleanup_selected[i] ? VQEAF_SYM_CHECK : VQEAF_SYM_CLOSE,
                VQEAF_ICON_SM,
                g_cleanup_selected[i] ? VQEAF_C_ACCENT : VQEAF_C_TEXT_DIM);
            label = vqeaf_label(entry, g_cleanup_items[i].path,
                                VQEAF_FONT_CAPTION, VQEAF_C_TEXT);
            lv_obj_set_flex_grow(label, 1);
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
            tools_format_bytes(g_cleanup_items[i].size, bytes, sizeof(bytes));
            vqeaf_label(entry, bytes, VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
            vqeaf_on_click(entry, cleanup_toggle_cb, (void *)(intptr_t)i);
        }
    }
    else {
        tools_status(root, "Chưa có ứng viên dọn dẹp. Bấm Quét ổ để đọc thực tế.");
    }

    tools_status(root, g_cleanup_msg[0] != '\0' ? g_cleanup_msg :
        "Chưa có thay đổi nào trên đĩa.");
    {
        lv_obj_t *actions = vqeaf_row(root);
        tools_button(actions, "Đưa vào thùng rác", 280,
                     cleanup_quarantine_cb, NULL);
        tools_button(actions, "Khôi phục", 280,
                     cleanup_restore_cb, NULL);
    }
}
