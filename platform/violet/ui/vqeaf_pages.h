/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_pages.h - page builders used by the shell. */
#ifndef VQEAF_PAGES_H
#define VQEAF_PAGES_H

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#include "vqeaf_ui.h"

/* Fills `content` with the widgets for `page`. The shell owns the status and
 * navigation bars; a page only builds the middle of the screen. */
void vqeaf_pages_build(vqeaf_page_t page, lv_obj_t *content);
void vqeaf_pages_refresh(void);

/* ---- lock-screen security ------------------------------------------------- */
/* Honours the configured PIN: with no PIN set this opens Home directly, with
 * a PIN it opens the PIN entry page. Both the swipe-up gesture and the tap
 * affordance on the lock screen call this, so a PIN cannot be bypassed. */
void vqeaf_lock_unlock(void);
int  vqeaf_lock_pin_enabled(void);

/* ---- Default apps -------------------------------------------------------- */
/* A real router for generic "open" actions: each role maps to a stored app
 * (built-in page or installed QEAPP) in the Settings service. On violet the
 * store is session-only, so a reboot restores the built-in fallback. */
typedef enum {
    VQEAF_DEFAULT_BROWSER = 0,
    VQEAF_DEFAULT_MUSIC,
    VQEAF_DEFAULT_GALLERY,
    VQEAF_DEFAULT_FILES,
    VQEAF_DEFAULT_HOME,
    VQEAF_DEFAULT_COUNT
} vqeaf_default_role_t;

const char *vqeaf_default_role_name(int role);
/* Stored selector: "app:<registry id>" or "qeapp:<installed index>". */
const char *vqeaf_default_selector(int role);
int  vqeaf_defaults_set(int role, const char *selector);
/* Opens the app chosen for the role. Returns 1 when a real page/app opened. */
int  vqeaf_defaults_open(int role);

/* System Terminal: execute one command line against real framework state and
 * redraw the console. Exposed to QEAFRESCUE as `term <command>` for testing. */
void vqeaf_terminal_run(const char *line);

/* 1 when the canvas passed in leaves the shell's top status-bar band blank
 * (every pixel there is the same colour), so the shell may overlay the clock
 * and battery on the app's own pixels like a phone does. 0 when the app paints
 * in that band, and 0 for a NULL canvas. */
int vqeaf_pages_frame_top_band_empty(const uint32_t *frame);

/* ---- applied keyboard theme --------------------------------------------- */
/* The Chủ đề bàn phím page previews a theme; "Áp dụng" commits it to the
 * shell's real LVGL keyboards (search, Notes, Wi-Fi password). The choice is
 * session-only: the initramfs root has no persistent prefs partition. */
int vqeaf_pages_kb_theme_count(void);
int vqeaf_pages_kb_theme_applied(void);
uint32_t vqeaf_pages_kb_theme_key_color(int index);
/* Styles a keyboard with the applied theme and tracks it so a later Apply
 * also restyles keyboards that are already on screen. */
void vqeaf_pages_kb_theme_style(lv_obj_t *keyboard);

/* ---- music player view --------------------------------------------------- */
/* Session view of the Music app: 0 player, 1 canvas (lyric overlay), 2 queue.
 * Exposed so intents/deep-links and the render harness can open a specific
 * surface; the on-screen buttons drive the same state. */
void vqeaf_pages_music_view_set(int view);
int vqeaf_pages_music_view(void);

/* ---- app registry and the bottom dock ----------------------------------- */

/* One launchable entry. The first four entries are the system apps that make
 * up the default dock; the rest is the app drawer. `icon` indexes the
 * VPEPixel bitmap set (vqeaf_app_icons.h); `symbol` stays as the fallback. */
typedef struct {
    const char *symbol;
    const char *name;
    uint32_t bg;
    uint32_t fg;
    vqeaf_page_t page;
    int icon;
} vqeaf_app_t;

#define VQEAF_DOCK_SLOTS 4

const vqeaf_app_t *vqeaf_app_by_id(int id);
int vqeaf_app_count(void);

/* App drawer paging. The drawer shows VQEAF_APPS_PER_PAGE icons at a time;
 * dots and horizontal swipes move between pages. State is RAM-only. */
#define VQEAF_APPS_PER_PAGE 8
int vqeaf_pages_apps_page(void);
int vqeaf_pages_apps_page_count(void);
void vqeaf_pages_apps_page_set(int page);

/* Long-press app info sheet, shared by the page grids and the shell dock. */
void vqeaf_pages_app_info(int app_id);
int vqeaf_pages_app_info_visible(void);

/* Dock model: fixed four slots, -1 for an empty slot. The state lives in RAM
 * (the initramfs root is not persistent), so a reboot restores the defaults.
 * Rearrangement goes through the info sheet's "Di chuyển" move mode. */
int vqeaf_dock_slot(int slot);
void vqeaf_dock_move(int from, int to);
void vqeaf_dock_drop(int app_id, int slot);
void vqeaf_dock_clear(int slot);

#endif /* VQEAF_PAGES_H */
