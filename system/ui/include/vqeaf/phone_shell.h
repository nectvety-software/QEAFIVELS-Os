/* Smartphone shell: lock screen, home grid, quick settings and app screens.
 * Pure state machine + renderer so the whole UI can be exercised on the host.
 * The kernel owns every real value (clock, storage, power state); this file
 * only draws what it is given and never invents battery, WiFi or time. */
#ifndef VQEAF_PHONE_SHELL_H
#define VQEAF_PHONE_SHELL_H
#include "gfx.h"

typedef enum {
    VQEAF_PHONE_LOCK = 0,
    VQEAF_PHONE_HOME,
    VQEAF_PHONE_APP,
    VQEAF_PHONE_QUICK
} VqeafPhoneScreen;

typedef enum {
    VQEAF_APP_ABOUT = 0,
    VQEAF_APP_SETTINGS,
    VQEAF_APP_STORAGE,
    VQEAF_APP_UPDATE,
    VQEAF_APP_POWER,
    VQEAF_APP_FASTBOOT,
    VQEAF_APP_CLOCK,
    VQEAF_APP_LOCK,
    VQEAF_APP_COUNT
} VqeafPhoneApp;

typedef enum {
    VQEAF_PHONE_KEY_NONE = 0,
    VQEAF_PHONE_KEY_UP,
    VQEAF_PHONE_KEY_DOWN,
    VQEAF_PHONE_KEY_LEFT,
    VQEAF_PHONE_KEY_RIGHT,
    VQEAF_PHONE_KEY_ENTER,
    VQEAF_PHONE_KEY_BACK,
    VQEAF_PHONE_KEY_QUICK,
    VQEAF_PHONE_KEY_LOCK
} VqeafPhoneKey;

/* Actions the shell cannot perform itself: the kernel applies them to the
 * power manager, the power menu or the full-screen views. */
typedef enum {
    VQEAF_PHONE_ACTION_NONE = 0,
    VQEAF_PHONE_ACTION_TIMEOUT_NEXT,
    VQEAF_PHONE_ACTION_WAKELOCK_TOGGLE,
    VQEAF_PHONE_ACTION_POWER_MENU,
    VQEAF_PHONE_ACTION_SLEEP,
    VQEAF_PHONE_ACTION_FASTBOOT,
    VQEAF_PHONE_ACTION_UPDATE,
    VQEAF_PHONE_ACTION_UPDATE_CLOSE,
    VQEAF_PHONE_ACTION_LOCK,
    VQEAF_PHONE_ACTION_POWER_OFF,
    VQEAF_PHONE_ACTION_REBOOT
} VqeafPhoneAction;

typedef struct {
    VqeafPhoneScreen screen;
    VqeafPhoneApp app;
    uint32_t sel;
    uint32_t cols;
    VqeafPhoneScreen previous;
    uint32_t previous_sel;
} VqeafPhoneState;

typedef struct {
    int clock_valid;
    uint32_t year, month, day, hour, minute, second;
    uint32_t timeout_index;   /* VqeafScreenTimeout 0..4 */
    int keep_awake;
    int battery_present;
    uint32_t battery_permille;
    int charging;
    int storage_mounted;
    uint64_t storage_sectors;
    const char *storage_path;
    const char *storage_file;
    const char *storage_summary;
    uint64_t uptime_ticks;
    uint64_t free_pages;
    uint32_t task_count;
    uint32_t fb_width, fb_height;
} VqeafPhoneModel;

void vqeaf_phone_init(VqeafPhoneState *st, uint32_t cols);
/* Returns the follow-up action; screen transitions are applied in place. */
VqeafPhoneAction vqeaf_phone_key(VqeafPhoneState *st, VqeafPhoneKey key);
/* Touch hit-test in canvas pixels. Maps a tap to the same selection and
 * activation the keys produce (grid cell, dock slot, menu row, back arrow).
 * Returns the follow-up action; screen transitions are applied in place. */
VqeafPhoneAction vqeaf_phone_tap(VqeafPhoneState *st, uint32_t fb_w, uint32_t fb_h,
                                 uint32_t x, uint32_t y);
void vqeaf_phone_render(const VqeafFramebuffer *fb, const VqeafPhoneState *st,
                        const VqeafPhoneModel *model);
uint32_t vqeaf_phone_app_count(void);
const char *vqeaf_phone_app_label(uint32_t index);
const char *vqeaf_phone_timeout_label(uint32_t index);
#endif
