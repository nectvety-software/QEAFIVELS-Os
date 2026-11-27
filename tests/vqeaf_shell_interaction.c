/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_shell_interaction.c - drive the real shell with a pointer.
 *
 * Built by scripts/test_violet_shell.py against the same sources as the
 * initramfs shell (state providers stubbed like vqeaf_ui_host.c). Verifies the
 * tap/long-press contract by execution: tap opens, long-press shows the info
 * sheet, stock Home widget/grid/pagination, and Home dock move mode.
 * Key steps are dumped as PPM for visual inspection.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "vqeaf_state.h"
#include "vqeaf_ui.h"
#include "vqeaf_pages.h"
#include "vqeaf_security.h"
#include "vqeaf_theme.h"
#include "vqeaf_components.h"
#include "vqeaf_drag.h"
#include "icons/vqeaf_app_icons.h"
#include "vqeaf_tools_ui.h"
#include "vqeaf_ime.h"
#include "vqeaf_fonts.h"
#include "vqeaf_qeapp.h"
#include <vqeaf/qeapp_package.h>
#include "vqeaf_qeapp_device.h"
#include <vqeaf/settings.h>
#include "vqeaf_shutdown.h"
#include "vqeaf_host_platform.h"
#include "vqeaf_host_sync.h"

#ifndef __linux__
void vqeaf_install_render(int state);
void vqeaf_install_render_close(void);
#endif

/* Test doubles for platform haptics (host gate has no vibrator). */
int vio_haptics_available(void) { return 0; }
int vio_haptic_pulse(int ms) { (void)ms; return 0; }

/* Capture and camera are Linux device paths (DRM scanout / V4L2), so the host
 * gate reports "not available" exactly like vqeaf_ui_host.c does. */
int vio_rec_start(int fps, int seconds)
{
    (void)fps;
    (void)seconds;
    return -1;
}

int vio_rec_stop(void) { return -1; }
int vio_rec_active(void) { return 0; }
const char *vio_rec_status(void) { return "host renderer: no DRM scanout"; }
int vio_screenshot_now(void) { return -1; }

static int g_sound_write_fail;

static int test_sound_open(void *context)
{
    (void)context;
    return 0;
}

static int test_sound_write(void *context, const int16_t *samples, size_t count)
{
    (void)context;
    (void)samples;
    if(g_sound_write_fail) return -1;
    return (int)count;
}

static void test_sound_close(void *context)
{
    (void)context;
}

static const vqeaf_sound_backend_t test_sound_backend = {
    .name = "PCM kiểm thử",
    .open = test_sound_open,
    .write = test_sound_write,
    .close = test_sound_close,
};

int vqeaf_camera_query_caps(char *buf, int cap)
{
    if(buf != NULL && cap > 0)
        snprintf(buf, (size_t)cap, "host renderer: no V4L2 nodes");
    return -1;
}

int vqeaf_camera_create_session(int32_t *session_handle, char *err, int err_cap)
{
    if(session_handle != NULL) *session_handle = -1;
    if(err != NULL && err_cap > 0)
        snprintf(err, (size_t)err_cap, "host renderer: no cam-req-mgr");
    return -1;
}

int vqeaf_camera_probe(char *buf, int cap)
{
    if(buf != NULL && cap > 0)
        snprintf(buf, (size_t)cap, "host renderer: no V4L2 nodes");
    return -1;
}

vqeaf_state_t vqeaf_state = {
    .clock_text = "09:41",
    .date_text = "Thứ 3, 10 Tháng 11, 2026",
    .wifi_up = 1,
    .brightness = 85,
};

void vqeaf_state_refresh(void)
{
}

int vqeaf_state_set_brightness(int percent)
{
    vqeaf_state.brightness = percent;
    return 0;
}

/* Stands in for the platform's applied gadget mode. The device wires this to
 * usb_gadget_mode(); the test owns it so it can prove the row follows the
 * mode main actually set. */
static int g_usb_mode = -1;

static int fake_usb_mode(void)
{
    return g_usb_mode;
}

/* Device-only native runtime stubs (same contract as vqeaf_ui_host.c): the
 * host cannot fork/map native modules, so the QEAPP page renders its honest
 * empty state instead of a fabricated frame. */
int vqeaf_qeapp_device_session_start(const uint8_t *package, size_t size,
                                     uint32_t effective_permissions,
                                     char *error, size_t error_cap)
{
    (void)package; (void)size; (void)effective_permissions;
    if(error != NULL && error_cap > 0) error[0] = '\0';
    return 1;
}

int vqeaf_qeapp_device_session_send(const vqeaf_qeapp_device_input_t *event)
{
    (void)event;
    return -1;
}

int vqeaf_qeapp_device_session_poll(char *error, size_t error_cap)
{
    if(error != NULL && error_cap > 0) error[0] = '\0';
    return 0;
}

void vqeaf_qeapp_device_session_stop(void) {}
int vqeaf_qeapp_device_session_active(void) { return 0; }
uint32_t vqeaf_qeapp_device_session_frame_seq(void) { return 0u; }

int vqeaf_qeapp_device_launch(const uint8_t *package, size_t size,
                              uint32_t effective_permissions,
                              char *report, size_t report_cap,
                              char *error, size_t error_cap)
{
    (void)package;
    (void)size;
    (void)effective_permissions;
    if(report != NULL && report_cap > 0) report[0] = '\0';
    if(error != NULL && error_cap > 0)
        snprintf(error, error_cap, "native execution requires the device.");
    return -1;
}

const uint32_t *vqeaf_qeapp_device_frame(void)
{
    return NULL;
}

const char *vqeaf_qeapp_device_lifecycle(void)
{
    return "";
}

const char *vqeaf_qeapp_device_process(void)
{
    return "";
}

uint32_t vqeaf_qeapp_device_hash(void)
{
    return 0u;
}

int vqeaf_qeapp_device_page_pending(void)
{
    return 0;
}

void vqeaf_qeapp_device_page_ack(void)
{
}

static uint32_t *framebuffer;
static lv_display_t *display;
static lv_indev_t *pointer;
static uint32_t g_ms;
static lv_point_t g_point;
static int g_pressed;
static int g_failures;
static const char *g_outdir;

/* Platform hooks for the restart sequence: record the real stage order and
 * the machine-reset call instead of rebooting the test host. */
static int g_stage_count;
static char g_stages[8][32];
static int g_final_count;
static int g_final_result;
static vqeaf_shutdown_action_t g_final_action;

static void test_shutdown_stage(vqeaf_shutdown_action_t action,
                                const char *stage)
{
    (void)action;
    if(g_stage_count < 8)
        snprintf(g_stages[g_stage_count], sizeof(g_stages[0]), "%s", stage);
    g_stage_count++;
}

static int test_shutdown_final(vqeaf_shutdown_action_t action)
{
    g_final_action = action;
    g_final_count++;
    return g_final_result;
}

static void reset_shutdown_hooks(void)
{
    g_stage_count = 0;
    g_final_count = 0;
    g_final_result = 0;
    g_final_action = VQEAF_SHUTDOWN_NONE;
    memset(g_stages, 0, sizeof(g_stages));
    vqeaf_shutdown_set_hooks(test_shutdown_stage, test_shutdown_final);
}

static int stage_seen(const char *stage)
{
    int i;
    for(i = 0; i < g_stage_count && i < 8; i++)
        if(strcmp(g_stages[i], stage) == 0) return 1;
    return 0;
}

#define CHECK(cond, msg) do { \
    if(cond) { printf("ok   %s\n", msg); } \
    else { printf("FAIL %s\n", msg); g_failures++; } \
} while(0)

static void flush_cb(lv_display_t *disp, const lv_area_t *area,
                     uint8_t *px_map)
{
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void)
{
    return g_ms;
}

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    /* Mirror the device touch read cb (main_violet.c): a real press is user
     * activity and must reset the screen-timeout idle clock, otherwise a long
     * typing session sleeps the shell mid-keystroke. */
    if(g_pressed) vqeaf_ui_activity();
    data->point = g_point;
    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void step(int ms)
{
    int i;
    for(i = 0; i < ms / 10; i++) {
        g_ms += 10;
        lv_timer_handler();
    }
}

static void settle(void)
{
    lv_obj_update_layout(lv_screen_active());
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display);
}

static void tap(int x, int y)
{
    g_point.x = x;
    g_point.y = y;
    g_pressed = 1;
    step(60);
    g_pressed = 0;
    step(60);
}

/* A real finger drag: press, move in `steps` polls, then lift. */
static void swipe(int x0, int y0, int x1, int y1, int steps)
{
    int i;

    g_point.x = x0;
    g_point.y = y0;
    g_pressed = 1;
    step(30);
    for(i = 1; i <= steps; i++) {
        g_point.x = x0 + (x1 - x0) * i / steps;
        g_point.y = y0 + (y1 - y0) * i / steps;
        step(20);
    }
    g_pressed = 0;
    step(60);
}

static void long_press(int x, int y)
{
    g_point.x = x;
    g_point.y = y;
    g_pressed = 1;
    step(1100);   /* device long-press time is 900 ms */
    g_pressed = 0;
    step(60);
}

/* Hold past the long-press threshold, then move without lifting. */
static void hold_drag_to(int x0, int y0, int x1, int y1, int steps)
{
    int i;

    g_point.x = x0;
    g_point.y = y0;
    g_pressed = 1;
    step(1000);
    for(i = 1; i <= steps; i++) {
        g_point.x = x0 + (x1 - x0) * i / steps;
        g_point.y = y0 + (y1 - y0) * i / steps;
        step(20);
    }
}

/* Hold the finger down without lifting: used to observe Material's ripple. */
static void press_hold(int x, int y, int ms)
{
    g_point.x = x;
    g_point.y = y;
    g_pressed = 1;
    step(ms);
}

static void release_hold(void)
{
    g_pressed = 0;
    step(80);
}

static lv_obj_t *find_label(lv_obj_t *root, const char *text, int min_y)
{
    uint32_t i, n;
    lv_area_t area;

    if(lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if(lv_obj_check_type(root, &lv_label_class)) {
        const char *t = lv_label_get_text(root);
        lv_obj_get_coords(root, &area);
        if(t != NULL && strcmp(t, text) == 0 && area.y1 >= min_y)
            return root;
    }
    n = lv_obj_get_child_count(root);
    for(i = 0; i < n; i++) {
        lv_obj_t *hit = find_label(lv_obj_get_child(root, i), text, min_y);
        if(hit != NULL) return hit;
    }
    return NULL;
}

/* First textarea in the tree (the Notes field), so the IME can bind it. */
static lv_obj_t *find_label_topmost(lv_obj_t *root, const char *text, int min_y)
{
    uint32_t i = lv_obj_get_child_count(root);
    lv_area_t area;
    while(i > 0) {
        lv_obj_t *child = lv_obj_get_child(root, --i);
        lv_obj_t *hit = find_label_topmost(child, text, min_y);
        if(hit != NULL) return hit;
    }
    if(lv_obj_check_type(root, &lv_label_class)) {
        const char *t = lv_label_get_text(root);
        lv_obj_get_coords(root, &area);
        if(t != NULL && strcmp(t, text) == 0 && area.y1 >= min_y)
            return root;
    }
    return NULL;
}

static lv_obj_t *find_textarea(lv_obj_t *root)
{
    uint32_t i, n;
    if(lv_obj_check_type(root, &lv_textarea_class)) return root;
    n = lv_obj_get_child_count(root);
    for(i = 0; i < n; i++) {
        lv_obj_t *hit = find_textarea(lv_obj_get_child(root, i));
        if(hit != NULL) return hit;
    }
    return NULL;
}

/* Finds an IME key by its printed label (SHIFT, ?123, SPACE, DEL, ...). */
static lv_obj_t *find_ime_key(lv_obj_t *root, const char *label)
{
    uint32_t i, n, j, m;
    n = lv_obj_get_child_count(root);
    for(i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(root, i);
        m = lv_obj_get_child_count(child);
        for(j = 0; j < m; j++) {
            lv_obj_t *lab = lv_obj_get_child(child, j);
            if(lv_obj_check_type(lab, &lv_label_class) &&
               strcmp(lv_label_get_text(lab), label) == 0) return child;
        }
        lv_obj_t *hit = find_ime_key(child, label);
        if(hit != NULL) return hit;
    }
    return NULL;
}

/* Confirms a special key really carries a Material Icons glyph, not just the
 * legacy text caption. The font pointer makes this independent of pixel diff. */
static int ime_key_has_icon(lv_obj_t *key, const char *symbol)
{
    uint32_t i, n;
    if(key == NULL) return 0;
    n = lv_obj_get_child_count(key);
    for(i = 0; i < n; i++) {
        lv_obj_t *lab = lv_obj_get_child(key, i);
        if(lv_obj_check_type(lab, &lv_label_class) &&
           strcmp(lv_label_get_text(lab), symbol) == 0 &&
           lv_obj_get_style_text_font(lab, 0) == VQEAF_ICON_SM) return 1;
    }
    return 0;
}

/* Centre of the clickable item that owns a label (tile or button). */
static int label_target(lv_obj_t *label, int *x, int *y)
{
    lv_obj_t *item;
    lv_area_t area;

    if(label == NULL) return -1;
    item = lv_obj_get_parent(label);
    if(item == NULL) return -1;
    lv_obj_scroll_to_view_recursive(item, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_get_coords(item, &area);
    *x = (area.x1 + area.x2) / 2;
    *y = (area.y1 + area.y2) / 2;
    return 0;
}

static int label_self_target(lv_obj_t *label, int *x, int *y);

static int tap_text(lv_obj_t *root, const char *text)
{
    lv_obj_t *label = find_label(root, text, 0);
    int x, y;
    int rc;
    if(label == NULL) return -1;
    rc = lv_obj_has_flag(label, LV_OBJ_FLAG_CLICKABLE) ?
         label_self_target(label, &x, &y) : label_target(label, &x, &y);
    if(rc != 0) return -1;
    tap(x, y);
    settle();
    return 0;
}

static int enter_digits(const char *digits)
{
    for(; *digits != '\0'; digits++) {
        char key[2] = { *digits, '\0' };
        if(tap_text(lv_screen_active(), key) != 0) return -1;
    }
    return 0;
}

static int first_child_target(lv_obj_t *parent, int *x, int *y)
{
    lv_obj_t *child = lv_obj_get_child(parent, 0);
    lv_area_t area;
    if(child == NULL) return -1;
    lv_obj_update_layout(lv_screen_active());
    lv_obj_get_coords(child, &area);
    *x = (area.x1 + area.x2) / 2;
    *y = (area.y1 + area.y2) / 2;
    return 0;
}

/* Header labels sit at the far right of a full-width row; label_target()
 * aims at the row centre, so small action labels need their own coords. */
static int label_self_target(lv_obj_t *label, int *x, int *y)
{
    lv_area_t area;
    if(label == NULL) return -1;
    lv_obj_update_layout(lv_screen_active());
    lv_obj_get_coords(label, &area);
    *x = (area.x1 + area.x2) / 2;
    *y = (area.y1 + area.y2) / 2;
    return 0;
}

static int modal_up(void)
{
    return lv_obj_get_child_count(lv_layer_top()) > 0;
}

static lv_obj_t *find_drag_ghost(void)
{
    lv_obj_t *layer = lv_layer_top();
    uint32_t count = lv_obj_get_child_count(layer);
    for(uint32_t i = 0; i < count; i++) {
        lv_obj_t *obj = lv_obj_get_child(layer, (int32_t)i);
        if(lv_obj_get_width(obj) == 104 && lv_obj_get_height(obj) == 104 &&
           !lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE))
            return obj;
    }
    return NULL;
}

static lv_obj_t *find_label_contains(lv_obj_t *root, const char *needle,
                                     int min_y)
{
    uint32_t i, n;
    lv_area_t area;

    if(lv_obj_check_type(root, &lv_label_class)) {
        const char *t = lv_label_get_text(root);
        lv_obj_get_coords(root, &area);
        if(t != NULL && strstr(t, needle) != NULL && area.y1 >= min_y)
            return root;
    }
    n = lv_obj_get_child_count(root);
    for(i = 0; i < n; i++) {
        lv_obj_t *hit = find_label_contains(lv_obj_get_child(root, i), needle,
                                            min_y);
        if(hit != NULL) return hit;
    }
    return NULL;
}

static lv_obj_t *find_image(lv_obj_t *root)
{
    uint32_t i, n;
    if(lv_obj_check_type(root, &lv_image_class)) return root;
    n = lv_obj_get_child_count(root);
    for(i = 0; i < n; i++) {
        lv_obj_t *hit = find_image(lv_obj_get_child(root, i));
        if(hit != NULL) return hit;
    }
    return NULL;
}

/* True when the image references a decoded buffer with real, varied pixels
 * (a failed or blank decode would be uniform). */
static int image_decoded_ok(lv_obj_t *img)
{
    const lv_image_dsc_t *d;
    const uint8_t *p;
    uint32_t first;
    size_t i, n;

    if(img == NULL) return 0;
    d = (const lv_image_dsc_t *)lv_image_get_src(img);
    if(d == NULL || d->header.magic != LV_IMAGE_HEADER_MAGIC) return 0;
    if(d->header.w <= 0 || d->header.h <= 0 || d->data == NULL) return 0;
    p = d->data;
    n = (size_t)d->header.w * d->header.h;
    first = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
            ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    for(i = 1; i < n && i < 4096; i++) {
        uint32_t c = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) |
                     ((uint32_t)p[i * 4 + 2] << 16) |
                     ((uint32_t)p[i * 4 + 3] << 24);
        if(c != first) return 1;
    }
    return 0;
}

static void snapshot(const char *name)
{
    char path[512];
    FILE *file;
    int x, y;

    settle();
    snprintf(path, sizeof(path), "%s/interaction-%s.ppm", g_outdir, name);
    file = fopen(path, "wb");
    if(file == NULL) {
        printf("FAIL cannot write %s\n", path);
        g_failures++;
        return;
    }
    fprintf(file, "P6\n720 1560\n255\n");
    for(y = 0; y < 1560; y++) {
        const uint32_t *row = framebuffer + (size_t)y * 720;
        for(x = 0; x < 720; x++) {
            uint32_t p = row[x];
            unsigned char rgb[3] = {
                (unsigned char)((p >> 16) & 0xFF),
                (unsigned char)((p >> 8) & 0xFF),
                (unsigned char)(p & 0xFF)
            };
            fwrite(rgb, 1, 3, file);
        }
    }
    fclose(file);
    printf("shot %s\n", path);
}

static void check_app_icon_contract(void)
{
    const lv_image_dsc_t *icons[VQEAF_APP_ICON_COUNT];
    int valid = 1;
    int unique = 1;

    for(int i = 0; i < VQEAF_APP_ICON_COUNT; i++) {
        icons[i] = vqeaf_app_icon(i);
        if(icons[i] == NULL || icons[i]->data == NULL ||
           icons[i]->header.magic != LV_IMAGE_HEADER_MAGIC ||
           icons[i]->header.cf != LV_COLOR_FORMAT_ARGB8888 ||
           icons[i]->header.w != 32 || icons[i]->header.h != 32 ||
           icons[i]->header.stride != 128 ||
           icons[i]->data_size != 32u * 32u * 4u ||
           icons[i]->data[3] != 0 ||
           icons[i]->data[(32 * 16 + 16) * 4 + 3] != 255)
            valid = 0;
    }
    for(int i = 0; i < VQEAF_APP_ICON_COUNT; i++) {
        if(icons[i] == NULL) continue;
        for(int j = i + 1; j < VQEAF_APP_ICON_COUNT; j++) {
            if(icons[j] != NULL &&
               memcmp(icons[i]->data, icons[j]->data,
                      icons[i]->data_size) == 0)
                unique = 0;
        }
    }
    CHECK(valid, "all 29 system icons are valid 32x32 ARGB8888 LVGL images");
    CHECK(unique, "every system icon has original pixel data");
    CHECK(vqeaf_app_icon(-1) == NULL &&
          vqeaf_app_icon(VQEAF_APP_ICON_COUNT) == NULL,
          "out-of-range system icon IDs return NULL");
}

