/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_ui.c - LVGL phone shell for VQEAF OS. */
#include "vqeaf_ui.h"

#include <stdio.h>
#include <string.h>

#include "vqeaf_pages.h"
#include "vqeaf_tools_ui.h"
#include "vqeaf_ime.h"
#include "vqeaf_qeapp_device.h"
#include "vqeaf_state.h"
#include "vqeaf_theme.h"
#include "violet.h"
#include "vqeaf_shutdown.h"
#include <vqeaf/settings.h>
#include "icons/vqeaf_app_icons.h"

static lv_display_t *g_display;
static lv_obj_t *g_status;
static lv_obj_t *g_status_time;
static lv_obj_t *g_status_wifi;
static lv_obj_t *g_status_battery;
static lv_obj_t *g_content;
static lv_obj_t *g_nav;
static lv_obj_t *g_nav_items[4];
static lv_obj_t *g_home_bar;
static lv_obj_t *g_sysnav;
static int g_virtual_keys;
static lv_timer_t *g_state_timer;
static vqeaf_page_t g_page = VQEAF_PAGE_LOCK;
static uint64_t g_last_activity_ms = 0;
/* Real sleep state: the backlight is off and PID 1 stops LVGL refreshes.
 * g_dimmed is the "screen going dark" step just before the timeout. */
static int g_sleeping;
static int g_dimmed;
static int g_saved_brightness = 85;
/* Provider baselines for the notification log: -2 means "not sampled yet". */
static int g_prev_wifi = -2;
static int g_prev_charging;
static vqeaf_page_t g_recent_pages[4] = {VQEAF_PAGE_COUNT, VQEAF_PAGE_COUNT,
                                         VQEAF_PAGE_COUNT, VQEAF_PAGE_COUNT};
static int g_recent_count = 0;

static const vqeaf_page_t nav_pages[4] = {
    VQEAF_PAGE_HOME, VQEAF_PAGE_APPS,
    VQEAF_PAGE_CONTROL, VQEAF_PAGE_SETTINGS
};
/* Dock move mode. Long-press opens the app info sheet everywhere in the
 * shell; the sheet's "Di chuyển" button arms this mode, and the next dock
 * tap drops the app into that slot. The dock content lives in vqeaf_pages.c. */
static int g_move_app = -1;
static vqeaf_sync_list_fn g_sync_list;
static vqeaf_sync_read_fn g_sync_read;

static void build_dock_items(void);
static void update_navigation(void);

/* The bottom bar follows the QeafdropOs launcher dock: a floating glass slab
 * with the same portrait fractions (82/844 H tall, 8/390 W side margins, 39%
 * radius) and the iOS home bar underneath. The four destinations keep their
 * labels, so the icon plate plus caption is centred in the dock instead of
 * filling it edge to edge. */
typedef struct {
    int dock_h;
    int dock_top;
    int margin;
    int radius;
    int icon;
    int home_w;
    int home_h;
    int home_bottom;
} nav_metrics_t;

static nav_metrics_t nav_metrics(void)
{
    nav_metrics_t m;
    int w = lv_display_get_horizontal_resolution(g_display);
    int h = lv_display_get_vertical_resolution(g_display);

    if(h >= w) {
        m.dock_h = (h * 82 + 422) / 844;
        m.margin = (w * 8 + 195) / 390;
        m.radius = (m.dock_h * 39 + 50) / 100;
        m.dock_top = h - (h * 20 + 422) / 844 - m.dock_h;
        m.home_w = (w * 134 + 195) / 390;
        m.home_h = (h * 5 + 422) / 844;
        if(m.home_h < 3) m.home_h = 3;
        m.home_bottom = (h * 8 + 422) / 844;
    }
    else {
        /* Landscape keeps the QeafdropOs iPad-style dock numbers. */
        m.dock_h = 86;
        m.margin = 10;
        m.radius = 25;
        m.dock_top = h - 16 - m.dock_h;
        m.home_w = 140;
        m.home_h = 5;
        m.home_bottom = 6;
    }
    m.icon = (m.dock_h * 52 + 50) / 100;
    return m;
}

static int nav_top(void)
{
    return nav_metrics().dock_top;
}

static void update_status(void)
{
    char battery[16];

    if(g_status_time)
        lv_label_set_text(g_status_time, vqeaf_state.clock_text);
    if(g_status_wifi) {
        if(vqeaf_state.wifi_up > 0)
            lv_label_set_text(g_status_wifi, VQEAF_SYM_WIFI);
        else
            lv_label_set_text(g_status_wifi, VQEAF_SYM_SIGNAL_WIFI_OFF);
        lv_obj_set_style_text_color(g_status_wifi,
            vqeaf_state.wifi_up > 0 ? VQEAF_C_ACCENT : VQEAF_C_TEXT_MUTE, 0);
    }
    if(g_status_battery) {
        if(vqeaf_state.battery_valid)
            snprintf(battery, sizeof(battery), "%d%%", vqeaf_state.battery_pct);
        else
            snprintf(battery, sizeof(battery), "--");
        lv_label_set_text(g_status_battery, battery);
        /* Charging is a real sysfs state; the accent colour is the only
         * indicator, because there is no battery glyph in the status row. */
        lv_obj_set_style_text_color(g_status_battery,
            vqeaf_state.charging ? VQEAF_C_ACCENT : VQEAF_C_TEXT_DIM, 0);
    }
}

/* Note activity WITHOUT waking. Page changes must reset the idle timer but
 * must never turn the backlight back on: the old code called
 * vqeaf_ui_activity() from vqeaf_ui_show(), which undid the timeout's
 * screen_off flag immediately after it was set. */
static void note_activity(void)
{
    g_last_activity_ms = vio_monotonic_ms();
}

static void sleep_enter(void)
{
    if(g_sleeping) return;
    if(!g_dimmed)
        g_saved_brightness = vqeaf_state.brightness > 0 ?
                             vqeaf_state.brightness : 85;
    g_sleeping = 1;
    g_dimmed = 0;
    vqeaf_state.screen_off = 1;
    vio_set_backlight(0);
    vqeaf_ui_show(VQEAF_PAGE_LOCK);
    vio_log("power: sleep (backlight off, LVGL refresh paused)");
}

static void sleep_dim(void)
{
    if(g_dimmed || g_sleeping) return;
    g_saved_brightness = vqeaf_state.brightness > 0 ?
                         vqeaf_state.brightness : 85;
    g_dimmed = 1;
    vio_set_backlight(g_saved_brightness / 4);
    vio_log("power: dim before sleep");
}

static void sleep_exit(void)
{
    if(!g_sleeping) return;
    g_sleeping = 0;
    g_dimmed = 0;
    vqeaf_state.screen_off = 0;
    vqeaf_state_set_brightness(g_saved_brightness);
    note_activity();
    vqeaf_ui_show(VQEAF_PAGE_LOCK);
    if(g_display != NULL) lv_refr_now(g_display);
    vio_log("power: wake (backlight %d)", g_saved_brightness);
}

int vqeaf_ui_sleeping(void)
{
    return g_sleeping;
}

