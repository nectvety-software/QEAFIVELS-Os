/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_ui.h - the VQEAF phone shell.
 *
 * The shell owns one LVGL screen with a persistent status bar, a scrollable
 * page area and a bottom navigation bar. The lock screen hides the nav bar and
 * lets the wallpaper run under the status bar, matching the reference design.
 */
#ifndef VQEAF_UI_H
#define VQEAF_UI_H

#include "lvgl.h"
#include "vqeaf_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VQEAF_PAGE_LOCK = 0,
    VQEAF_PAGE_HOME,
    VQEAF_PAGE_APPS,
    VQEAF_PAGE_CONTROL,
    VQEAF_PAGE_SETTINGS,
    /* Detail pages reached from Settings; they are not nav-bar targets. */
    VQEAF_PAGE_STORAGE,
    VQEAF_PAGE_ABOUT,
    VQEAF_PAGE_KEYBOARD,
    VQEAF_PAGE_KEYBOARD_THEMES,
    VQEAF_PAGE_WIFI,
    VQEAF_PAGE_DRIVERS,
    VQEAF_PAGE_SYNC,
    VQEAF_PAGE_LOCK_THEME,
    VQEAF_PAGE_FILES,
    VQEAF_PAGE_INSTALLER,
    VQEAF_PAGE_QEAPP,
    VQEAF_PAGE_QEAPP_NATIVE,
    VQEAF_PAGE_BROWSER,
    VQEAF_PAGE_NOTES,
    VQEAF_PAGE_RECENTS,
    VQEAF_PAGE_MUSIC,
    VQEAF_PAGE_VIDEO,
    VQEAF_PAGE_GALLERY,
    /* Reference layout for the Material 3 component set: app bar, cards by
     * elevation, FAB and dialog. Appended last so the existing page numbers
     * (app registry, recents, rescue verbs) keep their values. */
    VQEAF_PAGE_M3,
    /* Launcher edit mode (wallpaper / widgets / done), opened from Home and
     * the app drawer. Appended so existing page numbers stay stable. */
    VQEAF_PAGE_APPS_EDIT,
    /* Native QEAPP catalogue and installed-app management. */
    VQEAF_PAGE_QEAPP_STORE,
    VQEAF_PAGE_SECURITY,
    VQEAF_PAGE_PIN,
    VQEAF_PAGE_KEYBOARD_FEEDBACK,
    VQEAF_PAGE_DEFAULT_APPS,
    VQEAF_PAGE_TERMINAL,
    /* Desktop-style utilities. Appended so every existing page number and
     * rescue/app-registry contract keeps its value. */
    VQEAF_PAGE_NTFS_SEARCH,
    VQEAF_PAGE_TASK_MANAGER,
    VQEAF_PAGE_DISK_CLEANUP,
    VQEAF_PAGE_COUNT
} vqeaf_page_t;

/* Builds the shell on the already configured LVGL display. */
void vqeaf_ui_create(lv_display_t *display);

/* Switches pages and rebuilds the page content. */
void vqeaf_ui_show(vqeaf_page_t page);

/* Runs an installed native app by slot; used by the queued rescue launch. */
void vqeaf_ui_run_app(int index);

/* File picker for installing a .qeapp from device storage (any folder of the
 * OS storage tree, not just the rescue root). */
void vqeaf_ui_pick_qeapp(void);

vqeaf_page_t vqeaf_ui_page(void);
void vqeaf_ui_activity(void);
void vqeaf_ui_volume_adjust(int delta);
void vqeaf_ui_volume_show(void);
void vqeaf_ui_power_menu_show(void);

/* ---- "Dùng USB cho" sheet (cable plugged) ------------------------------- */
/* Which row the user picked; the platform maps them onto the gadget modes.
 * CHARGE releases the data function, FILE serves the QEAFRESCUE drive. There is
 * no photo entry: this kernel has no PTP responder, so the sheet states that
 * instead of offering a third pick that could only fall back. */
#define VQEAF_USB_CHARGE 0
#define VQEAF_USB_FILE   1

void vqeaf_ui_usb_dialog_show(void);
void vqeaf_ui_usb_dialog_hide(void);
int vqeaf_ui_usb_dialog_visible(void);
/* The pick made since the last call, or -1 when the sheet was dismissed. */
int vqeaf_ui_take_usb_choice(void);
/* Current gadget mode, for the Settings row: VQEAF_USB_* as the platform
 * applied it, or -1 when this device has no USB backend. The shell only
 * displays it; the sheet still reports a pick that the platform applies. */