static void check_icon_display_copy(void)
{
    uint8_t raw[64 * 64 * 4];
    uint8_t original[64 * 64 * 4];
    uint8_t display_copy[64 * 64 * 4];
    const size_t center = (32u * 64u + 32u) * 4u;
    const size_t edge = center + 4u;
    int alpha_unchanged = 1;

    memset(raw, 0, sizeof(raw));
    raw[center] = 0;
    raw[center + 1] = 0;
    raw[center + 2] = 255;
    raw[center + 3] = 255;
    raw[edge + 3] = 128;
    raw[(63u * 64u + 63u) * 4u + 3u] = 255;
    memcpy(original, raw, sizeof(raw));
    CHECK(vqeaf_qeapp_icon_display_copy(display_copy, sizeof(display_copy),
                                        raw, sizeof(raw)) == 0,
          "legacy icon display copy succeeds");
    CHECK(memcmp(raw, original, sizeof(raw)) == 0,
          "legacy icon normalization preserves raw package pixels");
    CHECK(display_copy[edge] == 0 && display_copy[edge + 1] == 0 &&
          display_copy[edge + 2] == 255 && display_copy[edge + 3] == 128,
          "partial-alpha black edge inherits opaque colour");
    CHECK(display_copy[2] == 255 && display_copy[3] == 0,
          "transparent pixels inherit colour without gaining alpha");
    for(size_t i = 3; i < sizeof(raw); i += 4)
        if(raw[i] != display_copy[i]) alpha_unchanged = 0;
    CHECK(alpha_unchanged, "legacy icon normalization preserves every alpha");
    CHECK(display_copy[(63u * 64u + 63u) * 4u + 3u] == 255 &&
          display_copy[(63u * 64u + 63u) * 4u + 2u] == 0,
          "opaque black artwork remains opaque black");
    CHECK(vqeaf_qeapp_icon_display_copy(display_copy, sizeof(display_copy) - 1,
                                        raw, sizeof(raw)) == -1 &&
          vqeaf_qeapp_icon_display_copy(NULL, sizeof(display_copy),
                                        raw, sizeof(raw)) == -1,
          "icon display copy rejects invalid buffers");
}

static uint32_t framebuffer_rgb(int x, int y)
{
    return framebuffer[(size_t)y * 720u + (unsigned)x] & 0x00FFFFFFu;
}

static void check_icon_clipping(void)
{
    static uint8_t pixels[32 * 32 * 4];
    static const lv_image_dsc_t opaque_icon = {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_ARGB8888,
            .w = 32, .h = 32, .stride = 128,
        },
        .data = pixels,
        .data_size = sizeof(pixels),
    };
    lv_obj_t *backdrop;
    lv_obj_t *clip;
    lv_obj_t *image;

    memset(pixels, 0xFF, sizeof(pixels));
    backdrop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(backdrop);
    lv_obj_set_size(backdrop, 720, 1560);
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0xFF00FF), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    clip = vqeaf_icon_img(backdrop, &opaque_icon, 104);
    lv_obj_set_pos(clip, 100, 120);
    settle();
    CHECK(clip != NULL && lv_obj_get_child_count(clip) == 1 &&
          lv_obj_get_style_radius(clip, 0) == 10 &&
          lv_obj_get_style_clip_corner(clip, 0),
          "bitmap icon has a real 10 px clipping container");
    image = clip != NULL ? lv_obj_get_child(clip, 0) : NULL;
    CHECK(image != NULL && lv_obj_get_style_radius(image, 0) == 0,
          "rounded clipping belongs to the parent, not the scaled image");
    CHECK(image != NULL && lv_obj_get_x(image) == 0 && lv_obj_get_y(image) == 0 &&
          lv_obj_get_width(image) == lv_obj_get_width(clip) &&
          lv_obj_get_height(image) == lv_obj_get_height(clip),
          "bitmap artwork is centered inside its clipping plate");
    CHECK(framebuffer_rgb(100, 120) == 0x00FF00FF &&
          framebuffer_rgb(203, 223) == 0x00FF00FF,
          "opaque bitmap corners are clipped away");
    CHECK(framebuffer_rgb(104, 124) == 0x00FFFFFF &&
          framebuffer_rgb(152, 124) == 0x00FFFFFF &&
          framebuffer_rgb(152, 172) == 0x00FFFFFF,
          "rounded icon interior remains visible");
    snapshot("icon-clip-probe");
    lv_obj_del(backdrop);
    settle();
}