static void state_timer_cb(lv_timer_t *timer)
{
    uint64_t now, limit, idle;
    int timeout;

    (void)timer;
    /* A screen-timeout blank or a lock-screen switch during the restarting
     * screen would hide it; the reset is only ~1 s away. */
    if(vqeaf_shutdown_active()) return;
    vqeaf_state_refresh();
    update_status();
    vqeaf_pages_refresh();

    /* Real provider transitions become notification-log entries. The first
     * tick only records the baseline, so boot does not emit a fake change. */
    if(g_prev_wifi == -2) {
        g_prev_wifi = vqeaf_state.wifi_up;
        g_prev_charging = vqeaf_state.battery_valid ? vqeaf_state.charging : 0;
    }
    else {
        if(vqeaf_state.wifi_up != g_prev_wifi &&
           vqeaf_state.wifi_up >= 0) {
            vqeaf_ui_notify("Wi-Fi",
                            vqeaf_state.wifi_up > 0 ? "Đã kết nối mạng" :
                            "Đã ngắt kết nối",
                            vqeaf_state.wifi_up > 0 ? VQEAF_SYM_WIFI :
                            VQEAF_SYM_SIGNAL_WIFI_OFF,
                            vqeaf_state.wifi_up > 0 ? 0x35E0C8 : 0x8A9A98);
            g_prev_wifi = vqeaf_state.wifi_up;
        }
        if(vqeaf_state.battery_valid &&
           vqeaf_state.charging != g_prev_charging) {
            vqeaf_ui_notify("Pin", vqeaf_state.charging ?
                            "Đang sạc" : "Đã rút sạc",
                            VQEAF_SYM_BATTERY_CHARGING_FULL,
                            vqeaf_state.charging ? 0x35E0C8 : 0x8A9A98);
            g_prev_charging = vqeaf_state.charging;
        }
    }

    now = vio_monotonic_ms();
    if(g_last_activity_ms == 0) g_last_activity_ms = now;
    timeout = vqeaf_state.screen_timeout_s;
    if(timeout <= 0 || g_sleeping) return;
    limit = (uint64_t)timeout * 1000u;
    idle = now - g_last_activity_ms;
    if(idle >= limit) sleep_enter();
    else if(limit > 4000u && idle >= limit - 3000u) sleep_dim();
}

void vqeaf_ui_set_sync_list(vqeaf_sync_list_fn fn)
{
    g_sync_list = fn;
}

int vqeaf_ui_sync_list(const char *path, vqeaf_sync_entry_t *out, int max)
{
    if(g_sync_list == NULL || path == NULL || out == NULL || max <= 0) return 0;
    return g_sync_list(path, out, max);
}

void vqeaf_ui_set_sync_read(vqeaf_sync_read_fn fn)
{
    g_sync_read = fn;
}

int vqeaf_ui_read_file(const char *name, uint8_t **out, size_t *size)
{
    if(g_sync_read == NULL || name == NULL || out == NULL || size == NULL)
        return -1;
    return g_sync_read(name, out, size);
}

/* ---- notification log ---------------------------------------------------- */
/* No notification daemon exists on this initramfs. The control centre shows
 * the events the shell actually observed this session, each stamped with the
 * uptime at which it happened. There is no seed entry, so an untouched boot
 * shows an honest empty state. */
#define VQEAF_NOTIFY_MAX 12

static vqeaf_notification_t g_notifications[VQEAF_NOTIFY_MAX];
static int g_notification_count;

static void copy_text(char *dst, size_t cap, const char *src)
{
    if(cap == 0) return;
    if(src == NULL) src = "";
    snprintf(dst, cap, "%s", src);
}

void vqeaf_ui_notify(const char *title, const char *body, const char *symbol,
                     uint32_t color)
{
    int i;

    if(title == NULL || title[0] == '\0') return;
    if(g_notification_count < VQEAF_NOTIFY_MAX) g_notification_count++;
    for(i = g_notification_count - 1; i > 0; i--)
        g_notifications[i] = g_notifications[i - 1];
    copy_text(g_notifications[0].title, sizeof(g_notifications[0].title),
              title);
    copy_text(g_notifications[0].body, sizeof(g_notifications[0].body), body);
    copy_text(g_notifications[0].symbol, sizeof(g_notifications[0].symbol),
              symbol);
    g_notifications[0].color = color;
    g_notifications[0].uptime_s = vqeaf_state.uptime_s;
}

int vqeaf_ui_notification_count(void)
{
    return g_notification_count;
}

const vqeaf_notification_t *vqeaf_ui_notification_at(int index)
{
    if(index < 0 || index >= g_notification_count) return NULL;
    return &g_notifications[index];
}

void vqeaf_ui_notification_clear(void)
{
    memset(g_notifications, 0, sizeof(g_notifications));
    g_notification_count = 0;
}

/* ---- display auto-rotate ------------------------------------------------- */
/* display_hal owns the flag and the orientation policy. The device links the
 * HAL; the host render has no panel, so it reports off and ignores the setter
 * instead of pretending a rotation happened. */
#ifdef __linux__
extern int display_auto_rotate(void);
extern void display_set_auto_rotate(int on);
#endif

int vqeaf_ui_auto_rotate(void)
{
#ifdef __linux__
    return display_auto_rotate();
#else
    return 0;
#endif
}

void vqeaf_ui_set_auto_rotate(int on)
{
#ifdef __linux__
    display_set_auto_rotate(on);
    vio_log("display: auto-rotate %s", on ? "on" : "off");
#else
    (void)on;
#endif
}

void vqeaf_ui_dock_move_arm(int app_id)
{
    if(vqeaf_app_by_id(app_id) == NULL) return;
    g_move_app = app_id;
    update_navigation();
}

int vqeaf_ui_dock_visible(void)
{
    return g_nav != NULL && !lv_obj_has_flag(g_nav, LV_OBJ_FLAG_HIDDEN);
}

int vqeaf_ui_gesture_pill_visible(void)
{
    return g_home_bar != NULL && !lv_obj_has_flag(g_home_bar, LV_OBJ_FLAG_HIDDEN);
}

/* Rebuild after the tap event finishes, so the tapped widget is not deleted
 * while LVGL is still dispatching events to it. */
static void dock_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    build_dock_items();
    update_navigation();
}

static void dock_event_cb(lv_event_t *event)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(event);
    const vqeaf_app_t *app;

    if(g_move_app >= 0) {
        int moving = g_move_app;
        g_move_app = -1;
        vqeaf_dock_drop(moving, slot);
        lv_timer_t *timer = lv_timer_create(dock_refresh_cb, 30, NULL);
        if(timer != NULL) lv_timer_set_repeat_count(timer, 1);
        return;
    }
    app = vqeaf_app_by_id(vqeaf_dock_slot(slot));
    if(app != NULL && app->page != VQEAF_PAGE_COUNT)
        vqeaf_ui_show(app->page);
}

static void dock_info_cb(lv_event_t *event)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(event);
    int app_id = vqeaf_dock_slot(slot);

    if(app_id >= 0)
        vqeaf_pages_app_info(app_id);
}

static void status_event_cb(lv_event_t *event)
{
    (void)event;
    if(g_page != VQEAF_PAGE_LOCK)
        vqeaf_ui_show(VQEAF_PAGE_CONTROL);
}

/* Shell gestures mirror the touch conventions the shell can actually honour:
 * swipe up unlocks, swipe down over chrome opens the quick panel, and a
 * horizontal swipe moves between the four nav destinations or returns from a
 * detail page. Vertical swipes inside a scrolling page stay scrolls.
 * The theme's containers all carry LV_OBJ_FLAG_GESTURE_BUBBLE, so the event
 * reaches this screen handler.
 *
 * One contact = one navigation. Changing the page rebuilds g_content while
 * the finger is still down; LVGL then sees a new active object and clears
 * gesture_sent (lv_indev.c:1267), which would re-fire LV_EVENT_GESTURE and
 * walk through several more pages, sometimes back to Home. Waiting for the
 * release drops the rest of that drag and also prevents a stray CLICKED on
 * the freshly built tiles. */
static void gesture_show(lv_indev_t *indev, vqeaf_page_t page)
{
    lv_indev_wait_release(indev);
    vqeaf_ui_show(page);
}

/* Pages where shell-wide gestures are allowed. App/content pages keep their
 * own swipes (scrolling, tabs, gallery paging, media controls); a global
 * handler there steals the gesture from the app. */
static int shell_gesture_page(vqeaf_page_t page)
{
    switch(page) {
    case VQEAF_PAGE_LOCK:
    case VQEAF_PAGE_HOME:
    case VQEAF_PAGE_APPS:
    case VQEAF_PAGE_CONTROL:
    case VQEAF_PAGE_SETTINGS:
    case VQEAF_PAGE_RECENTS:
        return 1;
    default:
        return 0;
    }
}

