/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_shutdown.c - Windows-style restart / shutdown.
 *
 * "Restart" on a desktop OS is not an instant black screen: the shell stays
 * up long enough to show a restarting screen, services close in order, the
 * filesystem is flushed and only then is the machine reset. This module keeps
 * that order observable. The stage label names work the OS really does; no
 * fake progress percentage is ever drawn.
 */
#include "vqeaf_shutdown.h"

#include "lvgl.h"
#include "vqeaf_theme.h"

/* The overlay must stay up long enough to be seen and to let PID 1 publish
 * the last boot-log line to QEAFRESCUE before the kernel goes away. */
#define VQEAF_SHUTDOWN_FLUSH_MS 700u
#define VQEAF_SHUTDOWN_SYNC_MS 1200u

typedef enum {
    PHASE_IDLE = 0,
    PHASE_SHOW,        /* restarting screen, input ignored */
    PHASE_FLUSH,       /* boot log + RESPONSE.TXT on the rescue volume */
    PHASE_SYNC,        /* sync() and the machine reset */
    PHASE_WAIT_RESET,  /* reset accepted, waiting for the hardware */
    PHASE_FAILED       /* kernel refused the reset */
} shutdown_phase_t;

static lv_obj_t *g_overlay;
static lv_obj_t *g_spinner;
static lv_obj_t *g_title;
static lv_obj_t *g_detail;
static lv_obj_t *g_stage;
static lv_timer_t *g_timer;
static shutdown_phase_t g_phase;
static vqeaf_shutdown_action_t g_action;
static uint32_t g_started_ms;
static vqeaf_shutdown_stage_fn g_stage_hook;
static vqeaf_shutdown_final_fn g_final_hook;

static int is_restart(vqeaf_shutdown_action_t action)
{
    return action != VQEAF_SHUTDOWN_POWER_OFF;
}

const char *vqeaf_shutdown_title(void)
{
    if(g_phase == PHASE_IDLE) return "";
    if(g_phase == PHASE_FAILED)
        return is_restart(g_action) ? "Không thể khởi động lại" :
                                     "Không thể tắt nguồn";
    return is_restart(g_action) ? "Đang khởi động lại" : "Đang tắt nguồn";
}

static void set_stage_text(const char *text)
{
    if(g_stage != NULL) lv_label_set_text(g_stage, text);
}

static void destroy_overlay(void)
{
    if(g_overlay != NULL) {
        lv_obj_delete(g_overlay);
        g_overlay = NULL;
    }
    g_spinner = NULL;
    g_title = NULL;
    g_detail = NULL;
    g_stage = NULL;
}

static void back_cb(lv_event_t *event)
{
    (void)event;
    vqeaf_shutdown_cancel();
}