static void check_advisory_service(void)
{
    size_t matches[VQEAF_ADVISORY_SEARCH_MAX];
    size_t count = vqeaf_advisory_count();
    int valid = count >= 8;
    int sequential = 1;
    char too_long[97];
    const char *too_many_tokens = "one two three four five";

    for(size_t i = 0; i < count; i++) {
        const vqeaf_advisory_t *record = vqeaf_advisory_at(i);
        if(record == NULL || record->id == NULL || record->title == NULL ||
           record->product == NULL || record->published == NULL ||
           record->severity == NULL || record->source == NULL ||
           record->summary == NULL || record->remediation == NULL ||
           record->id[0] == '\0' || record->title[0] == '\0' ||
           record->product[0] == '\0' || record->published[0] == '\0' ||
           record->summary[0] == '\0' || record->remediation[0] == '\0' ||
           strncmp(record->id, "CVE-", 4) != 0)
            valid = 0;
    }
    CHECK(valid, "offline advisory catalog has complete public CVE metadata");
    CHECK(vqeaf_advisory_at(count) == NULL,
          "out-of-range advisory IDs return NULL");
    CHECK(vqeaf_advisory_catalog_revision()[0] != '\0',
          "offline catalog publishes its revision");

    CHECK(vqeaf_advisory_search("", matches, VQEAF_ADVISORY_SEARCH_MAX) ==
          (int)count, "empty query returns the whole catalog");
    for(size_t i = 0; i < count; i++)
        if(matches[i] != i) sequential = 0;
    CHECK(sequential, "empty-query results preserve catalog order");

    CHECK(vqeaf_advisory_search("EtErNaLbLuE", matches,
                                VQEAF_ADVISORY_SEARCH_MAX) == 1 &&
          strcmp(vqeaf_advisory_at(matches[0])->id, "CVE-2017-0144") == 0,
          "advisory search is case-insensitive");
    CHECK(vqeaf_advisory_search("windows smb", matches,
                                VQEAF_ADVISORY_SEARCH_MAX) == 1 &&
          strcmp(vqeaf_advisory_at(matches[0])->id, "CVE-2017-0144") == 0,
          "all query tokens must match one record");
    CHECK(vqeaf_advisory_search("no-such-advisory", matches,
                                VQEAF_ADVISORY_SEARCH_MAX) == 0,
          "unknown advisory query returns no fabricated results");
    CHECK(vqeaf_advisory_search("", matches, 1) == 1 && matches[0] == 0,
          "search honours the caller's result bound");
    CHECK(vqeaf_advisory_search("", NULL, 1) == -1 &&
          vqeaf_advisory_search("", matches, 0) == -1 &&
          vqeaf_advisory_search(NULL, matches, 1) == -1,
          "invalid advisory search arguments are rejected");
    memset(too_long, 'x', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    CHECK(vqeaf_advisory_search(too_long, matches, 1) == -1,
          "overlong advisory queries are rejected");
    CHECK(vqeaf_advisory_search(too_many_tokens, matches, 1) == -1,
          "advisory queries have a bounded token count");
}

static uint8_t setup_settings_blob[VQEAF_SETTINGS_BLOB_SIZE];
static int setup_settings_save_fail;

static int setup_settings_save(void *ctx, const uint8_t *blob, uint32_t len)
{
    (void)ctx;
    if(setup_settings_save_fail) return -1;
    if(len > sizeof(setup_settings_blob)) return -1;
    memcpy(setup_settings_blob, blob, len);
    return 0;
}

static int setup_settings_load(void *ctx, uint8_t *blob, uint32_t cap,
                               uint32_t *len)
{
    (void)ctx;
    (void)blob;
    (void)cap;
    if(len != NULL) *len = 0;
    return -1;
}

int main(int argc, char **argv)
{
    lv_obj_t *label;
    int x, y;

    check_app_icon_contract();
    check_icon_display_copy();
    check_advisory_service();
    g_outdir = argc > 1 ? argv[1] : "build/screens/phone-ui";
    framebuffer = calloc(720u * 1560u, sizeof(uint32_t));
    if(framebuffer == NULL) return 2;

    lv_init();
    lv_tick_set_cb(tick_cb);
    display = lv_display_create(720, 1560);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, framebuffer, NULL,
                           720u * 1560u * sizeof(uint32_t),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_set_default(display);
    pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, indev_read_cb);
    lv_indev_set_long_press_time(pointer, 900);

    vqeaf_ui_create(display);
    vqeaf_ui_set_sync_list(host_sync_list);
    vqeaf_ui_set_sync_read(host_sync_read);
    step(100);

    CHECK(vqeaf_ui_page() == VQEAF_PAGE_SETUP && vqeaf_setup_required(),
          "first boot opens setup instead of lock");
    CHECK(find_label(lv_screen_active(), "Xin chào!", 0) != NULL,
          "setup welcome renders on initial boot");
    CHECK(!vqeaf_ui_dock_visible() && !vqeaf_ui_gesture_pill_visible() &&
          !vqeaf_tools_taskbar_visible(VQEAF_PAGE_SETUP),
          "setup hides dock, gesture pill and tools taskbar");
    vqeaf_ui_set_virtual_keys(1);
    CHECK(!vqeaf_ui_gesture_pill_visible(),
          "setup does not reveal a gesture pill with virtual keys enabled");
    vqeaf_ui_set_virtual_keys(0);
    snapshot("setup-welcome");
    swipe(360, 1545, 360, 1300, 12);
    swipe(710, 700, 570, 700, 12);
    swipe(360, 8, 360, 200, 12);
    settle();
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_SETUP &&
          !vqeaf_ui_control_visible(),
          "shell gestures cannot bypass incomplete setup");
    label = find_label(lv_screen_active(), "Gọi khẩn cấp", 0);
    CHECK(label != NULL && label_self_target(label, &x, &y) == 0,
          "welcome offers emergency call");
    if(label != NULL && label_self_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label_contains(lv_layer_top(), "Chưa có dịch vụ thoại", 0) != NULL,
          "emergency call reports the missing modem service honestly");
    tap_text(lv_layer_top(), "OK");
    CHECK(tap_text(lv_screen_active(), "Bắt đầu") == 0,
          "welcome start opens security");
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_SETUP &&
          find_label(lv_screen_active(), "Bảo mật", 0) != NULL,
          "setup start advances to security");
    snapshot("setup-security");
    CHECK(tap_text(lv_screen_active(), VQEAF_SYM_ARROW_BACK) == 0,
          "setup security back returns to welcome");
    CHECK(find_label(lv_screen_active(), "Xin chào!", 0) != NULL,
          "setup back restores the welcome step");
    CHECK(vqeaf_setup_complete() == 0,
          "legacy shell tests can complete setup through the real marker");
    vqeaf_ui_show(VQEAF_PAGE_LOCK);
    settle();

    lv_obj_t *icon_probe = vqeaf_icon_img(lv_screen_active(),
        vqeaf_app_icon(VQEAF_APP_ICON_VQEAF), 104);
    CHECK(icon_probe != NULL &&
          lv_obj_get_style_radius(icon_probe, LV_PART_MAIN) == 10,
          "bitmap app icons use 10 px rounded corners");
    if(icon_probe != NULL) lv_obj_del(icon_probe);
    check_icon_clipping();

    /* 1. Long-press a home-grid app: info sheet, not navigation. */
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    settle();
    snapshot("home-icon-corners");
    label = find_label(lv_screen_active(), "Điện thoại", 0);
    CHECK(label != NULL, "home grid has Điện thoại");
    if(label_target(label, &x, &y) == 0) long_press(x, y);
    CHECK(modal_up(), "long-press opens info sheet");
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME, "long-press does not navigate");
    CHECK(find_label(lv_layer_top(), "Ứng dụng hệ thống", 0) != NULL,
          "sheet says system app");
    CHECK(find_label(lv_layer_top(), "Điện thoại", 0) != NULL,
          "sheet names the app");
    snapshot("info-app");

    /* 2. Đóng closes the sheet. */
    label = find_label(lv_layer_top(), "Đóng", 0);
    CHECK(label != NULL, "sheet has Đóng");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    CHECK(!modal_up(), "Đóng closes the sheet");

    /* 3. Tap opens the page (Tệp lives on drawer page 2). */
    vqeaf_pages_apps_page_set(1);
    vqeaf_ui_show(VQEAF_PAGE_APPS);
    settle();
    label = find_label(lv_screen_active(), "Tệp", 0);
    CHECK(label != NULL, "drawer has Tệp");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_FILES, "tap opens Tệp -> FILES");

    /* 4. Long-press Tệp, then Mở on the sheet opens the same page. */
    vqeaf_pages_apps_page_set(1);
    vqeaf_ui_show(VQEAF_PAGE_APPS);
    settle();
    label = find_label(lv_screen_active(), "Tệp", 0);
    if(label_target(label, &x, &y) == 0) long_press(x, y);
    CHECK(modal_up(), "long-press Tệp opens sheet");
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_APPS, "long-press Tệp does not open it");
    label = find_label(lv_layer_top(), "Mở", 0);
    CHECK(label != NULL, "sheet has Mở for a page app");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_FILES, "Mở opens FILES");
    CHECK(!modal_up(), "Mở closes the sheet");

    /* 5. Long-press an installed QEAPP: its own info kind. */
    vqeaf_ui_show(VQEAF_PAGE_APPS);
    settle();
    if(vqeaf_qeapp_count() > 0) {
        const vqeaf_qeapp_t *app = vqeaf_qeapp_at(0);
        label = find_label(lv_screen_active(), app->name, 0);
        CHECK(label != NULL, "drawer lists the installed QEAPP");
        /* The installed section sits below the paged app grid; scroll it into
         * view before pressing, like a thumb would. */
        if(label != NULL) {
            lv_obj_scroll_to_view_recursive(label, LV_ANIM_OFF);
            settle();
        }
        if(label_target(label, &x, &y) == 0) long_press(x, y);
        CHECK(modal_up(), "long-press QEAPP opens sheet");
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_APPS,
              "long-press QEAPP does not open it");
        CHECK(find_label(lv_layer_top(), "QEAPP đã cài", 0) != NULL,
              "sheet says QEAPP");
        CHECK(find_label(lv_layer_top(), "Thông tin", 0) != NULL,
              "QEAPP sheet offers Thông tin, not Mở");
        snapshot("info-qeapp");
        label = find_label(lv_layer_top(), "Đóng", 0);
        if(label_target(label, &x, &y) == 0) tap(x, y);
        CHECK(!modal_up(), "Đóng closes the QEAPP sheet");
    }

    /* 6. Home keeps the dock in both themes. Long-press a dock app: the sheet
     *    offers Di chuyển because a real drop target is visible. */
    vqeaf_theme_set_fluent(1);
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    settle();
    label = find_label(lv_screen_active(), "Trình duyệt", 1400);
    CHECK(label != NULL, "nav dock has Trình duyệt");
    if(label_target(label, &x, &y) == 0) long_press(x, y);
    CHECK(modal_up(), "long-press dock opens sheet");
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME, "long-press dock does not navigate");
    CHECK(find_label(lv_layer_top(), "Di chuyển", 0) != NULL,
          "docked app offers Di chuyển");
    snapshot("info-dock");

    /* 7. Di chuyển arms move mode; the next dock tap swaps the slot. */
    label = find_label(lv_layer_top(), "Di chuyển", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    CHECK(!modal_up(), "Di chuyển closes the sheet");
    snapshot("move-armed");
    tap(303, 1447);   /* nav dock slot 1 centre */
    CHECK(vqeaf_dock_slot(1) == 7, "move drops Trình duyệt into slot 1");
    CHECK(vqeaf_dock_slot(0) == 8, "old occupant swaps to slot 0");
    snapshot("move-done");

    /* 8. A non-docked app offers Thêm vào dock; the dock lives on Home only,
     *    so arming returns to the launcher, then a slot tap adds it and a
     *    plain tap on the new dock icon opens its page. */
    vqeaf_pages_apps_page_set(2);   /* Cài đặt is on the third drawer page */
    vqeaf_ui_show(VQEAF_PAGE_APPS);
    settle();
    CHECK(!vqeaf_ui_dock_visible(), "app drawer hides the dock (Home only)");
    CHECK(vqeaf_ui_gesture_pill_visible(), "app drawer keeps the gesture pill");
    label = find_label(lv_screen_active(), "Cài đặt", 0);
    CHECK(label != NULL, "drawer has Cài đặt");
    if(label_target(label, &x, &y) == 0) long_press(x, y);
    CHECK(find_label(lv_layer_top(), "Thêm vào dock", 0) != NULL,
          "non-docked app offers Thêm vào dock");
    label = find_label(lv_layer_top(), "Thêm vào dock", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME,
          "Thêm vào dock returns to Home where the dock lives");
    CHECK(vqeaf_ui_dock_visible(), "dock is visible on Home for the drop");
    tap(101, 1447);
    settle();    /* the drop runs on the release, after the tap's own frames */
    CHECK(vqeaf_dock_slot(0) == 22, "Thêm vào dock puts Cài đặt in slot 0");
    tap(101, 1447);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_SETTINGS,
          "tap on new dock icon opens SETTINGS");

    vqeaf_theme_set_fluent(0);
    vqeaf_dock_drop(7, 0);
    vqeaf_dock_drop(8, 1);
    vqeaf_dock_drop(9, 2);
    vqeaf_dock_drop(10, 3);

    /* Hold-drag is a real pointer gesture, not tap-to-place mode. */
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    step(50);
    settle();
    label = find_label(lv_screen_active(), "Trình duyệt", 1400);
    lv_obj_t *second_label = find_label(lv_screen_active(), "Máy ảnh", 1400);
    CHECK(label != NULL && second_label != NULL,
          "dock exposes two drag sources");
    if(label != NULL && second_label != NULL &&
       label_target(label, &x, &y) == 0) {
        int tx, ty;
        if(label_target(second_label, &tx, &ty) == 0) {
            int first = vqeaf_dock_slot(0);
            int second = vqeaf_dock_slot(1);
            hold_drag_to(x, y, tx, ty, 12);
            CHECK(vqeaf_drag_active(), "dock hold creates an active drag");
            snapshot("dock-drag");
            release_hold();
            step(150);
            settle();
            CHECK(vqeaf_dock_slot(0) == second && vqeaf_dock_slot(1) == first,
                  "dock hold-drag swaps the two slots");
            CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME && !modal_up() &&
                  !vqeaf_drag_active(),
                  "dock drag neither launches an app nor opens information");
        }
    }

    label = find_label(lv_screen_active(), "Trình duyệt", 1400);
    if(label != NULL && label_target(label, &x, &y) == 0) {
        int first = vqeaf_dock_slot(0);
        int second = vqeaf_dock_slot(1);
        hold_drag_to(x, y, 360, 900, 14);
        CHECK(vqeaf_drag_active(), "dock drag can leave every valid target");
        snapshot("dock-drag-cancel");
        release_hold();
        step(150);
        settle();
        CHECK(vqeaf_dock_slot(0) == first && vqeaf_dock_slot(1) == second,
              "dropping outside the dock cancels without reordering");
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME && !modal_up() &&
              !vqeaf_drag_active(),
              "cancelled dock drag does not navigate or leak a ghost");
    }

    label = find_label(lv_screen_active(), "Trình duyệt", 1400);
    if(label != NULL && label_target(label, &x, &y) == 0) {
        int first = vqeaf_dock_slot(0);
        int second = vqeaf_dock_slot(1);
        lv_obj_t *ghost;
        hold_drag_to(x, y, x + 80, y, 8);
        ghost = find_drag_ghost();
        CHECK(ghost != NULL, "dock drag creates a top-layer ghost");
        if(ghost != NULL) {
            lv_obj_delete(ghost);
            g_point.x += 24;
            step(20);
            CHECK(!vqeaf_drag_active(),
                  "an externally deleted ghost cancels the drag safely");
        }
        release_hold();
        step(150);
        settle();
        CHECK(vqeaf_dock_slot(0) == first && vqeaf_dock_slot(1) == second &&
              !modal_up() && !vqeaf_drag_active(),
              "deleted ghost cancellation restores the dock and press state");
    }
    vqeaf_dock_move(0, 1);
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    step(50);
    settle();

    label = find_label(lv_screen_active(), "Điện thoại", 0);
    second_label = find_label(lv_screen_active(), "Tin nhắn", 0);
    CHECK(label != NULL && second_label != NULL,
          "Home exposes two reorder targets");
    if(label != NULL && second_label != NULL &&
       label_target(label, &x, &y) == 0) {
        int tx, ty;
        if(label_target(second_label, &tx, &ty) == 0) {
            int first = vqeaf_pages_home_order_at(0);
            int second = vqeaf_pages_home_order_at(1);
            hold_drag_to(x, y, tx, ty, 12);
            CHECK(vqeaf_drag_active(), "Home hold creates an active drag");
            snapshot("home-reorder");
            release_hold();
            step(150);
            settle();
            CHECK(vqeaf_pages_home_order_at(0) == second &&
                  vqeaf_pages_home_order_at(1) == first,
                  "Home hold-drag moves the icon within the visible page");
            CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME && !modal_up(),
                  "Home reorder does not launch an app or open information");
        }
    }
    vqeaf_pages_home_move(0, 1);
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    settle();

    /* 9. A tap on a page-less app stays put. */
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    settle();
    label = find_label(lv_screen_active(), "Điện thoại", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME,
          "tap on page-less app does not navigate");
    CHECK(find_label(lv_layer_top(),
                     "Điện thoại chưa có trang nội dung trên ảnh này.", 0) != NULL,
          "page-less app explains that no content backend exists");
    snapshot("app-unavailable");
    label = find_label(lv_layer_top(), "OK", 0);
    CHECK(label != NULL, "unavailable feedback offers dismissal");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    CHECK(find_label(lv_layer_top(),
                     "Điện thoại chưa có trang nội dung trên ảnh này.", 0) == NULL,
          "unavailable feedback can be dismissed");

    /* 9b. Stock Home is a regular content page with the floating dock and
     *     gesture pill. Other regular pages keep the gesture pill; native apps
     *     and Lock hide the dock as before. */
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    settle();
    CHECK(vqeaf_ui_dock_visible(), "stock Home shows the dock");
    CHECK(!vqeaf_tools_taskbar_visible(VQEAF_PAGE_HOME),
          "stock Home hides the tools taskbar");
    CHECK(vqeaf_ui_gesture_pill_visible(), "Home shows the gesture pill");
    CHECK(vqeaf_dock_slot(0) == 7 && vqeaf_dock_slot(1) == 8 &&
          vqeaf_dock_slot(2) == 9 && vqeaf_dock_slot(3) == 10,
          "stock dock defaults to Browser, Camera, Gallery and Music");
    CHECK(find_label(lv_screen_active(), "Trình duyệt", 1400) != NULL &&
          find_label(lv_screen_active(), "Máy ảnh", 1400) != NULL &&
          find_label(lv_screen_active(), "Thư viện", 1400) != NULL &&
          find_label(lv_screen_active(), "Nhạc", 1400) != NULL,
          "dock renders its four real destinations");
    snapshot("chrome-home");
    vqeaf_ui_show(VQEAF_PAGE_SETTINGS);
    settle();
    CHECK(!vqeaf_ui_dock_visible(), "Settings hides the dock");
    CHECK(vqeaf_ui_gesture_pill_visible(), "Settings keeps the gesture pill");
    snapshot("chrome-settings");

    /* 9c. Settings routes through the public QEAPP API. On the host, package
     *     mutation is explicitly unsupported and leaves the seeded registry
     *     unchanged; the session-only card is a labelled render fixture. */
    {
        vqeaf_qeapp_package_info_t package_info;
        vqeaf_qeapp_package_result_t package_result;
        uint8_t package_hash[VQEAF_QEAPP_PACKAGE_SHA256_SIZE] = { 0 };
        size_t before = vqeaf_qeapp_package_count();

        CHECK(vqeaf_qeapp_package_api_version() == VQEAF_QEAPP_PACKAGE_API_VERSION,
              "Settings uses QEAPP package API v1");
        CHECK(vqeaf_qeapp_package_preview("demo.qeapp", &package_info,
                                          &package_result) ==
              VQEAF_QEAPP_PACKAGE_UNSUPPORTED,
              "host package preview is unsupported");
        CHECK(package_result.struct_size == sizeof(package_result) &&
              package_result.api_version == VQEAF_QEAPP_PACKAGE_API_VERSION &&
              package_result.persisted == -1 && package_result.updated == -1,
              "unsupported preview initializes result metadata");
        CHECK(vqeaf_qeapp_package_install("demo.qeapp", &package_result) ==
              VQEAF_QEAPP_PACKAGE_UNSUPPORTED,
              "host package install is unsupported");
        CHECK(package_result.struct_size == sizeof(package_result) &&
              package_result.api_version == VQEAF_QEAPP_PACKAGE_API_VERSION &&
              package_result.persisted == -1 && package_result.updated == -1,
              "unsupported install initializes result metadata");
        CHECK(vqeaf_qeapp_package_install_verified("demo.qeapp", package_hash,
                                                   &package_result) ==
              VQEAF_QEAPP_PACKAGE_UNSUPPORTED,
              "host verified install is unsupported");
        CHECK(vqeaf_qeapp_package_uninstall("org.vqeaf.hello", &package_result) ==
              VQEAF_QEAPP_PACKAGE_UNSUPPORTED,
              "host package uninstall is unsupported");
        CHECK(package_result.struct_size == sizeof(package_result) &&
              package_result.api_version == VQEAF_QEAPP_PACKAGE_API_VERSION &&
              package_result.persisted == -1 && package_result.updated == -1,
              "unsupported uninstall initializes result metadata");
        CHECK(vqeaf_qeapp_package_count() == before,
              "unsupported host mutations change no registry state");

        label = find_label(lv_screen_active(), "Cài đặt .qeapp", 0);
        CHECK(label != NULL, "Settings has a .qeapp installer row");
        if(label_target(label, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_INSTALLER,
              "Settings installer row opens the installer");
        CHECK(find_label_contains(lv_screen_active(), "API QEAPP v1", 0) != NULL,
              "installer shows the public API badge");
        snapshot("installer-api");

        label = find_label(lv_screen_active(), "Cài", 0);
        if(label_target(label, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(modal_up(), "package tap opens the result modal on host");
        CHECK(find_label_contains(lv_layer_top(),
                                  "Host chỉ xem trước giao diện", 0) != NULL,
              "host preview explains the missing package backend");
        snapshot("install-host-unsupported");
        label = find_label(lv_layer_top(), "ĐÓNG", 0);
        if(label_target(label, &x, &y) == 0) tap(x, y);
        step(100);

        vqeaf_install_render(4);
        settle();
        CHECK(find_label(lv_layer_top(), "Đã cài trong phiên hiện tại.", 0) != NULL,
              "session-only install has a distinct truthful result");
        CHECK(find_label_contains(lv_layer_top(), "Chưa lưu vào kho p77", 0) != NULL,
              "session-only result explains the reboot boundary");
        snapshot("install-session-only");
        vqeaf_install_render_close();
        step(100);
    }

    vqeaf_ui_show(VQEAF_PAGE_KEYBOARD);
    settle();
    CHECK(!vqeaf_ui_dock_visible(), "a Settings detail page hides the dock");
    CHECK(vqeaf_ui_gesture_pill_visible(), "detail page keeps the gesture pill");
    vqeaf_ui_show(VQEAF_PAGE_FILES);
    settle();
    CHECK(!vqeaf_ui_dock_visible(), "a running app page hides the dock");
    CHECK(vqeaf_ui_gesture_pill_visible(), "app page keeps the gesture pill");
    vqeaf_ui_show(VQEAF_PAGE_QEAPP_NATIVE);
    settle();
    CHECK(!vqeaf_ui_dock_visible(), "a native .qeapp hides the dock");
    CHECK(vqeaf_ui_gesture_pill_visible(),
          "a native .qeapp keeps the gesture pill for home");
    snapshot("chrome-native");
    vqeaf_ui_show(VQEAF_PAGE_LOCK);
    settle();
    CHECK(!vqeaf_ui_dock_visible(), "lock screen hides the dock");
    CHECK(!vqeaf_ui_gesture_pill_visible(), "lock screen hides the gesture pill");
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    settle();
    CHECK(vqeaf_ui_dock_visible() && vqeaf_ui_gesture_pill_visible(),
          "returning Home restores the dock and gesture pill");

    /* 10. Files: open subfolders and preview/inspect multimedia. */
    vqeaf_ui_show(VQEAF_PAGE_FILES);
    settle();
    label = find_label(lv_screen_active(), "media", 0);
    CHECK(label != NULL, "files root lists media");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label(lv_screen_active(), "logo.png", 0) != NULL,
          "tap opens /media subfolder");
    CHECK(find_label(lv_screen_active(), "..", 0) != NULL,
          "/media lists a parent entry");

    /* PNG: real decode + preview. */
    label = find_label(lv_screen_active(), "logo.png", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(modal_up(), "PNG opens the viewer");
    CHECK(image_decoded_ok(find_image(lv_layer_top())),
          "PNG decodes to varied pixels");
    CHECK(find_label_contains(lv_layer_top(), "PNG", 0) != NULL,
          "PNG summary shown");
    snapshot("view-png");
    label = find_label(lv_layer_top(), "Đóng", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    CHECK(!modal_up(), "PNG viewer closes");

    /* JPEG: real decode + preview. */
    label = find_label(lv_screen_active(), "photo.jpg", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(image_decoded_ok(find_image(lv_layer_top())),
          "JPEG decodes to varied pixels");
    snapshot("view-jpg");
    label = find_label(lv_layer_top(), "Đóng", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);

    /* BMP: real decode + preview. */
    label = find_label(lv_screen_active(), "pattern.bmp", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(image_decoded_ok(find_image(lv_layer_top())),
          "BMP decodes to varied pixels");
    snapshot("view-bmp");
    label = find_label(lv_layer_top(), "Đóng", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);

    /* MP3: parsed ID3 metadata, honest no-playback note, no image. */
    label = find_label(lv_screen_active(), "song.mp3", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(modal_up(), "MP3 opens the viewer");
    CHECK(find_image(lv_layer_top()) == NULL, "MP3 viewer shows no image");
    CHECK(find_label_contains(lv_layer_top(), "Test Title", 0) != NULL,
          "MP3 ID3 title parsed");
    CHECK(find_label_contains(lv_layer_top(), "codec phát chưa có", 0) != NULL,
          "MP3 reports no playback codec honestly");
    snapshot("view-mp3");
    label = find_label(lv_layer_top(), "Đóng", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);

    /* Deeper subfolder: /media -> /media/sub -> open a text file. */
    label = find_label(lv_screen_active(), "sub", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label(lv_screen_active(), "readme.txt", 0) != NULL,
          "tap opens /media/sub subfolder");
    label = find_label(lv_screen_active(), "readme.txt", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label_contains(lv_layer_top(), "Xin chao", 0) != NULL,
          "subfolder text file content shown");
    label = find_label(lv_layer_top(), "Đóng", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);

    /* ".." walks back up to /media. */
    label = find_label(lv_screen_active(), "..", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label(lv_screen_active(), "logo.png", 0) != NULL,
          ".. returns to /media");

    /* 11. QEAFRESCUE tab: subfolders on the rescue volume. */
    label = find_label(lv_screen_active(), "QEAFRESCUE", 0);
    CHECK(label != NULL, "files page has QEAFRESCUE pill");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label(lv_screen_active(), "QEAFRESCUE /", 0) != NULL,
          "rescue tab shows root path");
    CHECK(find_label(lv_screen_active(), "Apps", 0) != NULL,
          "rescue root lists Apps");
    CHECK(find_label(lv_screen_active(), "35 B", 0) != NULL,
          "whole byte size has no spurious fraction digit");
    CHECK(find_label(lv_screen_active(), "3 B", 0) != NULL,
          "small byte size renders exactly");
    snapshot("rescue-root");

    label = find_label(lv_screen_active(), "Apps", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label(lv_screen_active(), "QEAFRESCUE /Apps", 0) != NULL,
          "tap opens /Apps on rescue tab");
    CHECK(find_label(lv_screen_active(), "README.TXT", 0) != NULL,
          "/Apps lists README.TXT");
    CHECK(find_label(lv_screen_active(), "..", 0) != NULL,
          "/Apps lists a parent entry");

    label = find_label(lv_screen_active(), "README.TXT", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label_contains(lv_layer_top(), "Thu muc Apps", 0) != NULL,
          "rescue text file content shown");
    snapshot("rescue-text");
    label = find_label(lv_layer_top(), "Đóng", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();

    /* Deeper: /Apps -> /Apps/Tools. */
    label = find_label(lv_screen_active(), "Tools", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label(lv_screen_active(), "QEAFRESCUE /Apps/Tools", 0) != NULL,
          "tap opens /Apps/Tools");
    label = find_label(lv_screen_active(), "TOOL.TXT", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label_contains(lv_layer_top(), "Cong cu mau", 0) != NULL,
          "nested rescue text shown");
    label = find_label(lv_layer_top(), "Đóng", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();

    /* ".." twice walks back to the rescue root. */
    label = find_label(lv_screen_active(), "..", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label(lv_screen_active(), "QEAFRESCUE /Apps", 0) != NULL,
          ".. returns to /Apps");
    label = find_label(lv_screen_active(), "..", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label(lv_screen_active(), "QEAFRESCUE /", 0) != NULL,
          ".. returns to rescue root");
    snapshot("rescue-back");

    /* Switching back to the system tab resets the path. */
    label = find_label(lv_screen_active(), "Hệ thống", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label(lv_screen_active(), "media", 0) != NULL,
          "system tab lists media again");

    /* 12. "Khởi động lại" follows a desktop-style sequence: a visible
     * restarting screen, ordered platform stages (log flush -> sync) and
     * exactly one machine-reset call. No reboot happens on this host. */
    reset_shutdown_hooks();
    vqeaf_ui_power_menu_show();
    settle();
    label = find_label(lv_layer_top(), "Khởi động lại", 0);
    CHECK(label != NULL, "power menu offers Khởi động lại");
    CHECK(find_label(lv_layer_top(), "Ngủ", 0) != NULL,
          "duplicate Reboot row replaced by Sleep");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(vqeaf_shutdown_active(), "tap starts the restart sequence");
    CHECK(strcmp(vqeaf_shutdown_title(), "Đang khởi động lại") == 0,
          "restart title is shown");
    CHECK(find_label(lv_layer_top(), "Đang khởi động lại", 0) != NULL,
          "restarting overlay is visible");
    CHECK(find_label(lv_layer_top(),
                     "Vui lòng đợi, không ngắt nguồn thiết bị.", 0) != NULL,
          "overlay tells the user to wait");
    CHECK(stage_seen("request"), "platform receives request stage");
    snapshot("restart");

    step(800);
    settle();
    CHECK(stage_seen("flush-log"), "boot log is flushed before reset");
    CHECK(find_label(lv_layer_top(), "Đang lưu nhật ký hệ thống...", 0) != NULL,
          "flush stage is visible");

    step(600);
    settle();
    CHECK(g_final_count == 1 &&
          g_final_action == VQEAF_SHUTDOWN_RESTART,
          "machine reset hook called exactly once for restart");
    CHECK(stage_seen("sync"), "filesystem sync precedes machine reset");
    CHECK(find_label(lv_layer_top(),
                     "Đang chờ phần cứng khởi động lại...", 0) != NULL,
          "waiting-for-reset stage is shown");
    vqeaf_shutdown_request(VQEAF_SHUTDOWN_RESTART);
    CHECK(g_final_count == 1, "a second request cannot issue a second reset");
    vqeaf_shutdown_cancel();
    CHECK(!vqeaf_shutdown_active(), "cancel returns to the shell");
    CHECK(find_label(lv_layer_top(), "Đang khởi động lại", 0) == NULL,
          "restarting overlay is removed");

    /* 13. A kernel that refuses the reset must show the real failure. */
    reset_shutdown_hooks();
    g_final_result = -1;
    vqeaf_ui_power_menu_show();
    settle();
    label = find_label(lv_layer_top(), "Tắt nguồn", 0);
    CHECK(label != NULL, "power menu offers Tắt nguồn");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(strcmp(vqeaf_shutdown_title(), "Đang tắt nguồn") == 0,
          "power-off has its own title");
    step(1400);
    settle();
    CHECK(g_final_count == 1 &&
          g_final_action == VQEAF_SHUTDOWN_POWER_OFF,
          "power-off calls the machine-reset hook once");
    CHECK(find_label(lv_layer_top(), "Không thể tắt nguồn", 0) != NULL,
          "refused reset is reported truthfully");
    CHECK(find_label(lv_layer_top(), "Quay lại", 0) != NULL,
          "refusal screen offers a way back");
    snapshot("poweroff-refused");
    label = find_label(lv_layer_top(), "Quay lại", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(!vqeaf_shutdown_active(), "back button leaves the refusal screen");
    g_final_result = 0;

    /* 14. Stock Home is a two-page, four-column launcher built from the 30
     *     real registry entries. The system widget shows live uptime/RAM/storage,
     *     never fabricated screen-time data. A long drag rebuilds once, stays on
     *     the selected page after release, and clamps instead of wrapping. */
    CHECK(vqeaf_pages_home_page_count() == 2,
          "Home has two pages for 30 real apps");
    vqeaf_pages_home_page_set(-9);
    CHECK(vqeaf_pages_home_page() == 0, "Home page clamps at the first page");
    vqeaf_pages_home_page_set(99);
    CHECK(vqeaf_pages_home_page() == 1, "Home page clamps at the last page");
    vqeaf_pages_home_page_set(0);
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    settle();
    CHECK(find_label(lv_screen_active(), "Điện thoại", 0) != NULL &&
          find_label(lv_screen_active(), "Ghi chú", 0) != NULL &&
          find_label(lv_screen_active(), "Thời tiết", 0) == NULL,
          "Home page one contains the first 16 real apps");
    CHECK(find_label(lv_screen_active(), "Tiện ích hệ thống", 0) != NULL &&
          find_label(lv_screen_active(), "Thời gian chạy", 0) != NULL &&
          find_label(lv_screen_active(), "RAM đã dùng", 0) != NULL &&
          find_label(lv_screen_active(), "Lưu trữ", 0) != NULL,
          "Home widget renders real uptime, RAM and storage roles");
    CHECK(find_label(lv_screen_active(), "Tìm ứng dụng, cài đặt…", 0) != NULL,
          "Home renders the launcher search pill");
    CHECK(find_label_contains(lv_screen_active(), "Thời gian sử dụng", 0) == NULL,
          "Home does not fabricate screen-time statistics");
    {
        const char *first_row[] = { "Điện thoại", "Tin nhắn", "Danh bạ",
                                    "Trình duyệt" };
        lv_obj_t *labels[4];
        lv_area_t areas[4];
        int four_columns = 1;
        for(int i = 0; i < 4; i++) {
            labels[i] = find_label(lv_screen_active(), first_row[i], 0);
            if(labels[i] == NULL) {
                four_columns = 0;
                continue;
            }
            lv_obj_get_coords(labels[i], &areas[i]);
        }
        for(int i = 1; i < 4; i++) {
            if(labels[i] == NULL || labels[i - 1] == NULL) continue;
            if(areas[i].x1 <= areas[i - 1].x2 ||
               LV_ABS(areas[i].y1 - areas[i - 1].y1) > 24)
                four_columns = 0;
        }
        CHECK(four_columns, "portrait Home first row has four distinct columns");
    }
    {
        lv_obj_t *label = find_label(lv_screen_active(), "Điện thoại", 0);
        lv_obj_t *item = label != NULL ? lv_obj_get_parent(label) : NULL;
        lv_obj_t *plate = item != NULL ? lv_obj_get_child(item, 0) : NULL;
        lv_obj_t *image = plate != NULL && lv_obj_get_child_count(plate) == 1 ?
                          lv_obj_get_child(plate, 0) : NULL;
        lv_area_t item_area, plate_area, image_area;
        int centered = item != NULL && plate != NULL;
        if(centered) {
            lv_obj_get_coords(item, &item_area);
            lv_obj_get_coords(plate, &plate_area);
            centered = LV_ABS((item_area.x1 + item_area.x2) -
                              (plate_area.x1 + plate_area.x2)) <= 1;
        }
        CHECK(centered, "Home app icon plate is centered in its launcher cell");
        if(image != NULL && lv_obj_get_class(image) == &lv_image_class) {
            lv_obj_get_coords(plate, &plate_area);
            lv_obj_get_coords(image, &image_area);
            CHECK(LV_ABS((plate_area.x1 + plate_area.x2) -
                         (image_area.x1 + image_area.x2)) <= 1 &&
                  LV_ABS((plate_area.y1 + plate_area.y2) -
                         (image_area.y1 + image_area.y2)) <= 1,
                  "Home bitmap artwork is centered inside its plate");
        }
    }
    swipe(620, 800, 120, 800, 40);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME && vqeaf_pages_home_page() == 1,
          "left swipe from Home opens page two");
    CHECK(find_label(lv_screen_active(), "Thời tiết", 0) != NULL &&
          find_label(lv_screen_active(), "Dọn dẹp ổ đĩa", 0) != NULL &&
          find_label(lv_screen_active(), "Điện thoại", 0) == NULL,
          "Home page two contains the remaining 14 real apps");
    step(500);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME && vqeaf_pages_home_page() == 1,
          "Home page two stays after release without a stray app launch");
    CHECK(!modal_up(), "a stray CLICKED after the swipe did not open a sheet");
    snapshot("home-page-2");

    /* Storage and About are launcher entries with real destination pages. */
    vqeaf_pages_apps_page_set(2);
    vqeaf_ui_show(VQEAF_PAGE_APPS);
    settle();
    label = find_label(lv_screen_active(), "Lưu trữ", 0);
    CHECK(label != NULL, "drawer lists Lưu trữ");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_STORAGE, "tap opens the real Storage page");
    vqeaf_pages_apps_page_set(2);
    vqeaf_ui_show(VQEAF_PAGE_APPS);
    settle();
    label = find_label(lv_screen_active(), "Thông tin", 0);
    CHECK(label != NULL, "drawer lists Thông tin");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_ABOUT, "tap opens the real About page");
    vqeaf_pages_home_page_set(1);
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    settle();

    swipe(620, 800, 120, 800, 20);
    CHECK(vqeaf_pages_home_page() == 1, "outward swipe clamps on the last Home page");
    swipe(120, 800, 620, 800, 20);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME && vqeaf_pages_home_page() == 0,
          "right swipe returns to Home page one");
    swipe(120, 800, 620, 800, 20);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME && vqeaf_pages_home_page() == 0,
          "outward swipe clamps without opening another section");

    /* The app drawer remains a separate pager, and its right swipe still
     * returns to the launcher. */
    vqeaf_pages_apps_page_set(0);
    vqeaf_ui_show(VQEAF_PAGE_APPS);
    settle();
    CHECK(find_label(lv_screen_active(), "Ứng dụng", 0) != NULL &&
          find_label_contains(lv_screen_active(), "Tìm trên thiết bị", 0) != NULL,
          "app list content is rendered");
    CHECK(vqeaf_pages_apps_page_count() == 4,
          "app drawer is split into four paged views");
    step(500);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_APPS,
          "app list stays after the finger is lifted, no back to Home");
    snapshot("swipe-right-apps");
    swipe(120, 800, 620, 800, 20);
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME,
          "right swipe from the app list returns Home");

    /* 15. Keyboard appearance: every theme card carries a palette row and a
     * subtitle (previously Bubble/Facet were short and broke the grid), and
     * the active breadcrumb pill no longer runs off the right edge. */
    vqeaf_ui_show(VQEAF_PAGE_KEYBOARD_THEMES);
    settle();
    CHECK(find_label(lv_screen_active(), "Mint", 0) != NULL,
          "Bubble theme has a subtitle");
    CHECK(find_label(lv_screen_active(), "Ember", 0) != NULL,
          "Facet theme has a subtitle");
    label = find_label(lv_screen_active(), "Chủ đề", 0);
    CHECK(label != NULL, "active breadcrumb pill is present");
    if(label != NULL) {
        lv_area_t pill_area;
        lv_obj_get_coords(lv_obj_get_parent(label), &pill_area);
        CHECK(pill_area.x1 >= 0 && pill_area.x2 <= 710,
              "breadcrumb active pill fits inside the canvas");
    }
    {
        lv_obj_t *nightfall = find_label(lv_screen_active(), "Nightfall", 0);
        lv_obj_t *bubble = find_label(lv_screen_active(), "Bubble", 0);
        lv_area_t a, b;
        if(nightfall != NULL && bubble != NULL) {
            lv_obj_get_coords(lv_obj_get_parent(lv_obj_get_parent(nightfall)), &a);
            lv_obj_get_coords(lv_obj_get_parent(lv_obj_get_parent(bubble)), &b);
            CHECK(LV_ABS((a.y2 - a.y1) - (b.y2 - b.y1)) <= 24,
                  "theme cards in the grid have matching heights");
        }
        else CHECK(0, "both reference theme cards are rendered");
    }
    snapshot("keyboard-themes");

    /* 17. Settings uses proper Vietnamese, and the timeout really turns the
     * backlight off and keeps the OS asleep until real input wakes it. */
    vqeaf_state_set_screen_timeout(60);
    vqeaf_ui_show(VQEAF_PAGE_SETTINGS);
    settle();
    CHECK(find_label(lv_screen_active(), "Tắt màn hình", 0) != NULL,
          "settings row uses accented Tắt màn hình");
    CHECK(find_label(lv_screen_active(), "Tat man hinh", 0) == NULL,
          "unaccented label is gone");
    label = find_label(lv_screen_active(), "1 phút", 0);
    CHECK(label != NULL, "timeout value is accented");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(find_label(lv_screen_active(), "5 phút", 0) != NULL,
          "tapping the row advances to 5 phút");
    label = find_label_contains(lv_screen_active(), "tiết kiệm pin", 0);
    CHECK(label != NULL,
          "settings explains the sleep/power-saving behaviour");
    if(label != NULL) {
        lv_area_t note_area;
        lv_obj_scroll_to_view_recursive(label, LV_ANIM_OFF);
        settle();
        lv_obj_get_coords(label, &note_area);
        int screen_h = lv_display_get_vertical_resolution(display);
        /* Settings is a system app: the dock is hidden and the content runs
         * full height, so the note scrolls fully into view instead of having
         * to clear a floating dock. */
        CHECK(!vqeaf_ui_dock_visible(),
              "Settings hides the dock like a system app");
        CHECK(note_area.y1 >= VQEAF_STATUS_H && note_area.y2 <= screen_h,
              "timeout note scrolls fully into the full-height content");
    }
    snapshot("settings-timeout");

    vqeaf_state_set_screen_timeout(1);
    vqeaf_ui_activity();
    /* The state sampler runs every second, so a one-second timeout can take
     * almost two seconds to be observed. */
    step(2100);
    CHECK(vqeaf_ui_sleeping() == 1,
          "idle timeout enters sleep");
    CHECK(vqeaf_state.screen_off == 1 && vqeaf_state.brightness == 0,
          "backlight is off while sleeping");
    CHECK(vqeaf_ui_page() == VQEAF_PAGE_LOCK,
          "sleep lands on the lock screen");
    step(1200);
    CHECK(vqeaf_ui_sleeping() == 1,
          "no input keeps the OS asleep (no accidental wake on show)");
    vqeaf_ui_activity();
    CHECK(vqeaf_ui_sleeping() == 0 && vqeaf_state.screen_off == 0,
          "real input wakes the OS");
    CHECK(vqeaf_state.brightness >= 50,
          "backlight is restored on wake");
    vqeaf_ui_toggle_screen();
    CHECK(vqeaf_ui_sleeping() == 1,
          "manual Sleep uses the same state machine");
    vqeaf_ui_toggle_screen();
    CHECK(vqeaf_ui_sleeping() == 0 && vqeaf_state.brightness >= 50,
          "manual Sleep toggles back and restores brightness");
    vqeaf_state_set_screen_timeout(60);
    vqeaf_ui_activity();

    /* 18. "Áp dụng" must change the real OS keyboards, not just the preview
     * card. Drive a live LVGL keyboard and read its actual style colour. */
    vqeaf_ui_show(VQEAF_PAGE_KEYBOARD_THEMES);
    settle();
    lv_obj_t *kb = lv_keyboard_create(lv_layer_top());
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    vqeaf_pages_kb_theme_style(kb);
    CHECK(lv_color_eq(lv_obj_get_style_bg_color(kb, LV_PART_ITEMS),
                      lv_color_hex(vqeaf_pages_kb_theme_key_color(
                          vqeaf_pages_kb_theme_applied()))),
          "default theme styles a live keyboard");
    label = find_label(lv_screen_active(), "Bubble", 0);
    CHECK(label != NULL, "Bubble card is present");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(vqeaf_pages_kb_theme_applied() ==
              vqeaf_pages_kb_theme_default(),
          "selecting a card alone does not apply it");
    label = find_label(lv_screen_active(), "Áp dụng", 0);
    CHECK(label != NULL, "Áp dụng button is present");
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    CHECK(vqeaf_pages_kb_theme_applied() == 3,
          "Áp dụng commits the Bubble theme");
    CHECK(lv_color_eq(lv_obj_get_style_bg_color(kb, LV_PART_ITEMS),
                      lv_color_hex(vqeaf_pages_kb_theme_key_color(3))),
          "live OS keyboard is restyled immediately after Áp dụng");
    CHECK(find_label_contains(lv_screen_active(), "Đang dùng: Bubble", 0) != NULL,
          "UI reports the applied theme");
    CHECK(find_label_contains(lv_screen_active(), "trong phiên này", 0) != NULL,
          "session-only scope is stated honestly");
    snapshot("keyboard-theme-applied");
    CHECK(find_label_contains(lv_screen_active(), "hiệu lực trong phiên", 0) != NULL,
          "session-only scope stays visible next to the applied theme");

    lv_obj_t *kb2 = lv_keyboard_create(lv_layer_top());
    lv_obj_add_flag(kb2, LV_OBJ_FLAG_HIDDEN);
    vqeaf_pages_kb_theme_style(kb2);
    CHECK(lv_color_eq(lv_obj_get_style_bg_color(kb2, LV_PART_ITEMS),
                      lv_color_hex(vqeaf_pages_kb_theme_key_color(3))),
          "keyboards created later inherit the applied theme");
    lv_obj_delete(kb2);
    lv_obj_delete(kb);

    /* 19. The native app page keeps the shell's status bar only when the app's
     * own pixels leave the top band blank; a painted band hides it. */
    {
        static uint32_t canvas[VQEAF_QEAPP_DEVICE_W * VQEAF_QEAPP_DEVICE_HEIGHT];
        size_t i;
        for(i = 0; i < sizeof(canvas) / sizeof(canvas[0]); i++) canvas[i] = 0;
        CHECK(vqeaf_pages_frame_top_band_empty(canvas) == 1,
              "a blank top band lets the status bar overlay the app");
        canvas[20 * VQEAF_QEAPP_DEVICE_W + 100] = 0xFFFFFFFFu;
        CHECK(vqeaf_pages_frame_top_band_empty(canvas) == 0,
              "one painted pixel in the top band hides the status bar");
        CHECK(vqeaf_pages_frame_top_band_empty(NULL) == 0,
              "no frame means no status overlay");
    }

    /* 20. Dynamic colour (Material You): one seed generates the palette, the
     * seed survives a dark/light switch, and 0 restores the stock colours. */
    {
        lv_color_t stock_accent = VQEAF_C_ACCENT;
        vqeaf_ui_show(VQEAF_PAGE_SETTINGS);
        settle();
        CHECK(find_label_contains(lv_screen_active(), "Màu động", 0) != NULL,
              "Cài đặt offers dynamic colour");
        vqeaf_theme_set_seed(0xE53935u);
        CHECK(vqeaf_theme_seed() == 0xE53935u,
              "dynamic colour keeps its seed");
        CHECK(!lv_color_eq(VQEAF_C_ACCENT, stock_accent),
              "a red seed changes the accent");
        CHECK(VQEAF_C_ACCENT.red > VQEAF_C_ACCENT.green &&
              VQEAF_C_ACCENT.red > VQEAF_C_ACCENT.blue,
              "the accent carries the seed's hue");
        vqeaf_theme_set(VQEAF_THEME_LIGHT);
        CHECK(vqeaf_theme_seed() == 0xE53935u && VQEAF_C_BG.red > 200,
              "light mode keeps the seed and stays light");
        vqeaf_theme_set_seed(0);
        vqeaf_theme_set(VQEAF_THEME_DARK);
        CHECK(lv_color_eq(VQEAF_C_ACCENT, stock_accent),
              "seed 0 restores the stock palette");

        /* Automatic mode: the seed is the wallpaper's dominant colour, so the
         * accent follows the cobalt wallpaper instead of a hand-picked seed. */
        CHECK(!vqeaf_theme_auto(), "automatic colour starts off");
        vqeaf_theme_set_auto(1);
        CHECK(vqeaf_theme_auto(), "automatic colour can be enabled");
        {
            uint32_t wall = vqeaf_theme_wallpaper_seed();
            CHECK(wall != 0, "the wallpaper yields a non-zero seed");
            CHECK(vqeaf_theme_seed() == wall,
                  "automatic mode uses the wallpaper seed");
            CHECK(!lv_color_eq(VQEAF_C_ACCENT, stock_accent),
                  "the wallpaper seed changes the accent");
            CHECK(VQEAF_C_ACCENT.green > VQEAF_C_ACCENT.red &&
                  VQEAF_C_ACCENT.blue > VQEAF_C_ACCENT.red,
                  "the accent follows the cobalt wallpaper");
            CHECK(VQEAF_C_BG.red < 80 && VQEAF_C_BG.green > VQEAF_C_BG.red,
                  "dark mode keeps a dark cobalt background");

            /* Light mode derives its own seed from the light wallpaper, and a
             * manual seed turns automatic mode back off. */
            vqeaf_theme_set(VQEAF_THEME_LIGHT);
            {
                uint32_t wall_light = vqeaf_theme_wallpaper_seed();
                CHECK(wall_light != 0 && wall_light != wall,
                      "each wallpaper has its own dominant colour");
                CHECK(vqeaf_theme_seed() == wall_light,
                      "light mode follows the light wallpaper");
                CHECK(VQEAF_C_BG.red > 200,
                      "light mode stays light under automatic colour");
            }
        }
        vqeaf_theme_set_seed(0x1EE0C8u);
        CHECK(!vqeaf_theme_auto(), "a manual seed disables automatic colour");
        CHECK(vqeaf_theme_seed() == 0x1EE0C8u,
              "the manual seed wins over the wallpaper");
        vqeaf_theme_set_auto(0);
        vqeaf_theme_set_seed(0);
        vqeaf_theme_set(VQEAF_THEME_DARK);
        CHECK(!vqeaf_theme_auto() && lv_color_eq(VQEAF_C_ACCENT, stock_accent),
              "turning automatic colour off restores the stock palette");

        /* The settings card exposes the toggle and rebuilds the page. */
        vqeaf_ui_show(VQEAF_PAGE_SETTINGS);
        settle();
        {
            lv_obj_t *toggle = find_label(lv_screen_active(),
                                          "Theo hình nền", 0);
            int x, y;
            CHECK(toggle != NULL, "Cài đặt offers the wallpaper toggle");
            if(toggle != NULL && label_target(toggle, &x, &y) == 0) tap(x, y);
            settle();
            CHECK(vqeaf_theme_auto(),
                  "tapping the toggle follows the wallpaper");
            /* The toggle is now an M3 list item with a trailing switch: its
             * "on" state is the switch track painted in the primary role. */
            toggle = find_label(lv_screen_active(), "Theo hình nền", 0);
            lv_obj_t *auto_item = toggle != NULL ?
                lv_obj_get_parent(lv_obj_get_parent(toggle)) : NULL;
            lv_obj_t *slot = auto_item != NULL ?
                lv_obj_get_child(auto_item,
                                 lv_obj_get_child_count(auto_item) - 1) : NULL;
            lv_obj_t *track = slot != NULL ? lv_obj_get_child(slot, 0) : NULL;
            CHECK(track != NULL &&
                  lv_color_eq(lv_obj_get_style_bg_color(track, 0),
                              VQEAF_C_PRIMARY),
                  "the toggle switch shows its new state");
            /* Scroll the card into view so the snapshot shows the toggle. */
            if(auto_item != NULL)
                lv_obj_scroll_to_view_recursive(auto_item, LV_ANIM_OFF);
            settle();
            snapshot("settings-dynamic-auto");
        }
        vqeaf_theme_set_auto(0);
        vqeaf_theme_set(VQEAF_THEME_DARK);
    }

    /* 21. Material Design 3: the design system is real, not a mock-up. The
     * components use the M3 colour roles and shape scale, keep a 48 px touch
     * target, paint a state layer/ripple on a real press, follow a dynamic
     * colour change, and the switch drives the actual theme. */
    {
        lv_obj_t *label;
        lv_obj_t *button;
        lv_obj_t *m3_card = NULL;
        int x, y;

        vqeaf_ui_show(VQEAF_PAGE_SETTINGS);
        settle();
        label = find_label(lv_screen_active(), "Thiết kế QEAF", 0);
        CHECK(label != NULL, "Settings renders the QEAF design card");
        if(label != NULL) {
            m3_card = lv_obj_get_parent(lv_obj_get_parent(label));
            lv_obj_scroll_to_view_recursive(m3_card, LV_ANIM_OFF);
            lv_obj_update_layout(lv_screen_active());
            snapshot("material-components");
        }

        label = find_label(lv_screen_active(), "Hộp thoại", 0);
        CHECK(label != NULL, "filled button is present");
        if(label != NULL) {
            lv_area_t area;
            button = lv_obj_get_parent(label);
            lv_obj_get_coords(button, &area);
            CHECK(area.y2 - area.y1 >= 48,
                  "filled button keeps a 48 px touch target");
            CHECK(lv_obj_get_style_radius(button, 0) == LV_RADIUS_CIRCLE,
                  "filled button uses the M3 full shape");
            CHECK(lv_color_eq(lv_obj_get_style_bg_color(button, 0),
                              VQEAF_C_PRIMARY),
                  "filled button paints the primary role");
            CHECK(lv_obj_get_child_count(button) >= 2,
                  "button carries a state-layer ripple");
        }

        label = find_label(lv_screen_active(), "Snackbar", 0);
        CHECK(label != NULL &&
              lv_color_eq(
                  lv_obj_get_style_bg_color(lv_obj_get_parent(label), 0),
                  VQEAF_C_SECONDARY_CONTAINER),
              "tonal button paints the secondary container role");

        label = find_label(lv_screen_active(), "Đặt lại màu", 0);
        CHECK(label != NULL &&
              lv_obj_get_style_border_width(lv_obj_get_parent(label), 0) == 1,
              "outlined button keeps a 1 px outline");

        label = find_label(lv_screen_active(), "Tất cả", 0);
        CHECK(label != NULL &&
              lv_obj_get_style_bg_opa(lv_obj_get_parent(label), 0) ==
                  LV_OPA_COVER,
              "selected chip uses a filled container");
        label = find_label(lv_screen_active(), "Ảnh", 0);
        CHECK(label != NULL &&
              lv_obj_get_style_bg_opa(lv_obj_get_parent(label), 0) ==
                  LV_OPA_TRANSP,
              "unselected chip stays transparent");

        /* A real press paints the ripple and the state layer. */
        label = find_label(lv_screen_active(), "Hộp thoại", 0);
        if(label != NULL && m3_card != NULL) {
            lv_obj_t *ripple;
            lv_area_t area;
            button = lv_obj_get_parent(label);
            lv_obj_scroll_to_view_recursive(m3_card, LV_ANIM_OFF);
            lv_obj_update_layout(lv_screen_active());
            lv_obj_get_coords(button, &area);
            x = (area.x1 + area.x2) / 2;
            y = (area.y1 + area.y2) / 2;
            ripple = (lv_obj_t *)lv_obj_get_user_data(button);
            press_hold(x, y, 40);
            CHECK(ripple != NULL && lv_obj_get_width(ripple) > 0,
                  "a press starts a real ripple");
            /* Snapshot mid-expansion so the ripple is visible, not a uniform
             * tint over the whole button. */
            snapshot("material-ripple");
            step(120);
            CHECK(!lv_color_eq(lv_obj_get_style_bg_color(button, 0),
                               VQEAF_C_PRIMARY),
                  "a press applies the 12% state layer");
            release_hold();
            CHECK(ripple != NULL && lv_obj_get_width(ripple) == 0,
                  "releasing the finger clears the ripple");
            /* The filled button is wired to the demo dialog, so releasing it
             * really opens an M3 dialog on the top layer. Prove the click did
             * something, then close it so the page is interactive again. */
            CHECK(find_label(lv_layer_top(), "Đặt lại bảng màu?", 0) != NULL,
                  "the filled button opens the real M3 dialog on click");
            snapshot("material-dialog");
            while(lv_obj_get_child_count(lv_layer_top()) > 0)
                lv_obj_delete(lv_obj_get_child(lv_layer_top(), 0));
            settle();
        }

        /* The tonal button shows a real snackbar with a dismiss action. */
        label = find_label(lv_screen_active(), "Snackbar", 0);
        if(label != NULL && label_target(label, &x, &y) == 0) {
            tap(x, y);
            settle();
            CHECK(find_label(lv_layer_top(), "Ẩn", 0) != NULL,
                  "the tonal button opens a real snackbar on click");
            snapshot("material-snackbar");
            while(lv_obj_get_child_count(lv_layer_top()) > 0)
                lv_obj_delete(lv_obj_get_child(lv_layer_top(), 0));
            settle();
        }

        /* The switch is the real dark-mode control, not a decorative one. */
        label = find_label(lv_screen_active(), "Chế độ tối", 0);
        CHECK(label != NULL, "dark-mode switch row is present");
        if(label != NULL && label_target(label, &x, &y) == 0) {
            tap(x, y);
            settle();
            CHECK(vqeaf_theme_get() == VQEAF_THEME_LIGHT,
                  "the M3 switch switches the real theme");
            vqeaf_ui_show(VQEAF_PAGE_SETTINGS);
            settle();
            label = find_label(lv_screen_active(), "Chế độ tối", 0);
            if(label != NULL) {
                /* Light-mode snapshot of the design card. The title is looked
                 * up (not walked up blindly) so a missing one cannot
                 * dereference NULL, and the CHECK makes a rename break the
                 * gate instead of silently dropping the snapshot again. */
                lv_obj_t *light_title =
                    find_label(lv_screen_active(), "Thiết kế QEAF", 0);
                CHECK(light_title != NULL,
                      "the design card the light snapshot shows is present");
                if(light_title != NULL) {
                    lv_obj_t *light_card =
                        lv_obj_get_parent(lv_obj_get_parent(light_title));
                    lv_obj_scroll_to_view_recursive(light_card, LV_ANIM_OFF);
                    lv_obj_update_layout(lv_screen_active());
                    snapshot("material-light");
                }
            }
            if(label != NULL && label_target(label, &x, &y) == 0) {
                tap(x, y);
                settle();
            }
            CHECK(vqeaf_theme_get() == VQEAF_THEME_DARK,
                  "the switch can switch back");
        }

        /* Components follow dynamic colour: a red seed re-tints the primary. */
        vqeaf_theme_set_seed(0xE53935u);
        vqeaf_ui_show(VQEAF_PAGE_SETTINGS);
        settle();
        label = find_label(lv_screen_active(), "Hộp thoại", 0);
        CHECK(label != NULL &&
              lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(label),
                                                    0),
                          VQEAF_C_PRIMARY) &&
              VQEAF_C_PRIMARY.red > VQEAF_C_PRIMARY.green,
              "Material components follow the dynamic colour seed");
        vqeaf_theme_set_seed(0);
        vqeaf_ui_show(VQEAF_PAGE_SETTINGS);
        settle();
    }

    /* 22. Typing on the independent IME must NOT close it. A real pointer press
     * on a key used to steal focus (every lv_obj is CLICK_FOCUSABLE by default),
     * the bound textarea got DEFOCUSED and the shell hid the IME - so the first
     * keystroke dismissed the keyboard. Drive Notes with the real pointer indev
     * (not send_event, which never transfers focus) to mirror the device. */
    {
        lv_area_t area;
        int x, y;
        vqeaf_ui_show(VQEAF_PAGE_NOTES);
        settle();
        lv_obj_t *ta = find_textarea(lv_screen_active());
        CHECK(ta != NULL, "Notes renders a real textarea");
        if(ta != NULL) {
            lv_obj_get_coords(ta, &area);
            tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
            step(400);   /* let the IME slide-up finish */
            settle();
            CHECK(vqeaf_ime_visible(), "focusing the field shows the IME");
            lv_obj_t *win = vqeaf_ime_window();
            CHECK(win != NULL, "IME window is up");
            if(win != NULL) {
                lv_obj_t *grid = vqeaf_ime_grid();
                lv_obj_t *row0 = lv_obj_get_child(grid, 0);
                lv_obj_t *qkey = lv_obj_get_child(row0, 0);
                lv_obj_t *wkey = lv_obj_get_child(row0, 1);
                lv_obj_update_layout(win);
                lv_obj_get_coords(qkey, &area);
                x = (area.x1 + area.x2) / 2; y = (area.y1 + area.y2) / 2;
                tap(x, y);
                settle();
                CHECK(vqeaf_ime_visible(),
                      "typing a key keeps the IME open (no focus steal)");
                CHECK(strcmp(lv_textarea_get_text(ta), "q") == 0,
                      "the pressed key really inserts into the field");
                lv_obj_get_coords(wkey, &area);
                x = (area.x1 + area.x2) / 2; y = (area.y1 + area.y2) / 2;
                tap(x, y);
                settle();
                CHECK(vqeaf_ime_visible(),
                      "a second keystroke keeps the IME open");
                CHECK(strcmp(lv_textarea_get_text(ta), "qw") == 0,
                      "consecutive keys accumulate in the field");
                CHECK(lv_obj_has_state(ta, LV_STATE_FOCUSED),
                      "the field keeps input focus while typing");
                snapshot("notes-ime-typing");
                /* Negative control: tapping outside the field still dismisses,
                 * so the hide-on-defocus path is proven alive, not removed. */
                tap(360, 100);
                step(400);
                CHECK(!vqeaf_ime_visible(),
                      "tapping outside the field still dismisses the IME");
            }
        }
        vqeaf_ui_show(VQEAF_PAGE_HOME);
        settle();
    }

    /* 22b. Special keys rebuild the grid; none of them may dismiss the IME. */
    {
        int x, y;
        lv_area_t area;
        vqeaf_ui_show(VQEAF_PAGE_NOTES);
        settle();
        lv_obj_t *ta = find_textarea(lv_screen_active());
        if(ta != NULL) {
            lv_textarea_set_text(ta, "");
            lv_obj_get_coords(ta, &area);
            tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
            step(400);
            settle();
            lv_obj_t *win = vqeaf_ime_window();
            CHECK(vqeaf_ime_visible() && win != NULL,
                  "special-key test: IME is up");
            if(win != NULL) {
                CHECK(ime_key_has_icon(
                          find_ime_key(win, VQEAF_SYM_KEYBOARD_RETURN),
                          VQEAF_SYM_KEYBOARD_RETURN),
                      "ENTER uses a Material Icons glyph");
                lv_obj_t *key = find_ime_key(win, VQEAF_SYM_ARROW_UPWARD);
                CHECK(key != NULL, "IME has a SHIFT key");
                CHECK(ime_key_has_icon(key, VQEAF_SYM_ARROW_UPWARD),
                      "SHIFT uses a Material Icons glyph");
                if(key != NULL) {
                    lv_obj_update_layout(win);
                    lv_obj_get_coords(key, &area);
                    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
                    settle();
                    CHECK(vqeaf_ime_visible(), "SHIFT keeps the IME open");
                    CHECK(ime_key_has_icon(
                              find_ime_key(win, VQEAF_SYM_ARROW_UPWARD),
                              VQEAF_SYM_ARROW_UPWARD),
                          "active SHIFT keeps the shift icon");
                    key = find_ime_key(win, "A");
                    if(key != NULL) {
                        lv_obj_update_layout(win);
                        lv_obj_get_coords(key, &area);
                        tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
                        settle();
                    }
                    CHECK(vqeaf_ime_visible(), "uppercase typing keeps the IME open");
                    CHECK(strcmp(lv_textarea_get_text(ta), "A") == 0,
                          "SHIFT then a inserts A");
                }
                key = find_ime_key(win, "?123");
                CHECK(key != NULL, "IME has a ?123 key");
                CHECK(key != NULL &&
                      lv_obj_get_style_text_font(lv_obj_get_child(key, 0), 0) !=
                          VQEAF_ICON_SM,
                      "symbol mode keeps a readable text label");
                if(key != NULL) {
                    lv_obj_update_layout(win);
                    lv_obj_get_coords(key, &area);
                    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
                    settle();
                    CHECK(vqeaf_ime_visible(), "?123 keeps the IME open");
                    lv_obj_t *abc = find_ime_key(win, "ABC");
                    CHECK(abc != NULL &&
                          lv_obj_get_style_text_font(lv_obj_get_child(abc, 0), 0) !=
                              VQEAF_ICON_SM,
                          "ABC mode keeps a readable text label");
                    key = find_ime_key(win, "1");
                    if(key != NULL) {
                        lv_obj_update_layout(win);
                        lv_obj_get_coords(key, &area);
                        tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
                        settle();
                    }
                    CHECK(vqeaf_ime_visible(), "symbol typing keeps the IME open");
                    key = find_ime_key(win, "ABC");
                    if(key != NULL) {
                        lv_obj_update_layout(win);
                        lv_obj_get_coords(key, &area);
                        tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
                        settle();
                    }
                    CHECK(vqeaf_ime_visible(), "returning to ABC keeps the IME open");
                    CHECK(find_ime_key(win, VQEAF_SYM_ARROW_UPWARD) != NULL &&
                          find_ime_key(win, "ABC") == NULL,
                          "ABC returns the IME to the letter grid");
                    CHECK(ime_key_has_icon(
                              find_ime_key(win, VQEAF_SYM_ARROW_UPWARD),
                              VQEAF_SYM_ARROW_UPWARD),
                          "returning to letters restores the shift icon");
                }
                key = find_ime_key(win, "SPACE");
                CHECK(key != NULL, "IME has SPACE");
                CHECK(key != NULL &&
                      lv_obj_get_style_text_font(lv_obj_get_child(key, 0), 0) !=
                          VQEAF_ICON_SM,
                      "SPACE keeps a readable text label");
                if(key != NULL) {
                    lv_obj_update_layout(win);
                    lv_obj_get_coords(key, &area);
                    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
                    settle();
                    CHECK(vqeaf_ime_visible(), "SPACE keeps the IME open");
                    CHECK(strcmp(lv_textarea_get_text(ta), "A1 ") == 0,
                          "SPACE inserts a space");
                }
                key = find_ime_key(win, VQEAF_SYM_BACKSPACE);
                CHECK(key != NULL, "IME has DEL");
                CHECK(ime_key_has_icon(key, VQEAF_SYM_BACKSPACE),
                      "DEL uses a Material Icons glyph");
                if(key != NULL) {
                    lv_obj_update_layout(win);
                    lv_obj_get_coords(key, &area);
                    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
                    settle();
                    CHECK(vqeaf_ime_visible(), "DEL keeps the IME open");
                    CHECK(strcmp(lv_textarea_get_text(ta), "A1") == 0,
                          "DEL removes the last character");
                }
                snapshot("notes-ime-special");
            }
        }
        vqeaf_ui_show(VQEAF_PAGE_HOME);
        step(400);
        settle();
    }

    /* 22c. The search dialog binds its own IME; typing there must not close it. */
    {
        int x, y;
        lv_area_t area;
        vqeaf_ui_show(VQEAF_PAGE_APPS);
        settle();
        lv_obj_t *label = find_label(lv_screen_active(),
                                     "Tìm trên thiết bị...", 0);
        CHECK(label != NULL, "Apps page has the search pill");
        if(label != NULL && label_target(label, &x, &y) == 0) {
            tap(x, y);
            step(400);
            settle();
            CHECK(vqeaf_ime_visible(), "search field opens the IME");
            lv_obj_t *ta = find_textarea(lv_layer_top());
            CHECK(ta != NULL, "search dialog keeps a real textarea");
            lv_obj_t *win = vqeaf_ime_window();
            if(win != NULL) {
                lv_obj_t *key = find_ime_key(win, "q");
                CHECK(key != NULL, "search IME has letter keys");
                if(key != NULL) {
                    lv_obj_update_layout(win);
                    lv_obj_get_coords(key, &area);
                    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
                    settle();
                    CHECK(vqeaf_ime_visible(),
                          "typing in search keeps the IME open");
                    if(ta != NULL)
                        CHECK(strcmp(lv_textarea_get_text(ta), "q") == 0,
                              "search field receives the typed letter");
                }
            }
            snapshot("search-ime-typing");
            /* Clean the top layer so later dialogs start from a known state. */
            vqeaf_ui_show(VQEAF_PAGE_HOME);
            step(400);
            while(lv_obj_get_child_count(lv_layer_top()) > 0)
                lv_obj_delete(lv_obj_get_child(lv_layer_top(), 0));
            settle();
        }
    }

    /* 22d. Quick re-focus while the slide-down is still running used to let
     * the old hide animation delete the reused IME window mid-typing. */