static int settings_detail_page(vqeaf_page_t page)
{
    switch(page) {
    case VQEAF_PAGE_STORAGE:
    case VQEAF_PAGE_ABOUT:
    case VQEAF_PAGE_KEYBOARD:
    case VQEAF_PAGE_KEYBOARD_FEEDBACK:
    case VQEAF_PAGE_WIFI:
    case VQEAF_PAGE_DRIVERS:
    case VQEAF_PAGE_SYNC:
    case VQEAF_PAGE_LOCK_THEME:
        return 1;
    default:
        return 0;
    }
}

static int gesture_blocked_by_overlay(void)
{
    return vqeaf_ime_visible() || vqeaf_ui_usb_dialog_visible() ||
           vqeaf_pages_app_info_visible();
}

static int g_recent_index;   /* highlighted card in the task switcher */
static void recents_swipe(lv_indev_t *indev, lv_dir_t dir);

/* LVGL only exposes the current point on a GESTURE event, not where the finger
 * went down. Track the press edge from a fast timer so edge gestures can be
 * distinguished from an ordinary swipe in the middle of the screen. */
static lv_point_t g_press_start;
static uint32_t g_press_ms;
static int g_was_pressed;

static void input_track_cb(lv_timer_t *timer)
{
    lv_indev_t *dev = lv_indev_get_next(NULL);
    lv_point_t pt = { 0, 0 };
    int pressed;

    (void)timer;
    while(dev != NULL && lv_indev_get_type(dev) != LV_INDEV_TYPE_POINTER)
        dev = lv_indev_get_next(dev);
    if(dev == NULL) return;
    pressed = lv_indev_get_state(dev) == LV_INDEV_STATE_PRESSED;
    if(pressed && !g_was_pressed) {
        lv_indev_get_point(dev, &pt);
        g_press_start = pt;
        g_press_ms = (uint32_t)vio_monotonic_ms();
    }
    g_was_pressed = pressed;
}

static int edge_from_bottom(void)
{
    int h = (int)lv_display_get_vertical_resolution(g_display);
    return h > 0 && g_press_start.y >= h - 90;
}

static int edge_from_top(void)
{
    return g_press_start.y <= VQEAF_STATUS_H + 24;
}

static int edge_from_side(void)
{
    int w = (int)lv_display_get_horizontal_resolution(g_display);
    return w > 0 && (g_press_start.x <= 36 || g_press_start.x >= w - 36);
}

static int gesture_held(void)
{
    return (uint32_t)vio_monotonic_ms() - g_press_ms >= 500;
}

/* QEAF-style Back: content pages return to Apps, settings details return
 * to Settings, the drawer returns Home, and Home's right edge opens Apps. */
static void gesture_back(lv_indev_t *indev, lv_dir_t dir)
{
    if(settings_detail_page(g_page)) {
        gesture_show(indev, VQEAF_PAGE_SETTINGS);
        return;
    }
    if(g_page == VQEAF_PAGE_APPS) {
        if(dir == LV_DIR_RIGHT || vqeaf_pages_apps_page() == 0)
            gesture_show(indev, VQEAF_PAGE_HOME);
        else
            vqeaf_pages_apps_page_set(vqeaf_pages_apps_page() - 1);
        return;
    }
    if(g_page == VQEAF_PAGE_HOME) {
        if(dir == LV_DIR_RIGHT) gesture_show(indev, VQEAF_PAGE_APPS);
        return;
    }
    if(g_page == VQEAF_PAGE_RECENTS) {
        recents_swipe(indev, dir);
        return;
    }
    gesture_show(indev, VQEAF_PAGE_APPS);
}

static void recents_swipe(lv_indev_t *indev, lv_dir_t dir)
{
    int count = vqeaf_ui_recent_count();
    if(count <= 0) {
        gesture_show(indev, VQEAF_PAGE_HOME);
        return;
    }
    g_recent_index = (g_recent_index + (dir == LV_DIR_LEFT ? 1 : count - 1)) % count;
    vqeaf_page_t page = vqeaf_ui_recent_page(g_recent_index);
    if(page == VQEAF_PAGE_COUNT)
        gesture_show(indev, VQEAF_PAGE_HOME);
    else
        gesture_show(indev, page);
}