typedef int (*vqeaf_usb_mode_get_fn)(void);
void vqeaf_ui_set_usb_mode_provider(vqeaf_usb_mode_get_fn get);
int vqeaf_ui_usb_mode(void);
void vqeaf_ui_toggle_screen(void);
/* Rebuilds the shell in the chosen palette, staying on the current page. */
void vqeaf_ui_set_theme(vqeaf_theme_mode_t mode);
/* 1 while the display is off and the shell is in low-power sleep. PID 1 uses
 * this to pause LVGL refreshes; any real input wakes it through
 * vqeaf_ui_activity(). */
int vqeaf_ui_sleeping(void);

/* Recently opened top-level pages, newest first. Used by the task switcher. */
int vqeaf_ui_recent_count(void);
vqeaf_page_t vqeaf_ui_recent_page(int index);
/* Close one task (or the whole list) from the switcher. RAM only, like the
 * list itself: a reboot starts empty again. */
void vqeaf_ui_recent_close(vqeaf_page_t page);
void vqeaf_ui_recent_clear(void);

/* Dock move mode, armed from the app info sheet's "Di chuyển" button. The
 * next dock tap drops the app into that slot; navigating away cancels. */
void vqeaf_ui_dock_move_arm(int app_id);

/* ---- in-RAM notification log -------------------------------------------- */
/* The shell has no notification daemon, so the control centre lists the real
 * events this session produced: boot, provider changes, app install/remove
 * and rescue snapshots. Each entry carries the uptime at which it happened;
 * nothing is seeded with a fake message. Newest first. */
typedef struct {
    char title[64];
    char body[112];
    char symbol[8];       /* UTF-8 Material glyph, copied */
    uint32_t color;
    uint64_t uptime_s;
} vqeaf_notification_t;

/* Opens a real image from UserData/Pictures in the full-screen preview. */
void vqeaf_ui_open_picture(const char *name);
/* Floating bottom-left thumbnail shown right after a real capture. */
void vqeaf_ui_show_capture_thumb(const char *name);

void vqeaf_ui_notify(const char *title, const char *body, const char *symbol,
                     uint32_t color);
int vqeaf_ui_notification_count(void);
const vqeaf_notification_t *vqeaf_ui_notification_at(int index);
void vqeaf_ui_notification_clear(void);

/* Display auto-rotate is a real display_hal flag on the device. The host
 * render has no panel, so it reports off and ignores the setter. */
int vqeaf_ui_auto_rotate(void);
void vqeaf_ui_set_auto_rotate(int on);

/* Chrome visibility, reflecting the phone navigation policy: the
 * dock/hotseat shows on Home only, while the gesture pill stays on every
 * non-lock page (including a running native app) for home/recents. */
int vqeaf_ui_dock_visible(void);
int vqeaf_ui_gesture_pill_visible(void);

/* Three-button navigation bar (Back / Home / Recents). It
 * replaces the gesture pill when enabled; state is session-only on violet. */
void vqeaf_ui_set_virtual_keys(int on);
int vqeaf_ui_virtual_keys(void);

/* QEAFRESCUE file sync: main owns the volume lock, so the UI asks through a
 * hook instead of touching the FAT16 image from its own thread. */
typedef struct {
    char name[64];
    uint32_t size;
    uint8_t is_dir;
} vqeaf_sync_entry_t;

typedef int (*vqeaf_sync_list_fn)(const char *path, vqeaf_sync_entry_t *out,
                                  int max);
typedef int (*vqeaf_sync_read_fn)(const char *path, uint8_t **out,
                                  size_t *size);

void vqeaf_ui_set_sync_list(vqeaf_sync_list_fn fn);
/* Lists a rescue-volume directory; "/" is the root. */
int vqeaf_ui_sync_list(const char *path, vqeaf_sync_entry_t *out, int max);
void vqeaf_ui_set_sync_read(vqeaf_sync_read_fn fn);
/* Allocates *out with malloc; the caller frees it. `path` is absolute within
 * the rescue volume, e.g. "/Apps/hello.qeapp". */
int vqeaf_ui_read_file(const char *path, uint8_t **out, size_t *size);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_UI_H */