#if 0
    {
        lv_area_t area;
        vqeaf_ui_show(VQEAF_PAGE_NOTES);
        settle();
        lv_obj_t *ta = find_textarea(lv_screen_active());
        if(ta != NULL) {
            lv_textarea_set_text(ta, "");
            lv_obj_get_coords(ta, &area);
            tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
            step(300);
            settle();
            lv_obj_t *win = vqeaf_ime_window();
            CHECK(vqeaf_ime_visible() && win != NULL,
                  "refocus test: IME is up");
            vqeaf_ime_hide();
            vqeaf_ime_show(ta, NULL);   /* faster than the 250 ms slide-down */
            step(500);
            settle();
            CHECK(vqeaf_ime_visible(),
                  "re-focus during slide-down keeps the IME open");
            CHECK(vqeaf_ime_window() == win && win != NULL,
                  "reused IME window is not deleted by the old hide");
            win = vqeaf_ime_window();
            if(win != NULL) {
                lv_obj_t *key = find_ime_key(win, "q");
                if(key != NULL) {
                    lv_obj_update_layout(win);
                    lv_obj_get_coords(key, &area);
                    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
                    settle();
                }
                CHECK(vqeaf_ime_visible(),
                      "typing after a fast re-focus keeps the IME open");
                CHECK(strcmp(lv_textarea_get_text(ta), "q") == 0,
                      "typing after a fast re-focus reaches the field");
            }
            vqeaf_ime_hide();
            step(400);
        }
        vqeaf_ui_show(VQEAF_PAGE_HOME);
        settle();
    }