static void gesture_cb(lv_event_t *event)
{
    lv_indev_t *indev = lv_indev_active();
    lv_obj_t *origin = lv_event_get_target(event);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    int index, from_status, from_pill, from_nav;

    if(indev == NULL) return;
    if(gesture_blocked_by_overlay()) return;
    vqeaf_ui_activity();
    from_status = (origin == g_status);
    from_pill = (origin == g_home_bar);
    from_nav = (origin == g_nav);

    if(g_page == VQEAF_PAGE_LOCK) {
        if(dir == LV_DIR_TOP) vqeaf_lock_unlock();
        return;
    }

    /* Edge gestures take priority over page-specific handling. The finger's
     * start point and how long it was held come from input_track_cb(). */
    if(dir == LV_DIR_TOP && edge_from_bottom()) {
        if(gesture_held()) {
            g_recent_index = vqeaf_ui_recent_count() - 1;
            if(g_recent_index < 0) g_recent_index = 0;
            gesture_show(indev, VQEAF_PAGE_RECENTS);
        } else {
            gesture_show(indev, VQEAF_PAGE_HOME);
        }
        return;
    }
    if(dir == LV_DIR_BOTTOM && edge_from_top()) {
        gesture_show(indev, VQEAF_PAGE_CONTROL);
        return;
    }
    if((dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) && edge_from_side()) {
        if(edge_from_top()) {
            gesture_show(indev, VQEAF_PAGE_CONTROL);   /* upper edge action */
        } else if(gesture_held()) {
            recents_swipe(indev, dir);                 /* switch between apps */
        } else {
            gesture_back(indev, dir);                  /* edge = Back */
        }
        return;
    }

    /* A swipe that starts on app content belongs to the app. Only the status
     * bar and the gesture pill may open shell overlays from inside an app. */
    if(!shell_gesture_page(g_page)) {
        if(dir == LV_DIR_BOTTOM && from_status)
            gesture_show(indev, VQEAF_PAGE_CONTROL);
        else if(dir == LV_DIR_TOP && from_pill) {
            g_recent_index = vqeaf_ui_recent_count() - 1;
            if(g_recent_index < 0) g_recent_index = 0;
            gesture_show(indev, VQEAF_PAGE_RECENTS);
        }
        return;
    }

    if(dir == LV_DIR_BOTTOM) {
        /* Pull down from the status bar (or the top of a launcher page) opens
         * the quick panel. Control's own scroll is left alone. */
        if(g_page != VQEAF_PAGE_CONTROL &&
           (from_status || (lv_obj_get_scroll_top(g_content) <= 0 &&
                            g_page != VQEAF_PAGE_SETTINGS)))
            gesture_show(indev, VQEAF_PAGE_CONTROL);
        return;
    }
    if(dir == LV_DIR_TOP) {
        /* Swipe up on the pill (or a launcher page) opens the task switcher. */
        if(g_page != VQEAF_PAGE_RECENTS && (from_pill || g_page != VQEAF_PAGE_CONTROL)) {
            g_recent_index = vqeaf_ui_recent_count() - 1;
            if(g_recent_index < 0) g_recent_index = 0;
            gesture_show(indev, VQEAF_PAGE_RECENTS);
        }
        return;
    }
    if(dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;

    /* Horizontal swipes on the task switcher move between recent tasks, not
     * back to Home - that is what a "multitasking tab" swipe should do. */
    if(g_page == VQEAF_PAGE_RECENTS) {
        recents_swipe(indev, dir);
        return;
    }
    /* The app drawer is a pager. */
    if(g_page == VQEAF_PAGE_APPS) {
        if(dir == LV_DIR_RIGHT && vqeaf_pages_apps_page() == 0) {
            gesture_show(indev, VQEAF_PAGE_HOME);
            return;
        }
        vqeaf_pages_apps_page_set(vqeaf_pages_apps_page() +
                                  (dir == LV_DIR_LEFT ? 1 : -1));
        gesture_show(indev, VQEAF_PAGE_APPS);
        return;
    }
    if(settings_detail_page(g_page)) {
        gesture_show(indev, VQEAF_PAGE_SETTINGS);
        return;
    }
    /* A right swipe from Home is the launcher's "all apps" gesture. Without
     * this branch the nav index would wrap around to Settings instead. */
    if(dir == LV_DIR_RIGHT && g_page == VQEAF_PAGE_HOME) {
        gesture_show(indev, VQEAF_PAGE_APPS);
        return;
    }
    /* Horizontal nav switching only from the dock itself (or plain chrome);
     * swipes on content are left to the content. */
    if(!from_nav && !from_status && !from_pill && g_page != VQEAF_PAGE_HOME &&
       g_page != VQEAF_PAGE_APPS && g_page != VQEAF_PAGE_SETTINGS)
        return;
    for(index = 0; index < 4; index++)
        if(nav_pages[index] == g_page) break;
    if(index == 4) return;
    index = (index + (dir == LV_DIR_LEFT ? 1 : 3)) % 4;
    gesture_show(indev, nav_pages[index]);
}

static void build_status_bar(lv_obj_t *screen)
{
    g_status = vqeaf_obj(screen);
    lv_obj_add_flag(g_status, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_pos(g_status, 0, 0);
    lv_obj_set_size(g_status, lv_pct(100), VQEAF_STATUS_H);
    lv_obj_set_style_pad_left(g_status, 30, 0);
    lv_obj_set_style_pad_right(g_status, 30, 0);
    /* The wallpaper fills the screen, so the chrome must sit on top of the
     * photo rather than draw its own light slab. */
    lv_obj_set_style_bg_color(g_status,
        vqeaf_wallpaper_active() ? lv_color_black() : VQEAF_C_BG, 0);
    lv_obj_set_style_bg_opa(g_status,
        vqeaf_wallpaper_active() ? 120 : LV_OPA_TRANSP, 0);
    if(vqeaf_theme_liquid_glass()) {
        vqeaf_glass_surface(g_status, 0, LV_OPA_40);
        lv_obj_set_style_border_width(g_status, 0, 0);
    }
    lv_obj_set_flex_flow(g_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_status, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_color_t chrome_ink = vqeaf_wallpaper_text_color();
    g_status_time = vqeaf_label(g_status, "--:--", VQEAF_FONT_CAPTION,
                                chrome_ink);
    lv_obj_t *right = vqeaf_obj(g_status);
    /* Content-sized flex rows inside a padded bar settle at the wrong width
     * on the first layout, which pushed the battery text past the edge; a
     * fixed box keeps the icons inside the 30 px status padding. */
    lv_obj_set_size(right, 124, 30);
    lv_obj_set_style_pad_column(right, 12, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_status_wifi = vqeaf_label(right, VQEAF_SYM_SIGNAL_WIFI_OFF,
                                VQEAF_ICON_SM, chrome_ink);
    /* A content-sized icon label inside a content-sized flex row can settle
     * at zero width before the first layout; give it its own box. */
    lv_obj_set_size(g_status_wifi, 34, 30);
    lv_obj_set_style_text_align(g_status_wifi, LV_TEXT_ALIGN_CENTER, 0);
    g_status_battery = vqeaf_label(right, "--", VQEAF_FONT_CAPTION,
                                   chrome_ink);
    lv_obj_set_width(g_status_battery, 60);
    lv_obj_set_style_text_align(g_status_battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(g_status, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_status, status_event_cb, LV_EVENT_CLICKED, NULL);
}

static void home_bar_event_cb(lv_event_t *event)
{
    (void)event;
    if(g_page != VQEAF_PAGE_LOCK)
        vqeaf_ui_show(VQEAF_PAGE_HOME);
}

static void build_navigation(lv_obj_t *screen)
{
    nav_metrics_t m = nav_metrics();
    int dock_w = lv_display_get_horizontal_resolution(g_display) - 2 * m.margin;

    g_nav = vqeaf_obj(screen);
    lv_obj_add_flag(g_nav, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_pos(g_nav, m.margin, m.dock_top);
    lv_obj_set_size(g_nav, dock_w, m.dock_h);
    if(vqeaf_theme_fluent()) {
        /* Windows 11 taskbar: the dock becomes one acrylic plate instead of
         * bare icons floating over the wallpaper. */
        vqeaf_acrylic_surface(g_nav, 20);
    }
    else {
        /* Keep the dock icons, remove the enclosing slab/frame. */
        lv_obj_set_style_bg_color(g_nav,
            vqeaf_wallpaper_active() ? lv_color_black() : VQEAF_C_SURFACE_2, 0);
        lv_obj_set_style_bg_opa(g_nav,
            vqeaf_wallpaper_active() ? 120 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_nav, 0, 0);
        lv_obj_set_style_radius(g_nav, m.radius, 0);
        lv_obj_set_style_shadow_width(g_nav, 0, 0);
        lv_obj_set_style_shadow_opa(g_nav, LV_OPA_0, 0);
    }
    lv_obj_set_style_pad_hor(g_nav, 10, 0);
    lv_obj_set_flex_flow(g_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_nav, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    build_dock_items();

    g_home_bar = vqeaf_obj(screen);
    lv_obj_add_flag(g_home_bar, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(g_home_bar, m.home_w, m.home_h);
    lv_obj_align(g_home_bar, LV_ALIGN_BOTTOM_MID, 0, -m.home_bottom);
    lv_obj_set_style_radius(g_home_bar, m.home_h / 2, 0);
    lv_obj_set_style_bg_color(g_home_bar,
        vqeaf_wallpaper_text_color(), 0);
    lv_obj_set_style_bg_opa(g_home_bar, LV_OPA_60, 0);
    lv_obj_add_flag(g_home_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_home_bar, home_bar_event_cb, LV_EVENT_CLICKED, NULL);
}

static void sysnav_cb(lv_event_t *event)
{
    int id = (int)(intptr_t)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_active();
    vqeaf_page_t page;

    if(indev != NULL) lv_indev_wait_release(indev);
    if(id == 1) {
        page = VQEAF_PAGE_HOME;
    } else if(id == 2) {
        g_recent_index = vqeaf_ui_recent_count() - 1;
        if(g_recent_index < 0) g_recent_index = 0;
        page = VQEAF_PAGE_RECENTS;
    } else if(settings_detail_page(g_page)) {
        page = VQEAF_PAGE_SETTINGS;
    } else if(g_page == VQEAF_PAGE_APPS) {
        page = VQEAF_PAGE_HOME;
    } else if(g_page == VQEAF_PAGE_HOME) {
        return;                 /* Back is a no-op on the launcher */
    } else {
        page = VQEAF_PAGE_APPS;
    }
    vqeaf_ui_show(page);
}

static void build_sysnav(lv_obj_t *screen)
{
    static const char *const glyphs[3] = {
        VQEAF_SYM_ARROW_BACK, VQEAF_SYM_HOME, VQEAF_SYM_APPS
    };

    g_sysnav = vqeaf_obj(screen);
    lv_obj_set_size(g_sysnav, 420, 66);
    lv_obj_align(g_sysnav, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(g_sysnav, VQEAF_C_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(g_sysnav, LV_OPA_90, 0);
    lv_obj_set_style_radius(g_sysnav, 22, 0);
    if(vqeaf_theme_liquid_glass())
        vqeaf_glass_surface(g_sysnav, 22, (lv_opa_t)190);
    lv_obj_add_flag(g_sysnav, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(g_sysnav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_sysnav, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for(int i = 0; i < 3; i++) {
        lv_obj_t *btn = vqeaf_obj(g_sysnav);
        lv_obj_set_size(btn, 110, 56);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t *label = vqeaf_label(btn, glyphs[i], VQEAF_ICON,
                                      VQEAF_C_TEXT);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn, sysnav_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
    if(!g_virtual_keys)
        lv_obj_add_flag(g_sysnav, LV_OBJ_FLAG_HIDDEN);
}

void vqeaf_ui_set_virtual_keys(int on)
{
    g_virtual_keys = on ? 1 : 0;
    vqeaf_settings_set_bool("nav.virtual_keys", g_virtual_keys);
    if(g_sysnav != NULL) {
        if(g_virtual_keys) lv_obj_clear_flag(g_sysnav, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_sysnav, LV_OBJ_FLAG_HIDDEN);
    }
    if(g_home_bar != NULL) {
        if(g_virtual_keys) lv_obj_add_flag(g_home_bar, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(g_home_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

int vqeaf_ui_virtual_keys(void) { return g_virtual_keys; }

static void build_dock_items(void)
{
    nav_metrics_t m = nav_metrics();

    lv_obj_clean(g_nav);
    for(int i = 0; i < VQEAF_DOCK_SLOTS; i++) {
        const vqeaf_app_t *app = vqeaf_app_by_id(vqeaf_dock_slot(i));
        lv_obj_t *item = vqeaf_obj(g_nav);
        g_nav_items[i] = item;
        lv_obj_set_height(item, lv_pct(100));
        lv_obj_set_flex_grow(item, 1);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(item, 6, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        /* Material state layer: the dock item tints while pressed. No ripple
         * child here, because update_navigation() indexes child 0/1. */
        vqeaf_m3_state_layer(item);

        /* Rounded plate behind the icon, like a dock app icon. The plate
         * stays even for bitmap icons so the active/move border has a box. */
        lv_obj_t *plate = lv_obj_create(item);
        lv_obj_remove_style_all(plate);
        lv_obj_set_size(plate, m.icon, m.icon);
        lv_obj_set_style_radius(plate, m.icon * 3 / 10, 0);
        const lv_image_dsc_t *dsc =
            app != NULL ? vqeaf_app_icon(app->icon) : NULL;
        int app_gradient = app != NULL && dsc == NULL &&
                           !vqeaf_theme_liquid_glass();
        lv_obj_set_style_bg_color(plate,
            app_gradient ? lv_color_hex(app->fg) :
            app != NULL ? lv_color_hex(app->bg) : VQEAF_C_SURFACE_3, 0);
        lv_obj_set_style_bg_opa(plate,
            dsc != NULL ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(plate,
            vqeaf_theme_liquid_glass() || app_gradient ? 1 : 0, 0);
        lv_obj_set_style_border_color(plate,
            vqeaf_theme_liquid_glass() ? lv_color_hex(0xF2D8FF) :
            app_gradient ? lv_color_white() : VQEAF_C_ACCENT, 0);
        if(vqeaf_theme_liquid_glass())
            lv_obj_set_style_border_opa(plate, LV_OPA_70, 0);
        else if(app_gradient)
            vqeaf_app_icon_style(plate, lv_color_hex(app->fg),
                                 lv_color_hex(app->bg));
        lv_obj_remove_flag(plate, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(plate, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(plate, LV_OBJ_FLAG_GESTURE_BUBBLE);
        if(dsc != NULL) {
            lv_obj_t *img = vqeaf_icon_img(plate, dsc, m.icon);
            lv_obj_center(img);
        }
        else {
            lv_obj_t *glyph = vqeaf_glyph(plate,
                app != NULL ? app->symbol : VQEAF_SYM_ADD, VQEAF_ICON,
                app_gradient ? lv_color_white() :
                app != NULL ? lv_color_hex(app->fg) : VQEAF_C_TEXT_MUTE);
            lv_obj_center(glyph);
        }

        lv_obj_t *label = vqeaf_label(item,
            app != NULL ? app->name : "Trống", VQEAF_FONT_CAPTION,
            app != NULL ? VQEAF_C_TEXT_DIM : VQEAF_C_TEXT_MUTE);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        if(app != NULL) {
            lv_obj_add_event_cb(item, dock_event_cb, LV_EVENT_SHORT_CLICKED,
                                (void *)(intptr_t)i);
            lv_obj_add_event_cb(item, dock_info_cb, LV_EVENT_LONG_PRESSED,
                                (void *)(intptr_t)i);
        }
    }
}

/* The status bar and the dock are built once, at create time. A palette change
 * (dark/light, or a new dynamic-colour seed) must repaint them too instead of
 * leaving dark chrome over a light page. */
static vqeaf_theme_mode_t g_built_mode = VQEAF_THEME_LIGHT;
static uint32_t g_built_seed;
static int g_built_glass;
static int g_built_fluent;

static void refresh_chrome(void)
{
    /* The status bar sits over the screen's own background (the content starts
     * at y=VQEAF_STATUS_H), so the screen colour must follow the palette too. */
    lv_obj_t *screen = lv_screen_active();
    if(screen != NULL)
        lv_obj_set_style_bg_color(screen, VQEAF_C_BG, 0);
    if(g_status != NULL) {
        lv_obj_set_style_bg_color(g_status, VQEAF_C_BG, 0);
        if(vqeaf_theme_liquid_glass()) {
            vqeaf_glass_surface(g_status, 0, LV_OPA_40);
            lv_obj_set_style_border_width(g_status, 0, 0);
        }
        else {
            lv_obj_set_style_bg_grad_dir(g_status, LV_GRAD_DIR_NONE, 0);
            lv_obj_set_style_bg_opa(g_status, LV_OPA_20, 0);
            lv_obj_set_style_border_width(g_status, 0, 0);
        }
        if(g_status_time != NULL)
            lv_obj_set_style_text_color(g_status_time, VQEAF_C_TEXT, 0);
        if(g_status_wifi != NULL)
            lv_obj_set_style_text_color(g_status_wifi, VQEAF_C_TEXT_MUTE, 0);
        if(g_status_battery != NULL)
            lv_obj_set_style_text_color(g_status_battery, VQEAF_C_TEXT_DIM, 0);
    }
    if(g_nav != NULL) {
        lv_obj_set_style_bg_color(g_nav, VQEAF_C_SURFACE_2, 0);
        lv_obj_set_style_bg_opa(g_nav, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_grad_dir(g_nav, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_border_color(g_nav, VQEAF_C_LINE, 0);
        lv_obj_set_style_border_opa(g_nav, LV_OPA_0, 0);
        lv_obj_set_style_border_width(g_nav, 0, 0);
        lv_obj_set_style_shadow_width(g_nav, 0, 0);
        lv_obj_set_style_shadow_opa(g_nav, LV_OPA_0, 0);
        build_dock_items();
    }
    if(g_sysnav != NULL) {
        lv_obj_set_style_bg_color(g_sysnav, VQEAF_C_SURFACE_2, 0);
        lv_obj_set_style_bg_opa(g_sysnav, LV_OPA_90, 0);
        lv_obj_set_style_bg_grad_dir(g_sysnav, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_radius(g_sysnav, 22, 0);
        lv_obj_set_style_border_width(g_sysnav, 0, 0);
        if(vqeaf_theme_liquid_glass())
            vqeaf_glass_surface(g_sysnav, 22, (lv_opa_t)190);
    }
    if(g_home_bar != NULL)
        lv_obj_set_style_bg_color(g_home_bar, VQEAF_C_TEXT, 0);
}

static void update_navigation(void)
{
    for(int i = 0; i < VQEAF_DOCK_SLOTS; i++) {
        const vqeaf_app_t *app = vqeaf_app_by_id(vqeaf_dock_slot(i));
        int active = app != NULL && app->page == g_page &&
                     g_page != VQEAF_PAGE_LOCK;
        lv_obj_t *plate = lv_obj_get_child(g_nav_items[i], 0);
        lv_obj_t *label = lv_obj_get_child(g_nav_items[i], 1);
        int app_gradient = app != NULL &&
                           vqeaf_app_icon(app->icon) == NULL &&
                           !vqeaf_theme_liquid_glass();
        if(plate)
            lv_obj_set_style_border_width(plate,
                active || g_move_app >= 0 ? 2 :
                vqeaf_theme_liquid_glass() || app_gradient ? 1 : 0, 0);
        if(plate && vqeaf_theme_liquid_glass())
            lv_obj_set_style_border_color(plate,
                active ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xD9B6E6), 0);
        if(label)
            lv_obj_set_style_text_color(label,
                active ? VQEAF_C_TEXT :
                vqeaf_theme_liquid_glass() ? lv_color_hex(0xEBD8F2) :
                                             VQEAF_C_TEXT_DIM, 0);
    }
}

void vqeaf_ui_create(lv_display_t *display)
{
    lv_obj_t *screen;

    g_display = display;
    if(g_display == NULL) return;
    /* The palette is a runtime table, not a compile-time constant. */
    vqeaf_theme_set(vqeaf_theme_get());
    vqeaf_shutdown_init();
    screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, VQEAF_C_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    g_content = vqeaf_obj(screen);
    lv_obj_set_pos(g_content, 0, VQEAF_STATUS_H);
    lv_obj_set_size(g_content, lv_pct(100),
                    nav_top() - VQEAF_STATUS_H);
    lv_obj_set_style_bg_color(g_content, VQEAF_C_BG, 0);
    lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_content, LV_SCROLLBAR_MODE_OFF);

    build_status_bar(screen);
    /* Taskbar is a sibling placed under the status bar. Creating it before
     * navigation keeps its tap targets above page content but below the
     * full-screen sheets on the top layer. */
    vqeaf_tools_taskbar_create(screen);
    build_navigation(screen);
    build_sysnav(screen);
    g_virtual_keys = vqeaf_settings_get_bool("nav.virtual_keys", 0);
    if(g_virtual_keys) {
        if(g_sysnav != NULL) lv_obj_clear_flag(g_sysnav, LV_OBJ_FLAG_HIDDEN);
        if(g_home_bar != NULL) lv_obj_add_flag(g_home_bar, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_event_cb(screen, gesture_cb, LV_EVENT_GESTURE, NULL);
    {
        lv_timer_t *track = lv_timer_create(input_track_cb, 10, NULL);
        if(track != NULL) lv_timer_set_repeat_count(track, -1);
    }
    vqeaf_state_refresh();
    update_status();
    vqeaf_ui_notify("Hệ thống", "VQEAF OS đã khởi động trên Redmi Note 7 Pro",
                    VQEAF_SYM_CHECK_CIRCLE, 0x35E0C8);
    if(g_state_timer == NULL)
        g_state_timer = lv_timer_create(state_timer_cb, 1000, NULL);
    vqeaf_ui_show(VQEAF_PAGE_LOCK);
}

void vqeaf_ui_set_theme(vqeaf_theme_mode_t mode)
{
    vqeaf_theme_set(mode);
    /* show() notices g_built_mode changed and rebuilds the chrome before the
     * page, so the whole shell switches palette in one pass. */
    if(g_display != NULL) vqeaf_ui_show(g_page);
}

void vqeaf_ui_show(vqeaf_page_t page)
{
    int i;
    int content_top;
    if(g_content == NULL || page < 0 || page >= VQEAF_PAGE_COUNT) return;

    /* Track recently opened app/detail pages for the task switcher. System
     * shells (lock/control) are not switchable tasks. */
    if(page != VQEAF_PAGE_LOCK && page != VQEAF_PAGE_CONTROL &&
       page != VQEAF_PAGE_RECENTS) {
        for(i = 0; i < g_recent_count; i++)
            if(g_recent_pages[i] == page) break;
        if(i == g_recent_count) {
            if(g_recent_count < 4) g_recent_count++;
            for(i = g_recent_count - 1; i > 0; i--)
                g_recent_pages[i] = g_recent_pages[i - 1];
            g_recent_pages[0] = page;
        }
        else if(i > 0) {
            vqeaf_page_t tmp = g_recent_pages[i];
            for(int j = i; j > 0; j--) g_recent_pages[j] = g_recent_pages[j - 1];
            g_recent_pages[0] = tmp;
        }
    }

    g_page = page;
    g_move_app = -1;
    if(g_built_mode != vqeaf_theme_get() || g_built_seed != vqeaf_theme_seed() ||
       g_built_glass != vqeaf_theme_liquid_glass() ||
       g_built_fluent != vqeaf_theme_fluent()) {
        refresh_chrome();
        g_built_mode = vqeaf_theme_get();
        g_built_seed = vqeaf_theme_seed();
        g_built_glass = vqeaf_theme_liquid_glass();
        g_built_fluent = vqeaf_theme_fluent();
    }
    /* The taskbar owns its own strip under the status bar; page content
     * starts below it whenever the strip is visible. */
    vqeaf_tools_taskbar_refresh(page);
    content_top = VQEAF_STATUS_H +
                  (vqeaf_tools_taskbar_visible(page) ? VQEAF_TASKBAR_H : 0);
    lv_obj_clean(g_content);
    lv_obj_set_style_bg_color(g_content, VQEAF_C_BG, 0);
    lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, 0);

    /* The status bar is a property of the shell, not of a page: every page
     * starts with it visible, and only QEAPP_NATIVE (a full-canvas app frame)
     * hides it again below. Without this, leaving that page for the lock
     * screen left the shell with no clock or battery. */
    lv_obj_clear_flag(g_status, LV_OBJ_FLAG_HIDDEN);

    /* The dock/hotseat is a launcher affordance: it belongs to Home only.
     * System apps, detail pages and running .qeapp apps own the screen like a
     * phone launcher, leaving just the gesture pill for home/recents. */
    int home = (page == VQEAF_PAGE_HOME);
    if(page == VQEAF_PAGE_LOCK) {
        lv_obj_set_pos(g_content, 0, 0);
        lv_obj_set_height(g_content,
                          lv_display_get_vertical_resolution(g_display));
        /* The lock screen is a single canvas, not a scrolled page: without a
         * scroll target the swipe-up gesture is delivered to the shell. */
        lv_obj_remove_flag(g_content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_home_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(g_status, LV_OPA_TRANSP, 0);
    }
    else if(page == VQEAF_PAGE_QEAPP_NATIVE) {
        /* A running native app owns the whole canvas: its frame is drawn for
         * 720x1560 and carries its own top bar, so the dock stays out of the
         * way. The gesture pill remains over the app, exactly like phone
         * gesture navigation, so the user can always return to the launcher.
         * The status bar is decided by the app's own pixels: a phone keeps the
         * clock over an app that leaves the top band blank, and hides it when
         * the app paints there. */
        lv_obj_set_pos(g_content, 0, 0);
        lv_obj_set_height(g_content,
                          lv_display_get_vertical_resolution(g_display));
        lv_obj_remove_flag(g_content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_home_bar, LV_OBJ_FLAG_HIDDEN);
        if(vqeaf_pages_frame_top_band_empty(vqeaf_qeapp_device_frame())) {
            /* Over the app, but light: the frame stays readable under it. */
            lv_obj_set_style_bg_opa(g_status, LV_OPA_20, 0);
            lv_obj_move_foreground(g_status);
            vio_log("ui: qeapp frame leaves the top band blank, status overlaid");
        }
        else {
            lv_obj_add_flag(g_status, LV_OBJ_FLAG_HIDDEN);
            vio_log("ui: qeapp frame paints the top band, status hidden");
        }
    }
    else {
        lv_obj_set_pos(g_content, 0, content_top);
        if(home)
            lv_obj_set_height(g_content, nav_top() - content_top);
        else
            lv_obj_set_height(g_content,
                              lv_display_get_vertical_resolution(g_display) -
                              content_top);
        lv_obj_add_flag(g_content, LV_OBJ_FLAG_SCROLLABLE);
        /* Home keeps the floating dock; every other page (system apps, detail
         * pages) takes the full content area and hides the dock like a phone
         * launcher. The gesture pill stays visible on all of them for
         * home/recents. */
        if(home)
            lv_obj_clear_flag(g_nav, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(g_nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_home_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(g_status, LV_OPA_20, 0);
    }

    vqeaf_pages_build(page, g_content);
    update_navigation();
    update_status();
    lv_obj_scroll_to_y(g_content, 0, LV_ANIM_OFF);
    note_activity();
}

vqeaf_page_t vqeaf_ui_page(void)
{
    return g_page;
}

/* Called only for genuine user input (touch, power/volume keys, gestures). */
void vqeaf_ui_activity(void)
{
    note_activity();
    if(g_sleeping) sleep_exit();
    else if(g_dimmed) {
        g_dimmed = 0;
        vqeaf_state_set_brightness(g_saved_brightness);
    }
}

static lv_obj_t *g_volume_overlay;
static lv_obj_t *g_volume_bar;
static lv_timer_t *g_volume_timer;

static void volume_hide_cb(lv_timer_t *timer)
{
    (void)timer;
    if(g_volume_overlay != NULL) {
        lv_obj_add_flag(g_volume_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void vqeaf_ui_volume_show(void)
{
    lv_display_t *disp = g_display;
    if(disp == NULL) return;
    int w = lv_display_get_horizontal_resolution(disp);
    int h = lv_display_get_vertical_resolution(disp);
    if(g_volume_overlay == NULL) {
        g_volume_overlay = vqeaf_obj(lv_layer_top());
        lv_obj_set_size(g_volume_overlay, w / 2, 120);
        lv_obj_align(g_volume_overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(g_volume_overlay, VQEAF_C_SURFACE_2, 0);
        lv_obj_set_style_bg_opa(g_volume_overlay, LV_OPA_90, 0);
        lv_obj_set_style_radius(g_volume_overlay, 24, 0);
        lv_obj_set_flex_flow(g_volume_overlay, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(g_volume_overlay, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(g_volume_overlay, 10, 0);
        vqeaf_label(g_volume_overlay, "Âm lượng", VQEAF_FONT_CAPTION,
                    VQEAF_C_TEXT);
        g_volume_bar = lv_bar_create(g_volume_overlay);
        lv_obj_set_size(g_volume_bar, w / 2 - 40, 20);
        lv_bar_set_range(g_volume_bar, 0, 100);
        lv_obj_set_style_bg_color(g_volume_bar, VQEAF_C_SURFACE_3, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(g_volume_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(g_volume_bar, VQEAF_C_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(g_volume_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    }
    lv_bar_set_value(g_volume_bar, vqeaf_state.volume, LV_ANIM_OFF);
    lv_obj_clear_flag(g_volume_overlay, LV_OBJ_FLAG_HIDDEN);
    if(g_volume_timer != NULL) lv_timer_delete(g_volume_timer);
    g_volume_timer = lv_timer_create(volume_hide_cb, 2000, NULL);
    if(g_volume_timer != NULL) lv_timer_set_repeat_count(g_volume_timer, 1);
}

void vqeaf_ui_volume_adjust(int delta)
{
    vqeaf_state_set_volume(vqeaf_state.volume + delta);
    vqeaf_ui_volume_show();
    vqeaf_ui_activity();
}

void vqeaf_ui_toggle_screen(void)
{
    /* Manual Sleep / power-key toggle uses exactly the same state machine as
     * the timeout, so the two can never disagree about screen_off. */
    if(g_sleeping) sleep_exit();
    else sleep_enter();
}

/* ---- modal overlays on lv_layer_top() ------------------------------------
 * The power menu and the USB sheet are the same object: a full-screen scrim
 * that closes on a backdrop tap. Both rules that used to be repeated live
 * here instead.
 *
 * Deleting the scrim from inside its own CLICKED handler is what leaked the
 * tap: LVGL returns from the release path with the active object dangling and
 * the next poll hands the event to whatever reuses that memory, so the page
 * underneath changed by itself. A dismissal therefore waits one tick, after
 * the whole contact has been processed; a close that also navigates or shuts
 * the screen down uses overlay_close(), because the page it leaves behind is
 * rebuilt anyway and a sleeping screen stops running the async queue. */
typedef struct {
    lv_obj_t *obj;      /* the scrim, NULL when nothing is open */
    lv_obj_t *queued;   /* instance a queued dismissal belongs to */
} overlay_t;

static void overlay_close(overlay_t *ov)
{
    ov->queued = NULL;
    if(ov->obj == NULL) return;
    lv_obj_delete(ov->obj);
    ov->obj = NULL;
}

static void overlay_dismiss_async(void *arg)
{
    overlay_t *ov = (overlay_t *)arg;

    /* A close() in the meantime already cleared the slot; show() refuses to
     * reopen while one is pending, so there is nothing else to tell apart. */
    if(ov->queued == NULL) return;
    overlay_close(ov);
}

static void overlay_dismiss(overlay_t *ov)
{
    lv_indev_t *indev = lv_indev_active();

    /* One contact = one dismissal: swallow the rest of this press. */
    if(indev != NULL) lv_indev_wait_release(indev);
    if(ov->obj == NULL || ov->queued == ov->obj) return;
    ov->queued = ov->obj;
    lv_async_call(overlay_dismiss_async, ov);
}

/* Callers guarantee a display, no running shutdown overlay and an empty slot,
 * so this has no failure to report; it returns the scrim to parent the card. */
static lv_obj_t *overlay_open(overlay_t *ov, int w, int h,
                              lv_event_cb_t on_backdrop)
{
    lv_obj_t *scrim = vqeaf_obj(lv_layer_top());

    lv_obj_set_size(scrim, w, h);
    lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_50, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scrim, on_backdrop, LV_EVENT_CLICKED, NULL);
    ov->obj = scrim;
    ov->queued = NULL;
    return scrim;
}

static overlay_t g_power_menu;

static void power_menu_backdrop_cb(lv_event_t *event)
{
    (void)event;
    overlay_dismiss(&g_power_menu);
}

/* The menu only requests a sequence. The shutdown module draws the
 * restarting screen and PID 1 performs the actual machine reset. */
static void power_menu_reboot_cb(lv_event_t *e)
{
    (void)e;
    overlay_close(&g_power_menu);
    vqeaf_shutdown_request(VQEAF_SHUTDOWN_RESTART);
}

static void power_menu_poweroff_cb(lv_event_t *e)
{
    (void)e;
    overlay_close(&g_power_menu);
    vqeaf_shutdown_request(VQEAF_SHUTDOWN_POWER_OFF);
}

static void power_menu_sleep_cb(lv_event_t *e)
{
    (void)e;
    overlay_close(&g_power_menu);
    vqeaf_ui_toggle_screen();
}

/* "Khoá" locks without blanking: the backlight stays on so the lock screen
 * (clock, status widget, notifications) is visible, unlike "Ngủ". */
static void power_menu_lock_cb(lv_event_t *e)
{
    (void)e;
    overlay_close(&g_power_menu);
    note_activity();
    vqeaf_ui_show(VQEAF_PAGE_LOCK);
}

void vqeaf_ui_power_menu_show(void)
{
    if(g_power_menu.obj != NULL) {
        overlay_close(&g_power_menu);
        return;
    }
    if(g_display == NULL || vqeaf_shutdown_active()) return;
    int w = lv_display_get_horizontal_resolution(g_display);
    int h = lv_display_get_vertical_resolution(g_display);
    /* One disc size drives the whole panel, so the grid stays square at any
     * canvas width: two discs + the column gap + the padding on both sides. */
    int disc = w * 26 / 100;
    int pad = w * 5 / 100;
    int gap = w * 3 / 100;

    lv_obj_t *scrim = overlay_open(&g_power_menu, w, h,
                                   power_menu_backdrop_cb);

    /* Floating panel: round buttons with the label under each disc and no
     * header row, the choices themselves over the dimmed page. ROW_WRAP lays
     * two discs per line on its own, so there is no nested row container. */
    lv_obj_t *card = vqeaf_obj(scrim);
    lv_obj_set_width(card, 2 * (disc + pad) + gap);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 40, 0);
    lv_obj_set_style_pad_all(card, pad, 0);
    lv_obj_set_style_pad_column(card, gap, 0);
    lv_obj_set_style_pad_row(card, 34, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    vqeaf_m3_elevate(card, 3);

    static const struct {
        const char *label;
        const char *symbol;
        lv_event_cb_t cb;
        int danger;     /* filled error disc, for the destructive action only */
    } items[] = {
        { "Khoá", VQEAF_SYM_LOCK, power_menu_lock_cb, 0 },
        { "Tắt nguồn", VQEAF_SYM_POWER_SETTINGS_NEW, power_menu_poweroff_cb, 1 },
        { "Khởi động lại", VQEAF_SYM_REFRESH, power_menu_reboot_cb, 0 },
        { "Ngủ", VQEAF_SYM_BRIGHTNESS_6, power_menu_sleep_cb, 0 },
    };
    for(int i = 0; i < (int)(sizeof(items) / sizeof(items[0])); i++) {
        lv_color_t fg = items[i].danger ? VQEAF_C_ON_ERROR : VQEAF_C_TEXT;
        lv_obj_t *cell = vqeaf_column(card);
        lv_obj_set_width(cell, disc);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(cell, 14, 0);
        /* The label is not clickable, so a tap on it walks up to this cell. */
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cell, items[i].cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *button = vqeaf_obj(cell);
        lv_obj_set_size(button, disc, disc);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(button, items[i].danger ? VQEAF_C_ERROR
                                                         : VQEAF_C_SURFACE_3, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        /* vqeaf_obj() is a bare lv_obj: the disc needs CLICKABLE for the press
         * that drives its ripple, and that press is the click here. */
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button, items[i].cb, LV_EVENT_CLICKED, NULL);
        lv_obj_center(vqeaf_glyph(button, items[i].symbol, VQEAF_ICON, fg));
        vqeaf_m3_ripple_color(button, fg);

        lv_obj_t *label = vqeaf_label(cell, items[i].label, VQEAF_FONT_BODY,
                                      VQEAF_C_TEXT);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
}

int vqeaf_ui_recent_count(void)
{
    return g_recent_count;
}

void vqeaf_ui_recent_close(vqeaf_page_t page)
{
    int i;
    for(i = 0; i < g_recent_count; i++)
        if(g_recent_pages[i] == page) break;
    if(i >= g_recent_count) return;
    for(; i + 1 < g_recent_count; i++)
        g_recent_pages[i] = g_recent_pages[i + 1];
    g_recent_count--;
    g_recent_pages[g_recent_count] = VQEAF_PAGE_COUNT;
}

void vqeaf_ui_recent_clear(void)
{
    for(int i = 0; i < 4; i++)
        g_recent_pages[i] = VQEAF_PAGE_COUNT;
    g_recent_count = 0;
}

vqeaf_page_t vqeaf_ui_recent_page(int index)
{
    if(index < 0 || index >= g_recent_count) return VQEAF_PAGE_COUNT;
    return g_recent_pages[index];
}

/* ---- "Dung USB cho" (cable just plugged in) -------------------------------
 * Three real choices, no invented ones: charge only (the data function is
 * released), file transfer (the QEAFRESCUE drive appears on the PC) and photo
 * transfer. Which one is possible is decided by the platform; this only
 * reports the pick, so the UI stays free of USB code. */

static overlay_t g_usb_dialog;
static volatile int g_usb_choice = -1;

int vqeaf_ui_usb_dialog_visible(void)
{
    return g_usb_dialog.obj != NULL;
}

int vqeaf_ui_take_usb_choice(void)
{
    int choice = g_usb_choice;

    g_usb_choice = -1;
    return choice;
}

/* Settings shows the mode the platform actually applied. The shell never
 * touches configfs: it reads through this provider and still only reports a
 * pick, so main stays the single writer of the gadget. */
static vqeaf_usb_mode_get_fn g_usb_mode_get;

void vqeaf_ui_set_usb_mode_provider(vqeaf_usb_mode_get_fn get)
{
    g_usb_mode_get = get;
}

int vqeaf_ui_usb_mode(void)
{
    int mode = g_usb_mode_get != NULL ? g_usb_mode_get() : -1;

    /* Only the two modes the sheet offers can be described; anything else is
     * "unknown" rather than a third state the row would have to explain. */
    return (mode == VQEAF_USB_CHARGE || mode == VQEAF_USB_FILE) ? mode : -1;
}

static void usb_dialog_backdrop_cb(lv_event_t *event)
{
    (void)event;
    overlay_dismiss(&g_usb_dialog);
}

static void usb_dialog_choice_cb(lv_event_t *event)
{
    g_usb_choice = (int)(intptr_t)lv_event_get_user_data(event);
    overlay_dismiss(&g_usb_dialog);
}

void vqeaf_ui_usb_dialog_hide(void)
{
    g_usb_choice = -1;
    overlay_close(&g_usb_dialog);
}

void vqeaf_ui_usb_dialog_show(void)
{
    int w, h, pad, disc;
    lv_obj_t *card, *note;
    static const struct {
        const char *title;
        const char *hint;
        const char *symbol;
        int choice;
    } rows[] = {
        { "Sạc thiết bị", "Chỉ nạp pin, máy không hiện trên máy tính",
          VQEAF_SYM_BATTERY_CHARGING_FULL, VQEAF_USB_CHARGE },
        { "Truyền tệp", "Hiện ổ QEAFRESCUE để chép tệp",
          VQEAF_SYM_FOLDER, VQEAF_USB_FILE },
    };

    /* A second plug event while the sheet is up is not a second sheet. */
    if(g_usb_dialog.obj != NULL) return;
    if(g_display == NULL || vqeaf_shutdown_active()) return;

    w = lv_display_get_horizontal_resolution(g_display);
    h = lv_display_get_vertical_resolution(g_display);
    pad = w * 5 / 100;
    disc = w * 11 / 100;

    card = vqeaf_obj(overlay_open(&g_usb_dialog, w, h,
                                  usb_dialog_backdrop_cb));
    lv_obj_set_width(card, w * 88 / 100);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE_CONTAINER_HIGH, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 32, 0);
    lv_obj_set_style_pad_all(card, pad, 0);
    lv_obj_set_style_pad_row(card, pad / 2, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    vqeaf_m3_elevate(card, 3);

    lv_obj_set_width(vqeaf_label(card, "Dùng USB cho", VQEAF_FONT_TITLE,
                                 VQEAF_C_TEXT), lv_pct(100));
    lv_obj_set_width(vqeaf_label(card,
                                 "Chọn cách máy tính nhìn thấy máy này",
                                 VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM),
                     lv_pct(100));

    for(int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); i++) {
        lv_obj_t *row = vqeaf_row(card);
        lv_obj_t *disc_obj, *text;

        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_pad_all(row, pad / 2, 0);
        lv_obj_set_style_pad_column(row, pad / 2, 0);
        /* The labels are not clickable, so a tap on either of them walks up
         * to this row - the whole line is the target. */
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, usb_dialog_choice_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)rows[i].choice);

        disc_obj = vqeaf_obj(row);
        lv_obj_set_size(disc_obj, disc, disc);
        lv_obj_set_style_radius(disc_obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(disc_obj, VQEAF_C_SURFACE_3, 0);
        lv_obj_set_style_bg_opa(disc_obj, LV_OPA_COVER, 0);
        vqeaf_m3_ripple_color(disc_obj, VQEAF_C_ACCENT);
        lv_obj_center(vqeaf_glyph(disc_obj, rows[i].symbol, VQEAF_ICON,
                                  VQEAF_C_TEXT));

        text = vqeaf_column(row);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_set_style_pad_row(text, 4, 0);
        lv_obj_set_width(vqeaf_label(text, rows[i].title, VQEAF_FONT_BODY_BOLD,
                                     VQEAF_C_TEXT), lv_pct(100));
        lv_obj_set_width(vqeaf_label(text, rows[i].hint, VQEAF_FONT_BODY,
                                     VQEAF_C_TEXT_DIM), lv_pct(100));
    }

    /* No photo row: this kernel ships no PTP responder, so picking it could
     * only fall back to the drive. Say that once instead of offering a choice
     * that never happens. */
    note = vqeaf_label(card,
                       "Truyền ảnh chưa hỗ trợ: kernel không có PTP responder.",
                       VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
}