static void show_failed(void)
{
    g_phase = PHASE_FAILED;
    /* A failure is final: stop the spinning indicator. */
    if(g_spinner != NULL) lv_obj_add_flag(g_spinner, LV_OBJ_FLAG_HIDDEN);
    if(g_title != NULL)
        lv_label_set_text(g_title, vqeaf_shutdown_title());
    if(g_detail != NULL)
        lv_label_set_text(g_detail,
            "Hệ thống từ chối yêu cầu. Dữ liệu đã được đồng bộ.");
    set_stage_text("Quay lại màn hình chính để thử lại.");
    if(g_overlay != NULL) {
        lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *back = vqeaf_obj(g_overlay);
        lv_obj_set_size(back, 260, 64);
        lv_obj_align(back, LV_ALIGN_CENTER, 0, 210);
        lv_obj_set_style_bg_color(back, VQEAF_C_SURFACE_3, 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(back, 32, 0);
        vqeaf_label(back, "Quay lại", VQEAF_FONT_BODY, VQEAF_C_TEXT);
        lv_obj_set_flex_flow(back, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        vqeaf_on_click(back, back_cb, NULL);
    }
}

static void build_overlay(void)
{
    lv_display_t *disp = lv_display_get_default();
    int w, h;

    if(disp == NULL) return;
    w = lv_display_get_horizontal_resolution(disp);
    h = lv_display_get_vertical_resolution(disp);

    g_overlay = vqeaf_obj(lv_layer_top());
    lv_obj_set_size(g_overlay, w, h);
    lv_obj_set_pos(g_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_overlay, VQEAF_C_BG, 0);
    lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
    /* Full-screen and clickable: every tap during shutdown is swallowed. */
    lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_CLICKABLE);

    g_spinner = lv_arc_create(g_overlay);
    lv_obj_set_size(g_spinner, 96, 96);
    lv_obj_align(g_spinner, LV_ALIGN_CENTER, 0, -140);
    lv_arc_set_bg_angles(g_spinner, 0, 360);
    lv_arc_set_angles(g_spinner, 0, 60);
    lv_arc_set_rotation(g_spinner, 0);
    lv_obj_set_style_arc_width(g_spinner, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_spinner, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_spinner, VQEAF_C_SURFACE_3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_spinner, VQEAF_C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_spinner, true, LV_PART_INDICATOR);
    lv_obj_remove_style(g_spinner, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_spinner, LV_OBJ_FLAG_CLICKABLE);

    g_title = vqeaf_label(g_overlay, vqeaf_shutdown_title(),
                          VQEAF_FONT_HEADLINE, VQEAF_C_TEXT);
    lv_obj_align(g_title, LV_ALIGN_CENTER, 0, 0);
    g_detail = vqeaf_label(g_overlay,
                           "Vui lòng đợi, không ngắt nguồn thiết bị.",
                           VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
    lv_obj_align(g_detail, LV_ALIGN_CENTER, 0, 62);
    g_stage = vqeaf_label(g_overlay, "Đang đóng các dịch vụ...",
                          VQEAF_FONT_CAPTION, VQEAF_C_ACCENT);
    lv_obj_align(g_stage, LV_ALIGN_CENTER, 0, 118);
    lv_obj_t *brand = vqeaf_label(g_overlay, "VQEAF OS",
                                  VQEAF_FONT_BODY, VQEAF_C_TEXT_MUTE);
    lv_obj_align(brand, LV_ALIGN_BOTTOM_MID, 0, -120);
}

void vqeaf_shutdown_set_hooks(vqeaf_shutdown_stage_fn stage,
                              vqeaf_shutdown_final_fn final)
{
    g_stage_hook = stage;
    g_final_hook = final;
}

static void shutdown_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    vqeaf_shutdown_tick();
}

void vqeaf_shutdown_init(void)
{
    destroy_overlay();
    g_phase = PHASE_IDLE;
    g_action = VQEAF_SHUTDOWN_NONE;
    if(g_timer == NULL)
        g_timer = lv_timer_create(shutdown_timer_cb, 16, NULL);
}

void vqeaf_shutdown_request(vqeaf_shutdown_action_t action)
{
    if(action == VQEAF_SHUTDOWN_NONE || g_phase != PHASE_IDLE) return;
    g_action = action;
    g_phase = PHASE_SHOW;
    g_started_ms = lv_tick_get();
    if(g_stage_hook != NULL) g_stage_hook(action, "request");
    build_overlay();
    set_stage_text("Đang đóng các dịch vụ...");
}

void vqeaf_shutdown_cancel(void)
{
    if(g_phase == PHASE_IDLE) return;
    if(g_stage_hook != NULL) g_stage_hook(g_action, "cancelled");
    destroy_overlay();
    g_phase = PHASE_IDLE;
    g_action = VQEAF_SHUTDOWN_NONE;
}

int vqeaf_shutdown_active(void)
{
    return g_phase != PHASE_IDLE;
}

vqeaf_shutdown_action_t vqeaf_shutdown_action(void)
{
    return g_action;
}

void vqeaf_shutdown_tick(void)
{
    uint32_t elapsed;

    if(g_phase == PHASE_IDLE || g_phase == PHASE_FAILED ||
       g_phase == PHASE_WAIT_RESET) return;
    elapsed = lv_tick_get() - g_started_ms;
    if(g_spinner != NULL)
        lv_arc_set_rotation(g_spinner,
                            (int32_t)((elapsed / 16u * 6u) % 360u));

    if(g_phase == PHASE_SHOW && elapsed >= VQEAF_SHUTDOWN_FLUSH_MS) {
        g_phase = PHASE_FLUSH;
        if(g_stage_hook != NULL) g_stage_hook(g_action, "flush-log");
        set_stage_text("Đang lưu nhật ký hệ thống...");
    }
    if(g_phase == PHASE_FLUSH && elapsed >= VQEAF_SHUTDOWN_SYNC_MS) {
        int result = -1;
        g_phase = PHASE_SYNC;
        if(g_stage_hook != NULL) g_stage_hook(g_action, "sync");
        set_stage_text("Đang đồng bộ dữ liệu...");
        if(g_final_hook != NULL) result = g_final_hook(g_action);
        if(result < 0) {
            if(g_stage_hook != NULL) g_stage_hook(g_action, "refused");
            show_failed();
        }
        else {
            g_phase = PHASE_WAIT_RESET;
            set_stage_text("Đang chờ phần cứng khởi động lại...");
        }
    }
}