#endif

    /* 22. The Material page is the reference layout: the elevation scale is the
     * Material 3 one, shadows grow with it, and the components stack in the
     * documented order — app bar, cards, FAB, then the dialog on the top layer
     * above the 40% scrim. */
    {
        const vqeaf_m3_elevation_t *scale = vqeaf_m3_elevation_scale();
        lv_obj_t *label;
        lv_obj_t *fab;
        int x, y;

        CHECK(scale != NULL && scale[0].dp == 0 && scale[1].dp == 1 &&
              scale[2].dp == 3 && scale[3].dp == 6 && scale[4].dp == 8 &&
              scale[5].dp == 12,
              "elevation scale matches Material 3 (0,1,3,6,8,12 dp)");

        lv_obj_t *probe = lv_obj_create(lv_screen_active());
        vqeaf_m3_elevate(probe, 1);
        int shadow_1 = (int)lv_obj_get_style_shadow_width(probe, 0);
        vqeaf_m3_elevate(probe, 3);
        int shadow_3 = (int)lv_obj_get_style_shadow_width(probe, 0);
        vqeaf_m3_elevate(probe, 5);
        int shadow_5 = (int)lv_obj_get_style_shadow_width(probe, 0);
        lv_obj_delete(probe);
        CHECK(shadow_1 < shadow_3 && shadow_3 < shadow_5,
              "shadow strength grows with the elevation level");

        vqeaf_ui_show(VQEAF_PAGE_M3);
        settle();
        CHECK(find_label(lv_screen_active(), "Thiết kế QEAF", 0) != NULL,
              "the QEAF design page pins its top app bar");
        CHECK(find_label(lv_screen_active(),
                         "Thẻ elevated · elevation 1 · 1 dp", 0) != NULL,
              "cards are labelled with their elevation level");
        CHECK(find_label(lv_screen_active(), "Paper Plane", 0) != NULL,
              "the paper-plane component is on the design page");
        CHECK(find_label(lv_screen_active(), "Swipe Confirm", 0) != NULL,
              "the swipe-confirm component is on the design page");
        CHECK(find_label(lv_screen_active(), "Download Ring", 0) != NULL,
              "the download-ring component is on the design page");
        CHECK(find_label(lv_screen_active(), "Checklist", 0) != NULL,
              "the checklist component is on the design page");

        label = find_label(lv_screen_active(), "Tạo", 0);
        CHECK(label != NULL, "the extended FAB is on the page");
        if(label != NULL) {
            fab = lv_obj_get_parent(label);
            CHECK(lv_obj_get_style_shadow_width(fab, 0) == shadow_3,
                  "the FAB uses elevation level 3");
            CHECK(fab != NULL && lv_obj_get_parent(fab) != NULL,
                  "the FAB is parented on the page content");
        }

        if(label != NULL && label_target(label, &x, &y) == 0) {
            tap(x, y);
            settle();
            CHECK(find_label(lv_layer_top(), "Tạo thẻ mới?", 0) != NULL,
                  "the FAB opens the dialog on the top layer");
            snapshot("m3-layout-dialog");
            label = find_label(lv_layer_top(), "Tạo", 0);
            if(label != NULL && label_target(label, &x, &y) == 0) {
                tap(x, y);
                settle();
                CHECK(find_label_contains(lv_layer_top(), "Đã tạo thẻ mẫu",
                                          0) != NULL,
                      "confirming shows the snackbar");
            }
        }
        while(lv_obj_get_child_count(lv_layer_top()) > 0)
            lv_obj_delete(lv_obj_get_child(lv_layer_top(), 0));
        settle();
        snapshot("m3-layout");
    }

    /* 23. Liquid Glass controls are driven by the real pointer indev. */
    {
        static const vqeaf_component_item_t items[] = {
            { "Tự động", NULL, 0 },
            { "Sáng", NULL, 0 },
            { "Tối", NULL, 0 },
        };
        static const char *const labels[] = { "Một", "Hai", "Ba" };
        lv_obj_t *root = vqeaf_obj(lv_layer_top());
        lv_obj_t *slide, *slider, *toggle, *check, *tabs, *select, *action;
        lv_obj_t *label;
        lv_area_t area;
        int x, y;

        lv_obj_set_size(root, 600, 760);
        lv_obj_set_pos(root, 60, 120);
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(root, 18, 0);
        lv_obj_set_style_pad_row(root, 14, 0);
        slide = vqeaf_lg_slide_confirm(root, "Trượt để xác nhận", NULL, NULL);
        slider = vqeaf_lg_slider(root, 0);
        toggle = vqeaf_lg_switch(root, 0);
        check = vqeaf_lg_checkbox(root, "Cho phép", 0);
        tabs = vqeaf_lg_tabs(root, labels, 3, 0);
        select = vqeaf_lg_select(root, items, 3, 0, NULL, NULL);
        action = vqeaf_lg_action_button(root, VQEAF_SYM_ARROW_FORWARD, NULL, NULL);
        lv_obj_update_layout(root);
        settle();

        lv_obj_get_coords(slide, &area);
        x = (area.x1 + area.x2) / 2;
        y = (area.y1 + area.y2) / 2;
        swipe(area.x1 + 34, y, area.x2 - 34, y, 8);
        settle();
        CHECK(vqeaf_lg_slide_confirmed(slide),
              "pointer drag confirms the liquid slide control");
        snapshot("m3-liquid-controls");
        lv_obj_get_coords(slide, &area);
        tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
        settle();
        CHECK(!vqeaf_lg_slide_confirmed(slide),
              "tap resets a confirmed liquid slide control");

        lv_obj_get_coords(slider, &area);
        swipe(area.x1, (area.y1 + area.y2) / 2,
              area.x2, (area.y1 + area.y2) / 2, 8);
        settle();
        CHECK(vqeaf_lg_slider_get(slider) == 100,
              "pointer drag maps the liquid slider to its full range");

        lv_obj_get_coords(toggle, &area);
        tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
        settle();
        CHECK(vqeaf_lg_switch_get(toggle), "pointer tap toggles the liquid switch");
        lv_obj_get_coords(check, &area);
        tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
        settle();
        CHECK(vqeaf_lg_checkbox_get(check), "pointer tap checks the glass checkbox");
        lv_obj_get_coords(vqeaf_lg_tab_item(tabs, 2), &area);
        tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
        settle();
        CHECK(vqeaf_lg_tabs_selected(tabs) == 2,
              "pointer tap moves the liquid tab lens");

        lv_obj_get_coords(select, &area);
        tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
        settle();
        CHECK(vqeaf_lg_overlay_visible(select), "pointer tap opens the liquid select");
        label = find_label(lv_layer_top(), "Sáng", 0);
        CHECK(label != NULL, "liquid select exposes its real choices");
        if(label != NULL && label_target(label, &x, &y) == 0) {
            tap(x, y);
        }
        settle();
        CHECK(vqeaf_lg_selected_index(select) == 1,
              "pointer choice updates the liquid select state");
        CHECK(!vqeaf_lg_any_overlay_visible(),
              "choosing an item dismisses the liquid surface");

        lv_obj_get_coords(select, &area);
        tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
        settle();
        CHECK(vqeaf_lg_overlay_visible(select), "liquid select reopens after choice");
        step(260);
        snapshot("m3-liquid-overlay");
        tap(8, 8);
        settle();
        CHECK(!vqeaf_lg_any_overlay_visible(),
              "backdrop tap dismisses the liquid surface safely");

        lv_obj_get_coords(action, &area);
        tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
        step(500);
        settle();
        CHECK(!lv_obj_has_flag(action, LV_OBJ_FLAG_CLICKABLE),
              "liquid action arrow melts away after one activation");
        lv_obj_delete(root);
        settle();
        CHECK(!vqeaf_lg_any_overlay_visible(), "liquid probe cleanup leaves no overlay");
    }

    /* 24. Recents is the card carousel: one snapped card per open task in
     * most-recent-first order, each card opens its app, and a card can be
     * closed (or the whole list cleared) without leaving a dead entry. */
    {
        lv_obj_t *label;
        int x, y;

        vqeaf_ui_show(VQEAF_PAGE_NOTES);
        settle();
        vqeaf_ui_show(VQEAF_PAGE_SETTINGS);
        settle();
        CHECK(vqeaf_ui_recent_count() >= 2 &&
              vqeaf_ui_recent_page(0) == VQEAF_PAGE_SETTINGS,
              "recents are newest-first");

        vqeaf_ui_show(VQEAF_PAGE_RECENTS);
        settle();
        CHECK(find_label(lv_screen_active(), "Ứng dụng gần đây", 0) != NULL,
              "the switcher shows its carousel header");
        snapshot("recents-carousel");
        CHECK(find_label(lv_screen_active(), "Ghi chú", 0) != NULL,
              "every open app gets a card");
        label = find_label(lv_screen_active(), "Cài đặt", 0);
        if(label != NULL && label_target(label, &x, &y) == 0) {
            tap(x, y);
            settle();
            CHECK(vqeaf_ui_page() == VQEAF_PAGE_SETTINGS,
                  "tapping the centred card opens that app");
        }

        /* The × button is wired to these two calls (recents_close_cb rebuilds
         * the page); check the model side here and the button visually on the
         * device. */
        int before = vqeaf_ui_recent_count();
        vqeaf_ui_recent_close(VQEAF_PAGE_NOTES);
        CHECK(vqeaf_ui_recent_count() == before - 1,
              "closing a task drops it from recents");
        vqeaf_ui_show(VQEAF_PAGE_RECENTS);
        settle();
        CHECK(find_label(lv_screen_active(), "Ghi chú", 0) == NULL,
              "the closed task loses its card");
        vqeaf_ui_recent_clear();
        vqeaf_ui_show(VQEAF_PAGE_RECENTS);
        settle();
        CHECK(vqeaf_ui_recent_count() == 0 &&
              find_label(lv_screen_active(), "Chưa có ứng dụng gần đây",
                         0) != NULL,
              "clearing shows the empty state");
        snapshot("recents-empty");
    }

    /* 25. The reference layout: lock widget, launcher edit mode, honest
     * control tiles and the 2x2 power menu. Every state comes from a provider
     * or is labelled "không hỗ trợ"; nothing is a decorative toggle. */
    {
        lv_obj_t *label;
        int x, y;
        vqeaf_state_t saved_state = vqeaf_state;
        static const char *const forbidden[] = {
            "PM2.5", "AQI", "iQOO", "FlashCharge", "Gọi khẩn cấp",
            "Mở khóa bằng vân tay"
        };

        CHECK(vqeaf_settings_set_str("lock.pin", "") == 0,
              "lock test starts without a PIN");
        vqeaf_state.battery_valid = 1;
        vqeaf_state.battery_pct = 76;
        vqeaf_state.charging = 1;
        vqeaf_state.wifi_up = 1;
        vqeaf_state.storage_valid = 1;
        vqeaf_state.storage_pct = 44;
        vqeaf_state.storage_total = 128ULL * 1024ULL * 1024ULL;
        vqeaf_state.storage_free = 72ULL * 1024ULL * 1024ULL;
        /* Let the provider sampler observe the fixture before isolating the
         * notification log from its real charging transition. */
        step(1000);
        vqeaf_ui_notification_clear();

        vqeaf_ui_show(VQEAF_PAGE_LOCK);
        settle();
        CHECK(find_label(lv_screen_active(), "Không có thông báo", 0) != NULL,
              "empty lock notification state is explicit");
        snapshot("lock-empty");

        vqeaf_ui_notify("Kiểm thử", "Sự kiện thật của phiên", VQEAF_SYM_CHECK,
                        0x35E0C8);
        CHECK(vqeaf_ui_notification_count() == 1 &&
              vqeaf_ui_notification_at(0) != NULL &&
              strcmp(vqeaf_ui_notification_at(0)->title, "Kiểm thử") == 0,
              "the notification log keeps real events newest-first");
        CHECK(vqeaf_ui_notification_at(0)->uptime_s <= vqeaf_state.uptime_s,
              "each entry carries the uptime it happened at");
        vqeaf_pages_refresh();
        settle();

        label = find_label(lv_screen_active(), vqeaf_state.date_text, 0);
        lv_obj_t *clock = find_label(lv_screen_active(), vqeaf_state.clock_text, 0);
        CHECK(label != NULL && clock != NULL,
              "lock screen shows the real date and clock");
        if(label != NULL && clock != NULL) {
            lv_area_t date_area;
            lv_area_t clock_area;
            lv_obj_get_coords(label, &date_area);
            lv_obj_get_coords(clock, &clock_area);
            CHECK(date_area.y1 < clock_area.y1,
                  "lock date sits above the clock");
            CHECK(clock_area.y2 - clock_area.y1 >= 150 &&
                  lv_obj_get_style_text_font(clock, 0) == &vqeaf_font_bold_160,
                  "lock clock uses the large display face");
        }
        CHECK(find_label(lv_screen_active(), "76%", 100) != NULL,
              "lock battery card renders the real percentage");
        CHECK(find_label(lv_screen_active(), "Đang sạc", 100) != NULL,
              "lock battery card renders the real charging state");
        CHECK(find_label(lv_screen_active(), "Bật", 100) != NULL,
              "lock Wi-Fi card renders the real radio state");
        CHECK(find_label(lv_screen_active(), "44%", 100) != NULL &&
              find_label(lv_screen_active(), "Lưu trữ", 100) != NULL,
              "lock storage card renders real usage");
        CHECK(find_label(lv_screen_active(), "1 thông báo", 0) != NULL &&
              find_label(lv_screen_active(), "Kiểm thử", 0) != NULL,
              "lock notification card shows the real count and latest event");
        for(size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); i++)
            CHECK(find_label_contains(lv_screen_active(), forbidden[i], 0) == NULL,
                  forbidden[i]);
        CHECK(find_label(lv_screen_active(), "Đèn pin", 0) != NULL &&
              find_label(lv_screen_active(), "Máy ảnh", 0) != NULL &&
              find_label(lv_screen_active(), "Không hỗ trợ", 0) != NULL,
              "unsupported lock hardware is visibly disabled with an explanation");
        snapshot("lock-widget");

        label = find_label(lv_screen_active(), "Đèn pin", 0);
        if(label != NULL && first_child_target(lv_obj_get_parent(label), &x, &y) == 0)
            tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_LOCK &&
              find_label_contains(lv_layer_top(),
                                  "Máy không có đèn pin điều khiển được", 0) != NULL,
              "torch explains its missing backend without unlocking");
        while(lv_obj_get_child_count(lv_layer_top()) > 0)
            lv_obj_delete(lv_obj_get_child(lv_layer_top(), 0));
        label = find_label(lv_screen_active(), "Máy ảnh", 0);
        if(label != NULL && first_child_target(lv_obj_get_parent(label), &x, &y) == 0)
            tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_LOCK &&
              find_label_contains(lv_layer_top(),
                                  "Máy ảnh không có trên thiết bị này", 0) != NULL,
              "camera explains its missing backend without unlocking");
        while(lv_obj_get_child_count(lv_layer_top()) > 0)
            lv_obj_delete(lv_obj_get_child(lv_layer_top(), 0));
        settle();

        vqeaf_ui_notify("Thông báo trực tiếp", "Cập nhật không cần tạo lại trang",
                        VQEAF_SYM_NOTIFICATIONS, 0x8157FF);
        vqeaf_pages_refresh();
        settle();
        CHECK(find_label(lv_screen_active(), "2 thông báo", 0) != NULL &&
              find_label(lv_screen_active(), "Thông báo trực tiếp", 0) != NULL,
              "lock notification widgets refresh live");
        label = find_label(lv_screen_active(), "Thông báo trực tiếp", 0);
        if(label != NULL && label_target(label, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_CONTROL,
              "tapping a real lock notification opens Control Center");

        vqeaf_ui_show(VQEAF_PAGE_LOCK);
        vqeaf_state.battery_valid = 0;
        vqeaf_state.wifi_up = -1;
        vqeaf_state.storage_valid = 0;
        snprintf(vqeaf_state.clock_text, sizeof(vqeaf_state.clock_text), "--:--");
        snprintf(vqeaf_state.date_text, sizeof(vqeaf_state.date_text),
                 "Chưa đặt thời gian");
        vqeaf_pages_refresh();
        settle();
        CHECK(find_label(lv_screen_active(), "--:--", 100) != NULL &&
              find_label(lv_screen_active(), "Chưa rõ", 100) != NULL &&
              find_label(lv_screen_active(), "--", 100) != NULL &&
              find_label(lv_screen_active(), "76%", 100) == NULL,
              "unknown providers stay unknown after a live refresh");
        vqeaf_state = saved_state;
        vqeaf_pages_refresh();
        settle();

        label = find_label(lv_screen_active(), "Vuốt để mở khóa", 0);
        if(label != NULL && label_target(label, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME,
              "no-PIN lock tap opens Home");

        CHECK(vqeaf_settings_set_str("lock.pin", "2468") == 0,
              "test PIN is stored through Settings Service");
        vqeaf_ui_show(VQEAF_PAGE_LOCK);
        settle();
        CHECK(find_label(lv_screen_active(), "Nhấn để nhập PIN", 0) != NULL &&
              find_label(lv_screen_active(), "Vuốt để mở khóa", 0) == NULL,
              "PIN-enabled lock advertises PIN entry");
        snapshot("lock-pin");
        label = find_label(lv_screen_active(), "Nhấn để nhập PIN", 0);
        if(label != NULL && label_target(label, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_PIN,
              "PIN-enabled lock opens the real keypad");
        {
            static const char *const wrong[] = { "1", "3", "5", "7" };
            static const char *const correct[] = { "2", "4", "6", "8" };
            for(size_t i = 0; i < 4; i++) {
                label = find_label(lv_screen_active(), wrong[i], 0);
                if(label != NULL && label_target(label, &x, &y) == 0) tap(x, y);
                settle();
            }
            CHECK(vqeaf_ui_page() == VQEAF_PAGE_PIN &&
                  find_label(lv_screen_active(), "PIN sai. Thử lại.", 0) != NULL,
                  "wrong PIN is rejected on the keypad");
            for(size_t i = 0; i < 4; i++) {
                label = find_label(lv_screen_active(), correct[i], 0);
                if(label != NULL && label_target(label, &x, &y) == 0) tap(x, y);
                settle();
            }
            CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME,
                  "correct PIN opens Home");
        }
        CHECK(vqeaf_settings_set_str("lock.pin", "") == 0,
              "test PIN is cleared after the unlock regression");
        vqeaf_state = saved_state;

        vqeaf_pages_home_page_set(0);
        vqeaf_ui_show(VQEAF_PAGE_HOME);
        settle();
        long_press(12, 700);
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_APPS_EDIT,
              "long-press on blank Home wallpaper opens launcher edit mode");
        CHECK(!vqeaf_ui_dock_visible() && !vqeaf_ui_gesture_pill_visible() &&
              !vqeaf_tools_taskbar_visible(VQEAF_PAGE_APPS_EDIT),
              "launcher edit mode hides stock dock, pill and tools taskbar");
        snapshot("apps-edit-longpress");
        label = find_label(lv_screen_active(), "Xong", 0);
        if(label != NULL && label_target(label, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME,
              "Xong returns from long-press edit mode to Home");

        vqeaf_ui_show(VQEAF_PAGE_APPS_EDIT);
        settle();
        CHECK(find_label(lv_screen_active(), "Sửa màn hình chính", 0) == NULL,
              "launcher edit mode is an overlay, not a separate headed page");
        CHECK(find_label(lv_screen_active(), "Điện thoại", 0) != NULL &&
              find_label(lv_screen_active(), "Hình nền", 0) != NULL &&
              find_label(lv_screen_active(), "Tiện ích", 0) != NULL,
              "edit mode previews the real grid and offers wallpaper/widgets");
        snapshot("apps-edit");
        label = find_label(lv_screen_active(), "Tiện ích", 0);
        if(label != NULL && label_target(label, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(find_label_contains(lv_layer_top(), "Chưa có bộ chọn tiện ích", 0) != NULL,
              "Widgets explains the missing picker instead of faking one");

        swipe(620, 700, 120, 700, 24);
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_APPS_EDIT &&
              vqeaf_pages_home_page() == 1,
              "edit preview pages with the same horizontal gesture");
        CHECK(find_label(lv_screen_active(), "Công cụ", 0) != NULL,
              "edit preview page two shows the remaining real apps");
        snapshot("apps-edit-page-2");
        vqeaf_pages_home_page_set(0);
        vqeaf_ui_show(VQEAF_PAGE_APPS_EDIT);
        settle();
        label = find_label(lv_screen_active(), "Xong", 0);
        if(label != NULL && label_target(label, &x, &y) == 0) {
            tap(x, y);
            settle();
            CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME,
                  "Xong returns to the home grid");
        }

        vqeaf_ui_show(VQEAF_PAGE_HOME);
        settle();
        step(600);
        vqeaf_ui_show(VQEAF_PAGE_CONTROL);
        snapshot("control-open-early");
        step(70);
        snapshot("control-open-mid");
        vqeaf_ui_control_close();
        step(30);
        CHECK(vqeaf_ui_control_visible(),
              "close requested during opening stays in progress");
        step(600);
        settle();
        CHECK(!vqeaf_ui_control_visible() &&
              vqeaf_ui_page() == VQEAF_PAGE_HOME,
              "close during opening restores Home without animation races");
        vqeaf_ui_show(VQEAF_PAGE_CONTROL);
        step(600);
        settle();
        CHECK(vqeaf_ui_control_visible(), "control centre opens as an overlay");
        CHECK(find_label(lv_screen_active(), "Trung tâm điều khiển",
                         0) != NULL,
              "control centre header is present");
        CHECK(find_label(lv_screen_active(), "Tự xoay", 0) != NULL &&
              find_label(lv_screen_active(), "Đang tắt", 0) != NULL,
              "auto-rotate tile reports the real display flag");
        CHECK(find_label(lv_screen_active(), "Không hỗ trợ", 0) != NULL,
              "tiles without a backend say so instead of faking a toggle");
        CHECK(find_label(lv_screen_active(), "Phát Wi-Fi", 0) == NULL,
              "compact mode keeps the 5x2 quick grid");
        CHECK(vqeaf_ui_media_session() == NULL &&
              find_label(lv_screen_active(), "Phương tiện", 0) == NULL &&
              find_label(lv_screen_active(), "Đang phát", 0) == NULL,
              "no media card appears without a real playback session");
        CHECK(find_label(lv_screen_active(), "Độ sáng", 0) != NULL &&
              find_label(lv_screen_active(), "Âm lượng", 0) != NULL,
              "quick panel keeps side-by-side sliders without media");
        CHECK(vqeaf_ui_media_session_start(VQEAF_UI_MEDIA_NONE,
                                           "Bài hát kiểm thử", NULL) == -1 &&
              vqeaf_ui_media_session_start(VQEAF_UI_MEDIA_AUDIO,
                                           NULL, NULL) == -1,
              "an empty or invalid session cannot activate the media card");
        snapshot("control-tiles");
        {
            lv_obj_t *label = find_label(lv_screen_active(), "Wi-Fi", 0);
            lv_obj_t *cell = label != NULL ? lv_obj_get_parent(label) : NULL;
            uint32_t count = cell != NULL ? lv_obj_get_child_count(cell) : 0;
            lv_obj_t *circle = count >= 4 ? lv_obj_get_child(cell, 1) : NULL;
            lv_obj_t *status = count >= 4 ? lv_obj_get_child(cell, 3) : NULL;
            lv_area_t cell_area, circle_area, status_area;
            int centered = cell != NULL && circle != NULL && status != NULL;
            if(centered) {
                lv_obj_get_coords(cell, &cell_area);
                lv_obj_get_coords(circle, &circle_area);
                lv_obj_get_coords(status, &status_area);
                centered = LV_ABS((cell_area.x1 + cell_area.x2) -
                                  (circle_area.x1 + circle_area.x2)) <= 1 &&
                          LV_ABS((cell_area.y1 + cell_area.y2) -
                                 (circle_area.y1 + status_area.y2)) <= 1;
            }
            CHECK(centered,
                  "Control icon, title and status form a centered quick-tile group");
        }

        CHECK(vqeaf_ui_media_session_start(VQEAF_UI_MEDIA_AUDIO,
                  "Bài hát kiểm thử", "WAV · 44,1 kHz") == 0,
              "a real playback backend can publish an audio session");
        step(20);
        settle();
        CHECK(vqeaf_ui_media_session() != NULL &&
              vqeaf_ui_media_session()->kind == VQEAF_UI_MEDIA_AUDIO &&
              strcmp(vqeaf_ui_media_session()->title, "Bài hát kiểm thử") == 0,
              "the shell retains the active session metadata");
        label = find_label(lv_screen_active(), "Bài hát kiểm thử", 0);
        CHECK(label != NULL &&
              find_label(lv_screen_active(), "WAV · 44,1 kHz", 0) != NULL &&
              find_label(lv_screen_active(), "Âm thanh", 0) != NULL &&
              find_label(lv_screen_active(), "Đang phát", 0) != NULL,
              "an active audio session shows its real title, detail and kind");
        CHECK(find_label(lv_screen_active(), "Độ sáng", 0) != NULL &&
              find_label(lv_screen_active(), "Âm lượng", 0) != NULL,
              "sliders remain present while media is active");
        CHECK(find_label(lv_screen_active(), VQEAF_SYM_SKIP_PREVIOUS, 0) == NULL &&
              find_label(lv_screen_active(), VQEAF_SYM_SKIP_NEXT, 0) == NULL,
              "the media card has no unsupported transport controls");
        if(label != NULL)
            lv_obj_scroll_to_view_recursive(label, LV_ANIM_OFF);
        step(600);
        snapshot("control-media-audio");

        CHECK(vqeaf_ui_media_session_start(VQEAF_UI_MEDIA_VIDEO,
                  "Video kiểm thử", "QVF · RGB565") == 0,
              "a video backend can replace the session metadata");
        step(20);
        settle();
        CHECK(vqeaf_ui_media_session() != NULL &&
              vqeaf_ui_media_session()->kind == VQEAF_UI_MEDIA_VIDEO &&
              find_label(lv_screen_active(), "Video kiểm thử", 0) != NULL,
              "a video session refreshes the open control centre");
        CHECK(vqeaf_ui_media_session_start(VQEAF_UI_MEDIA_OTHER,
                  "Media kiểm thử", "PNG") == 0,
              "other real media sessions use the same shell registry");
        step(20);
        settle();
        CHECK(vqeaf_ui_media_session() != NULL &&
              vqeaf_ui_media_session()->kind == VQEAF_UI_MEDIA_OTHER &&
              find_label(lv_screen_active(), "Media kiểm thử", 0) != NULL,
              "other media sessions refresh the open control centre");

        vqeaf_ui_media_session_stop();
        step(20);
        settle();
        CHECK(vqeaf_ui_media_session() == NULL &&
              find_label(lv_screen_active(), "Media kiểm thử", 0) == NULL &&
              find_label(lv_screen_active(), "Đang phát", 0) == NULL,
              "stopping real output removes the media card immediately");
        CHECK(find_label(lv_screen_active(), "Độ sáng", 0) != NULL &&
              find_label(lv_screen_active(), "Âm lượng", 0) != NULL,
              "stopping media leaves the sliders intact");
        label = find_label(lv_screen_active(), "Âm lượng", 0);
        if(label != NULL)
            lv_obj_scroll_to_view_recursive(label, LV_ANIM_OFF);
        step(600);
        snapshot("control-media-stopped");
        label = find_label(lv_screen_active(), "Trung tâm điều khiển", 0);
        if(label != NULL)
            lv_obj_scroll_to_view_recursive(label, LV_ANIM_OFF);
        settle();
        CHECK(vqeaf_ui_control_visible(),
              "media session updates keep the control centre open");

        label = find_label_topmost(lv_screen_active(), "Mở rộng", 0);
        CHECK(label != NULL, "control centre offers expanded mode");
        if(label != NULL && label_self_target(label, &x, &y) == 0) {
            tap(x, y);
            step(600);
            settle();
            CHECK(find_label(lv_screen_active(), "Phát Wi-Fi", 0) != NULL &&
                  find_label(lv_screen_active(), "Máy bay", 0) != NULL &&
                  find_label(lv_screen_active(), "Quay màn hình", 0) != NULL,
                  "expanded mode exposes every available control");
            snapshot("control-expanded");
            label = find_label_topmost(lv_screen_active(), "Thu gọn", 0);
            if(label != NULL && label_self_target(label, &x, &y) == 0) {
                tap(x, y);
                step(600);
                settle();
            }
        }

        label = find_label_topmost(lv_screen_active(), "Sửa", 0);
        CHECK(label != NULL, "control centre offers tile editing");
        if(label != NULL && label_self_target(label, &x, &y) == 0) {
            tap(x, y);
            step(600);
            settle();
            CHECK(find_label(lv_screen_active(), "Đặt lại", 0) != NULL &&
                  find_label(lv_screen_active(), "Xong", 0) != NULL,
                  "edit mode offers reset and done");
            label = find_label_topmost(lv_screen_active(), VQEAF_SYM_REMOVE, 0);
            CHECK(label != NULL, "edit mode marks tiles removable");
            snapshot("control-edit");

            lv_obj_t *wifi = find_label(lv_screen_active(), "Wi-Fi", 0);
            lv_obj_t *bluetooth = find_label(lv_screen_active(), "Bluetooth", 0);
            CHECK(wifi != NULL && bluetooth != NULL,
                  "edit mode exposes two reorderable tiles");
            if(wifi != NULL && bluetooth != NULL &&
               label_target(wifi, &x, &y) == 0) {
                int tx, ty;
                if(label_target(bluetooth, &tx, &ty) == 0) {
                    hold_drag_to(x, y, tx, ty, 12);
                    CHECK(vqeaf_drag_active(),
                          "Control Center hold creates an active drag");
                    snapshot("control-reorder");
                    release_hold();
                    step(600);
                    settle();
                    wifi = find_label(lv_screen_active(), "Wi-Fi", 0);
                    bluetooth = find_label(lv_screen_active(), "Bluetooth", 0);
                    if(wifi != NULL && bluetooth != NULL &&
                       label_self_target(wifi, &x, &y) == 0 &&
                       label_self_target(bluetooth, &tx, &ty) == 0)
                        CHECK(tx < x,
                              "Control Center drag changes the visible tile order");
                    else
                        CHECK(0, "reordered Control Center tiles remain present");
                    CHECK(vqeaf_ui_control_visible() && !vqeaf_drag_active(),
                          "Control Center reorder keeps the panel open");
                }
            }

            label = find_label_topmost(lv_screen_active(), VQEAF_SYM_REMOVE, 0);
            if(label != NULL && label_self_target(label, &x, &y) == 0) {
                tap(x, y);
                step(600);
                settle();
                CHECK(find_label(lv_screen_active(), VQEAF_SYM_REMOVE, 0) != NULL,
                      "removing one tile keeps the edit mode usable");
            }
            label = find_label_topmost(lv_screen_active(), "Đặt lại", 0);
            if(label != NULL && label_self_target(label, &x, &y) == 0) {
                tap(x, y);
                step(600);
                settle();
                CHECK(find_label(lv_screen_active(), "Phát Wi-Fi", 0) != NULL,
                      "reset restores the session tile list");
                int rx, ry;
                wifi = find_label(lv_screen_active(), "Wi-Fi", 0);
                bluetooth = find_label(lv_screen_active(), "Bluetooth", 0);
                if(wifi != NULL && bluetooth != NULL &&
                   label_self_target(wifi, &x, &y) == 0 &&
                   label_self_target(bluetooth, &rx, &ry) == 0)
                    CHECK(x < rx,
                          "reset restores the default Control Center order");
                else
                    CHECK(0, "reset keeps the default tiles present");
            }
            label = find_label_topmost(lv_screen_active(), "Xong", 0);
            if(label != NULL && label_self_target(label, &x, &y) == 0) {
                tap(x, y);
                step(600);
                settle();
            }
            CHECK(find_label(lv_screen_active(), "Sửa", 0) != NULL &&
                  find_label(lv_screen_active(), "Phát Wi-Fi", 0) == NULL,
                  "Xong returns to the compact grid");
        }

        label = find_label(lv_screen_active(), "Đèn pin", 0);
        if(label != NULL && label_target(label, &x, &y) == 0) {
            tap(x, y);
            settle();
            CHECK(find_label(lv_layer_top(),
                             "Thiết bị không hỗ trợ chức năng này.",
                             0) != NULL,
                  "tapping an unsupported tile explains the limit");
            while(lv_obj_get_child_count(lv_layer_top()) > 0)
                lv_obj_delete(lv_obj_get_child(lv_layer_top(), 0));
            settle();
        }

        g_point.x = 360;
        g_point.y = 1120;
        g_pressed = 1;
        step(30);
        g_point.y = 1080;
        step(20);
        step(120);
        settle();
        CHECK(vqeaf_ui_control_visible(),
              "swipe-up starts an animated close");
        snapshot("control-close-mid");
        g_pressed = 0;
        step(60);
        step(240);
        settle();
        CHECK(!vqeaf_ui_control_visible(),
              "swipe-up removes the control overlay");
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME,
              "closing the control panel restores Home");

        vqeaf_ui_power_menu_show();
        settle();
        CHECK(find_label(lv_layer_top(), "Khoá", 0) != NULL &&
              find_label(lv_layer_top(), "Tắt nguồn", 0) != NULL &&
              find_label(lv_layer_top(), "Khởi động lại", 0) != NULL &&
              find_label(lv_layer_top(), "Ngủ", 0) != NULL,
              "power menu lays out all four actions");
        snapshot("power-menu-2x2");
        vqeaf_ui_power_menu_show();
        settle();

        /* The dimmed backdrop only dismisses the menu. The page underneath
         * must not receive the tap: a stray click used to land on whatever
         * sat at that point once the overlay was deleted mid-event. */
        vqeaf_ui_show(VQEAF_PAGE_HOME);
        settle();
        vqeaf_ui_power_menu_show();
        settle();
        CHECK(find_label(lv_layer_top(), "Khoá", 0) != NULL,
              "power menu is open over Home");
        /* (360, 430) is the Home search pill: a clickable target right under
         * the scrim, so a leaked release shows up as an opened search modal. */
        tap(360, 430);
        settle();
        CHECK(find_label(lv_layer_top(), "Khoá", 0) == NULL,
              "tapping the backdrop closes the power menu");
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_HOME,
              "tapping the backdrop leaves the page underneath alone");
        CHECK(lv_obj_get_child_count(lv_layer_top()) == 0,
              "tapping the backdrop opens nothing underneath");

        vqeaf_ui_notification_clear();
        CHECK(vqeaf_ui_notification_count() == 0,
              "clearing the log leaves an honest empty state");
    }

    /* 26. Music app (mockup Player / Canvas / Queue). Metadata, likes, queue
     * reorder/remove, shuffle/repeat and lyrics are real session state driven
     * by a pointer; the transport is visibly disabled because the OS ships no
     * audio codec/mixer, so nothing fakes playback. */
    {
        lv_obj_t *lab;
        int x, y;

        vqeaf_ui_show(VQEAF_PAGE_MUSIC);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_MUSIC, "the music page opens");
        CHECK(vqeaf_ui_media_session() == NULL,
              "opening Music does not claim playback without an audio backend");
        CHECK(find_label_contains(lv_screen_active(), "kHz", 0) != NULL,
              "player shows a real codec/sample-rate line");
        CHECK(find_label_contains(lv_screen_active(),
              "Không phát/tua được", 0) != NULL,
              "transport is visibly disabled with a reason");
        CHECK(find_label(lv_screen_active(), "0:00", 0) != NULL,
              "progress stays at zero (no fabricated playback)");
        snapshot("music-player");

        /* Like toggles real per-track state: the heart recolours to accent. */
        lab = find_label(lv_screen_active(), VQEAF_SYM_FAVORITE, 0);
        CHECK(lab != NULL, "player has a like button");
        if(lab != NULL) {
            lv_color_t before = lv_obj_get_style_text_color(lab, 0);
            if(label_target(lab, &x, &y) == 0) { tap(x, y); settle(); }
            lab = find_label(lv_screen_active(), VQEAF_SYM_FAVORITE, 0);
            CHECK(lab != NULL &&
                  !lv_color_eq(before, lv_obj_get_style_text_color(lab, 0)),
                  "liking recolours the heart (real state, not a dead button)");
            if(lab != NULL && label_target(lab, &x, &y) == 0) {
                tap(x, y); settle();
            }
        }

        /* More opens a metadata dialog sourced from the file itself. */
        lab = find_label(lv_screen_active(), VQEAF_SYM_MORE_VERT, 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) {
            tap(x, y); settle();
            CHECK(find_label_contains(lv_layer_top(), "Dung lượng:", 0) != NULL,
                  "more dialog shows the real file size/metadata");
            CHECK(find_label_contains(lv_layer_top(),
                  "chưa có codec âm thanh", 0) != NULL,
                  "more dialog states the playback limit honestly");
            snapshot("music-more");
            lab = find_label(lv_layer_top(), "Đóng", 0);
            if(lab != NULL && label_target(lab, &x, &y) == 0) {
                tap(x, y); settle();
            }
            CHECK(!modal_up(), "the more dialog closes");
        }

        /* Play is disabled: tapping explains the missing codec, no fake audio. */
        lab = find_label(lv_screen_active(), VQEAF_SYM_PAUSE, 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) {
            tap(x, y); settle();
            CHECK(find_label_contains(lv_layer_top(),
                  "Chưa có codec âm thanh", 0) != NULL,
                  "a play tap reports the missing codec via snackbar");
            CHECK(vqeaf_ui_media_session() == NULL,
                  "an unsupported play tap cannot activate a media session");
            snapshot("music-play-snack");
            /* Dismiss directly: idling 2.7 s here would trip the screen
             * timeout and sleep the shell to LOCK mid-test. */
            while(lv_obj_get_child_count(lv_layer_top()) > 0)
                lv_obj_delete(lv_obj_get_child(lv_layer_top(), 0));
            settle();
        }

        /* Next/prev move the real current track (codec line changes). */
        {
            char codec_before[160];
            codec_before[0] = '\0';
            lab = find_label_contains(lv_screen_active(), "kHz", 0);
            if(lab != NULL)
                snprintf(codec_before, sizeof(codec_before), "%s",
                         lv_label_get_text(lab));
            lab = find_label(lv_screen_active(), VQEAF_SYM_SKIP_NEXT, 0);
            if(lab != NULL && label_target(lab, &x, &y) == 0) {
                tap(x, y); settle();
            }
            lab = find_label_contains(lv_screen_active(), "kHz", 0);
            CHECK(lab != NULL &&
                  strcmp(lv_label_get_text(lab), codec_before) != 0,
                  "next advances to a different real track");
            snapshot("music-next");
            lab = find_label(lv_screen_active(), VQEAF_SYM_SKIP_PREVIOUS, 0);
            if(lab != NULL && label_target(lab, &x, &y) == 0) {
                tap(x, y); settle();
            }
            lab = find_label_contains(lv_screen_active(), "kHz", 0);
            CHECK(lab != NULL &&
                  strcmp(lv_label_get_text(lab), codec_before) == 0,
                  "previous returns to the original track");
        }

        /* Lyrics button switches to the canvas overlay and back. */
        lab = find_label(lv_screen_active(), VQEAF_SYM_LYRICS, 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) {
            tap(x, y); settle();
        }
        CHECK(vqeaf_pages_music_view() == 1,
              "the lyrics button switches to the canvas view");
        CHECK(find_label_contains(lv_screen_active(),
              "Đây là lời bài hát mẫu", 0) != NULL ||
              find_label_contains(lv_screen_active(),
              "Chưa có lời bài hát", 0) != NULL,
              "canvas overlays real LRC lyrics or an honest empty state");
        snapshot("music-canvas");
        lab = find_label(lv_screen_active(), VQEAF_SYM_LYRICS, 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) {
            tap(x, y); settle();
        }
        CHECK(vqeaf_pages_music_view() == 0,
              "lyrics toggles back to the player");

        /* Queue: header, real source/count, drag-reorder and remove. */
        lab = find_label(lv_screen_active(), VQEAF_SYM_QUEUE_MUSIC, 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) {
            tap(x, y); settle();
        }
        CHECK(vqeaf_pages_music_view() == 2, "the queue button opens the queue");
        CHECK(find_label(lv_screen_active(), "Hàng chờ", 0) != NULL,
              "queue has its header");
        CHECK(find_label_contains(lv_screen_active(),
              "Phát tiếp từ:", 0) != NULL,
              "queue names the real source folder and track count");
        snapshot("music-queue");

        /* Drag the first row's "=" handle down one slot: the order flips. */
        {
            lv_obj_t *h0 = find_label(lv_screen_active(), "=", 0);
            CHECK(h0 != NULL, "queue rows expose a drag handle");
            if(h0 != NULL) {
                lv_obj_t *row0 = lv_obj_get_parent(h0);
                int first_was_song =
                    find_label_contains(row0, "Test Title", 0) != NULL;
                if(label_self_target(h0, &x, &y) == 0)
                    swipe(x, y, x, y + 110, 6);
                settle();
                h0 = find_label(lv_screen_active(), "=", 0);
                if(h0 != NULL) {
                    row0 = lv_obj_get_parent(h0);
                    int first_is_song =
                        find_label_contains(row0, "Test Title", 0) != NULL;
                    CHECK(first_is_song != first_was_song,
                          "dragging the handle reorders the queue");
                }
                snapshot("music-queue-reorder");
            }
        }

        /* Remove a queue entry: real memmove, the count drops. */
        lab = find_label(lv_screen_active(), VQEAF_SYM_CLOSE, 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) {
            tap(x, y); settle();
        }
        CHECK(find_label_contains(lv_screen_active(),
              "Phát tiếp từ: /media · 1 bài", 0) != NULL,
              "removing a queue entry updates the real count");
        snapshot("music-queue-removed");

        /* Repeat cycles off -> all -> one (the icon swaps to all-inclusive). */
        lab = find_label(lv_screen_active(), VQEAF_SYM_REPEAT, 0);
        CHECK(lab != NULL, "queue exposes a repeat control");
        if(lab != NULL && label_target(lab, &x, &y) == 0) {
            tap(x, y); settle();
        }
        CHECK(find_label(lv_screen_active(), VQEAF_SYM_ALL_INCLUSIVE, 0) != NULL,
              "repeat-one swaps the icon to all-inclusive");
        lab = find_label(lv_screen_active(), VQEAF_SYM_REPEAT, 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) {
            tap(x, y); settle();
        }

        /* Shuffle toggles real state: the icon recolours to accent. */
        lab = find_label(lv_screen_active(), VQEAF_SYM_SHUFFLE, 0);
        CHECK(lab != NULL, "queue exposes a shuffle control");
        if(lab != NULL) {
            lv_color_t before = lv_obj_get_style_text_color(lab, 0);
            if(label_target(lab, &x, &y) == 0) { tap(x, y); settle(); }
            lab = find_label(lv_screen_active(), VQEAF_SYM_SHUFFLE, 0);
            CHECK(lab != NULL &&
                  !lv_color_eq(before, lv_obj_get_style_text_color(lab, 0)),
                  "shuffle toggles real state (icon recolours)");
        }

        lab = find_label(lv_screen_active(), VQEAF_SYM_QUEUE_MUSIC, 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) {
            tap(x, y); settle();
        }
        CHECK(vqeaf_pages_music_view() == 0,
              "the queue button returns to the player");
        CHECK(vqeaf_ui_media_session() == NULL,
              "track selection and queue edits never fabricate playback");
        vqeaf_ui_show(VQEAF_PAGE_HOME);
        step(400); settle();
    }

    /* The built-in Video page decodes a still QVF frame for inspection. That
     * is not timed output and must not activate the Control Center media card. */
    {
        vqeaf_ui_show(VQEAF_PAGE_VIDEO);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_VIDEO,
              "the video inspection page opens");
        CHECK(vqeaf_ui_media_session() == NULL,
              "a static QVF frame preview is not a playback session");
        vqeaf_ui_show(VQEAF_PAGE_HOME);
        step(400); settle();
    }

    /* 14. "Dùng USB cho": the sheet a freshly plugged cable shows. Three real
     * rows, a tap reports that row's pick, and the sheet is gone afterwards
     * through the deferred close (never inside its own handler). */
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    step(200);
    settle();
    vqeaf_ui_usb_dialog_show();
    settle();
    CHECK(vqeaf_ui_usb_dialog_visible(), "a plugged cable opens the USB sheet");
    CHECK(find_label(lv_layer_top(), "Dùng USB cho", 0) != NULL,
          "USB sheet has a title");
    CHECK(find_label(lv_layer_top(), "Sạc thiết bị", 0) != NULL,
          "USB sheet offers charge only");
    CHECK(find_label(lv_layer_top(), "Truyền tệp", 0) != NULL,
          "USB sheet offers file transfer");
    CHECK(find_label(lv_layer_top(), "Truyền ảnh", 0) == NULL,
          "no photo choice the kernel cannot honour");
    CHECK(find_label_contains(lv_layer_top(),
                              "chưa hỗ trợ: kernel không có PTP", 0) != NULL,
          "the sheet says photo transfer is unsupported");
    CHECK(vqeaf_ui_take_usb_choice() == -1, "no pick before a tap");
    snapshot("usb-sheet");
    label = find_label(lv_layer_top(), "Truyền tệp", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    step(80);
    settle();
    CHECK(vqeaf_ui_take_usb_choice() == VQEAF_USB_FILE,
          "tapping a row reports that pick");
    CHECK(!vqeaf_ui_usb_dialog_visible(), "the sheet closes after the pick");
    CHECK(find_label(lv_layer_top(), "Sạc thiết bị", 0) == NULL,
          "a closed sheet leaves no rows behind");
    /* A tap on the dimmed backdrop cancels: no mode change is reported. */
    vqeaf_ui_usb_dialog_show();
    settle();
    tap(12, 12);
    settle();
    step(80);
    settle();
    CHECK(!vqeaf_ui_usb_dialog_visible(), "tapping the backdrop dismisses it");
    CHECK(vqeaf_ui_take_usb_choice() == -1, "a dismissal reports no pick");

    /* 15. Settings > USB: the same chooser, reachable with no cable. The row
     * displays the mode the platform applied, a tap opens the sheet, and the
     * pick is what main applies to the gadget. */
    vqeaf_ui_set_usb_mode_provider(NULL);
    CHECK(vqeaf_ui_usb_mode() == -1,
          "no USB provider reads as unknown, not as a made-up mode");
    vqeaf_ui_set_usb_mode_provider(fake_usb_mode);
    /* The shell may describe only the two rows it offers. Any other value a
     * provider could hand back (a reintroduced third mode, a stale platform
     * enum) must read as unknown, so this gate goes red instead of the row
     * leaking a mode it cannot name. */
    g_usb_mode = 2;
    CHECK(vqeaf_ui_usb_mode() == -1,
          "a provider value outside charge/file reads as unknown");
    g_usb_mode = 7;
    CHECK(vqeaf_ui_usb_mode() == -1,
          "an out-of-range provider value reads as unknown");
    g_usb_mode = VQEAF_USB_CHARGE;
    vqeaf_ui_show(VQEAF_PAGE_SETTINGS);
    step(300);
    settle();
    CHECK(find_label(lv_screen_active(), "USB", 0) != NULL,
          "Settings lists a USB entry");
    CHECK(find_label(lv_screen_active(),
                     "Chỉ sạc · máy ẩn khỏi máy tính", 0) != NULL,
          "the USB row shows the mode the platform applied");
    snapshot("settings-usb");
    label = find_label(lv_screen_active(), "USB", 0);
    if(label_target(label, &x, &y) == 0) {
        tap(x, y);
        settle();
        step(80);
        settle();
    }
    CHECK(vqeaf_ui_usb_dialog_visible(),
          "the USB row opens the chooser with no cable plugged");
    label = find_label(lv_layer_top(), "Truyền tệp", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    step(80);
    settle();
    CHECK(vqeaf_ui_take_usb_choice() == VQEAF_USB_FILE,
          "picking file transfer from Settings reports the file mode");
    /* main applies that pick to the gadget on the device; mirror it here. */
    g_usb_mode = VQEAF_USB_FILE;
    vqeaf_pages_refresh();
    settle();
    CHECK(vqeaf_ui_usb_mode() == VQEAF_USB_FILE,
          "the changed mode is what the shell now reads");
    CHECK(find_label(lv_screen_active(), "Truyền tệp · ổ QEAFRESCUE", 0) !=
              NULL,
          "the row reflects the new mode without rebuilding the page");
    label = find_label(lv_screen_active(), "USB", 0);
    if(label_target(label, &x, &y) == 0) {
        tap(x, y);
        settle();
        step(80);
        settle();
    }
    CHECK(vqeaf_ui_usb_dialog_visible(),
          "the entry opens again after a mode change");
    label = find_label(lv_layer_top(), "Sạc thiết bị", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    step(80);
    settle();
    CHECK(vqeaf_ui_take_usb_choice() == VQEAF_USB_CHARGE,
          "picking charge from Settings reports the charge mode");
    g_usb_mode = VQEAF_USB_CHARGE;
    vqeaf_pages_refresh();
    settle();
    CHECK(find_label(lv_screen_active(),
                     "Chỉ sạc · máy ẩn khỏi máy tính", 0) != NULL,
          "the row returns to charge only");
    /* Charge-only hides the rescue drive, so the way back must not need a
     * replug: the same Settings entry has to restore file transfer. */
    label = find_label(lv_screen_active(), "USB", 0);
    if(label_target(label, &x, &y) == 0) {
        tap(x, y);
        settle();
        step(80);
        settle();
    }
    CHECK(vqeaf_ui_usb_dialog_visible(),
          "charge-only still offers the chooser");
    label = find_label(lv_layer_top(), "Truyền tệp", 0);
    if(label_target(label, &x, &y) == 0) tap(x, y);
    settle();
    step(80);
    settle();
    CHECK(vqeaf_ui_take_usb_choice() == VQEAF_USB_FILE,
          "charge-only switches back to file transfer without a replug");
    g_usb_mode = VQEAF_USB_FILE;
    vqeaf_pages_refresh();
    settle();
    CHECK(find_label(lv_screen_active(), "Truyền tệp · ổ QEAFRESCUE", 0) !=
              NULL,
          "the Settings row shows the rescue drive is back");

    /* 16. Security centre: passive Wi-Fi inventory and offline CVE lookup.
     * The provider-off state must remain explicit; enabling the host provider
     * exposes the real scan fixture without adding any active-attack control. */
    {
        lv_obj_t *lab;
        lv_obj_t *ta;
        int saved_wifi_present = vqeaf_state.wifi_if_present;
        char saved_wifi_ifname[sizeof(vqeaf_state.wifi_ifname)];
        snprintf(saved_wifi_ifname, sizeof(saved_wifi_ifname), "%s",
                 vqeaf_state.wifi_ifname);

        vqeaf_state.wifi_if_present = 0;
        vqeaf_state.wifi_ifname[0] = '\0';
        vqeaf_ui_show(VQEAF_PAGE_SECURITY);
        settle();
        CHECK(find_label(lv_screen_active(), "Trung tâm bảo mật", 0) != NULL,
              "Security hub renders its own identity");
        CHECK(find_label(lv_screen_active(), "Wi-Fi chưa sẵn sàng", 0) != NULL,
              "Security hub reflects the unavailable Wi-Fi provider");
        snapshot("security-hub");
        lab = find_label(lv_screen_active(), "Kiểm tra Wi-Fi", 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_WIFI_AUDIT,
              "Security hub opens the Wi-Fi audit page");
        CHECK(find_label(lv_screen_active(),
                         "Không có giao diện mạng (thiếu driver/firmware).",
                         0) != NULL,
              "missing Wi-Fi provider is reported, not simulated");
        snapshot("wifi-audit-unavailable");

        vqeaf_state.wifi_if_present = 1;
        snprintf(vqeaf_state.wifi_ifname, sizeof(vqeaf_state.wifi_ifname),
                 "wlan0");
        lab = find_label(lv_screen_active(), "Quét lại", 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_WIFI_AUDIT,
              "rescan rebuilds the audit from the provider");
        CHECK(find_label(lv_screen_active(), "TOTOLINK_A720R", 0) != NULL &&
              find_label(lv_screen_active(), "QEAF-GUEST", 0) != NULL,
              "audit lists networks returned by the scan provider");
        CHECK(find_label(lv_screen_active(), "Mạng mở", 0) != NULL,
              "an open network receives a defensive risk label");
        CHECK(find_label(lv_screen_active(), "Dừng", 0) == NULL &&
              find_label(lv_screen_active(), "Tấn công", 0) == NULL,
              "passive audit exposes no attack or stop controls");
        snapshot("wifi-audit");

        lab = find_label(lv_screen_active(), VQEAF_SYM_ARROW_BACK, 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_SECURITY,
              "Wi-Fi audit back affordance returns to Security");
        lab = find_label(lv_screen_active(), "Cơ sở dữ liệu lỗ hổng", 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_ADVISORIES,
              "Security hub opens the offline advisory catalog");
        CHECK(find_label_contains(lv_screen_active(), "8 kết quả", 0) != NULL,
              "advisory page reports the real catalog result count");

        ta = find_textarea(lv_screen_active());
        CHECK(ta != NULL, "advisory page has a real search textarea");
        if(ta != NULL) lv_textarea_set_text(ta, "eternalblue");
        settle();
        CHECK(find_label(lv_screen_active(), "CVE-2017-0144", 0) != NULL &&
              find_label(lv_screen_active(), "CVE-2021-44228", 0) == NULL,
              "typing refreshes advisory results in place");
        snapshot("advisories-search-ui");
        lab = find_label(lv_screen_active(), "CVE-2017-0144", 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_ADVISORY_DETAIL,
              "tapping an advisory opens its detail page");
        CHECK(find_label(lv_screen_active(), "Khắc phục", 0) != NULL &&
              find_label(lv_screen_active(), "Microsoft", 0) != NULL,
              "advisory detail shows source metadata and remediation");
        snapshot("advisory-detail-ui");
        lab = find_label(lv_screen_active(), VQEAF_SYM_ARROW_BACK, 0);
        if(lab != NULL && label_target(lab, &x, &y) == 0) tap(x, y);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_ADVISORIES,
              "advisory detail returns to the filtered list");

        vqeaf_pages_advisory_query_set("");
        vqeaf_state.wifi_if_present = saved_wifi_present;
        snprintf(vqeaf_state.wifi_ifname, sizeof(vqeaf_state.wifi_ifname),
                 "%s", saved_wifi_ifname);
        vqeaf_ui_show(VQEAF_PAGE_HOME);
        step(400);
        settle();
    }

    int saved_volume = vqeaf_state.volume;
    vqeaf_state.volume = 60;
    vqeaf_sound_shutdown();
    g_sound_write_fail = 0;
    vqeaf_sound_set_backend(&test_sound_backend);
    CHECK(vqeaf_sound_init() == 0, "shell sound status uses a probed PCM backend");
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    vqeaf_ui_show(VQEAF_PAGE_CONTROL);
    label = find_label(lv_screen_active(), "PCM kiểm thử", 0);
    CHECK(label != NULL, "Control Center displays the live PCM backend");
    g_sound_write_fail = 1;
    step(50);
    vqeaf_pages_refresh();
    if(label != NULL)
        CHECK(strcmp(lv_label_get_text(label), "PCM ghi âm thanh thất bại") == 0,
              "Control Center refreshes a runtime PCM failure without rebuilding");
    else
        CHECK(0, "Control Center retains its PCM status label");
    vqeaf_sound_shutdown();
    vqeaf_sound_set_backend(NULL);
    vqeaf_sound_init();
    vqeaf_ui_show(VQEAF_PAGE_HOME);
    step(50);
    vqeaf_state.volume = saved_volume;

    {
        char key[32];
        int saved_theme = vqeaf_theme_get();
        vqeaf_settings_backend_t backend = {
            setup_settings_save, setup_settings_load, NULL
        };

        vqeaf_settings_reset();
        vqeaf_setup_reset();
        vqeaf_ui_show(VQEAF_PAGE_HOME);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_SETUP && vqeaf_setup_required(),
              "clearing the completion marker redirects Home back to setup");
        CHECK(tap_text(lv_screen_active(), "Bắt đầu") == 0,
              "setup flow can restart after a settings reset");
        for(int i = 0; i < VQEAF_SETTINGS_MAX - 1; i++) {
            snprintf(key, sizeof(key), "setup.test.%d", i);
            CHECK(vqeaf_settings_set_int(key, i) == 0,
                  "settings rejection fixture fills the real store");
        }
        CHECK(tap_text(lv_screen_active(), "Tạo PIN") == 0,
              "security opens the setup PIN keypad");
        CHECK(vqeaf_setup_subpage_active(VQEAF_PAGE_PIN),
              "setup PIN is an allowed temporary subpage");
        CHECK(enter_digits("1357") == 0 && enter_digits("1357") == 0,
              "setup PIN keypad accepts numeric input");
        CHECK(find_label(lv_screen_active(),
                         "Settings Service từ chối PIN. Thử lại sau.", 0) != NULL,
              "PIN storage rejection is reported instead of accepted");
        CHECK(tap_text(lv_screen_active(), "Hủy") == 0,
              "cancelling setup PIN returns to setup security");
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_SETUP &&
              find_label(lv_screen_active(), "Bảo mật", 0) != NULL,
              "PIN cancel does not open normal Security");
        snapshot("setup-security");

        vqeaf_settings_reset();
        vqeaf_setup_reset();
        vqeaf_setup_show_step(1);
        CHECK(tap_text(lv_screen_active(), "Tạo PIN") == 0 &&
              enter_digits("2468") == 0 && enter_digits("1357") == 0,
              "setup PIN accepts the first entry and a mismatched confirmation");
        CHECK(find_label(lv_screen_active(), "Không khớp. Nhập lại PIN mới.", 0) != NULL,
              "mismatched setup PIN restarts confirmation without storing it");
        CHECK(enter_digits("2468") == 0 && enter_digits("2468") == 0,
              "setup creates and confirms a PIN through Settings Service");
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_SETUP &&
              find_label(lv_screen_active(), "Mạng", 0) != NULL &&
              strcmp(vqeaf_settings_get_str("lock.pin", ""), "2468") == 0,
              "confirmed setup PIN advances to network and is readable");
        tap_text(lv_layer_top(), "OK");
        snapshot("setup-network");
        CHECK(vqeaf_state.wifi_if_present ?
              find_label(lv_screen_active(), vqeaf_state.wifi_ifname, 0) != NULL :
              find_label(lv_screen_active(), "Không có", 0) != NULL,
              "setup network renders the actual Wi-Fi interface state");
        CHECK(tap_text(lv_screen_active(), "Xem trạng thái Wi-Fi") == 0,
              "setup opens the existing Wi-Fi detail page");
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_WIFI &&
              vqeaf_setup_subpage_active(VQEAF_PAGE_WIFI),
              "setup Wi-Fi subpage is routed and tracked");
        CHECK(tap_text(lv_screen_active(), VQEAF_SYM_ARROW_BACK) == 0 &&
              find_label(lv_screen_active(), "Mạng", 0) != NULL,
              "Wi-Fi back returns to setup network instead of Settings");
        swipe(710, 700, 570, 700, 12);
        swipe(360, 1545, 360, 1300, 12);
        swipe(360, 8, 360, 200, 12);
        settle();
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_SETUP &&
              find_label(lv_screen_active(), "Mạng", 0) != NULL &&
              !vqeaf_ui_control_visible(),
              "network-step shell gestures cannot bypass setup");
        CHECK(tap_text(lv_screen_active(), "Tiếp tục") == 0,
              "network advances to appearance");
        snapshot("setup-appearance");
        CHECK(tap_text(lv_screen_active(),
                       vqeaf_theme_get() == VQEAF_THEME_DARK ? "Sáng" : "Tối") == 0,
              "appearance offers the opposite real theme");
        CHECK(vqeaf_theme_get() != saved_theme &&
              vqeaf_ui_page() == VQEAF_PAGE_SETUP,
              "theme selection applies immediately without leaving setup");
        CHECK(tap_text(lv_screen_active(), "Hoàn tất") == 0,
              "appearance advances to ready");
        snapshot("setup-ready");
        CHECK(find_label(lv_screen_active(), "Sẵn sàng!", 0) != NULL &&
              find_label(lv_screen_active(), "Đã đặt (phiên)", 0) != NULL,
              "ready summarizes the real PIN and setup state");

        vqeaf_settings_reset();
        for(int i = 0; i < VQEAF_SETTINGS_MAX; i++) {
            snprintf(key, sizeof(key), "setup.test.%d", i);
            vqeaf_settings_set_int(key, i);
        }
        vqeaf_setup_show_step(4);
        CHECK(tap_text(lv_screen_active(), "Vào hệ điều hành") == 0,
              "ready finish can be exercised");
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_SETUP && vqeaf_setup_required() &&
              find_label_contains(lv_layer_top(),
                                  "Settings Service không lưu được", 0) != NULL,
              "completion rejection keeps setup open and explains why");

        vqeaf_settings_reset();
        setup_settings_save_fail = 0;
        CHECK(vqeaf_settings_attach_backend(&backend) == 0,
              "failing durable backend attaches for setup write-through test");
        setup_settings_save_fail = 1;
        vqeaf_setup_reset();
        vqeaf_setup_show_step(4);
        CHECK(tap_text(lv_screen_active(), "Vào hệ điều hành") == 0,
              "finish runs against the failing durable backend");
        CHECK(vqeaf_ui_page() == VQEAF_PAGE_LOCK && !vqeaf_setup_required() &&
              vqeaf_settings_persistent() &&
              find_label_contains(lv_layer_top(),
                                  "Setup chỉ hoàn tất trong phiên", 0) != NULL,
              "durable save failure permits only an honestly warned session completion");
        snapshot("setup-complete-lock");

        vqeaf_settings_reset();
        vqeaf_setup_reset();
        CHECK(vqeaf_setup_required(),
              "setup returns after simulated reboot with session-only settings");
        vqeaf_setup_show_step(4);
        CHECK(tap_text(lv_screen_active(), "Vào hệ điều hành") == 0 &&
              vqeaf_ui_page() == VQEAF_PAGE_LOCK && !vqeaf_setup_required() &&
              !vqeaf_settings_persistent(),
              "normal session-only setup completion reaches lock");
        vqeaf_theme_set(saved_theme);
    }

    printf(g_failures == 0 ? "ALL OK\n" : "%d FAILURES\n", g_failures);
    free(framebuffer);
    return g_failures == 0 ? 0 : 1;
}