/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_state.h - what the shell is allowed to display.
 *
 * The shell renders only state that exists on this device. Anything the
 * kernel does not expose (Wi-Fi hardware, signal strength, weather, a real
 * time before the clock is set) stays unknown and is drawn as such instead
 * of being invented.
 */
#ifndef VQEAF_STATE_H
#define VQEAF_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Wall clock (CLOCK_REALTIME). time_valid is 0 until the RTC/network
     * gives a plausible date, in which case the shell shows --:--. */
    int time_valid;
    int hour;
    int minute;
    char clock_text[8];
    char date_text[64];

    /* Battery, from /sys/class/power_supply. */
    int battery_valid;
    int battery_pct;
    int charging;

    /* Wi-Fi: -1 unknown, 0 down, 1 up. Read from
     * /sys/class/net/<wlan>/operstate; no invented signal bars. */
    int wifi_up;
    int wifi_if_present;
    char wifi_ifname[16];
    char wifi_mac[18];
    int wifi_link;              /* /proc/net/wireless link quality, 0..100 */
    int wifi_driver_loaded;     /* /sys/module/wlan, /sys/module/cfg80211 */
    int wifi_firmware_present;  /* WCN3990-style blobs in a real fw dir */
    char wifi_firmware_path[64];

    /* Qualcomm Bluetooth transport: -1 unknown, 0 down, 1 up. */
    int bt_up;
    int bt_if_present;
    char bt_ifname[16];
    int bt_driver_loaded;       /* stock stack: hci_smd/hci_uart + power control */
    int bt_firmware_present;    /* dedicated /vendor/bt_firmware or known firmware */
    char bt_firmware_path[64];

    /* Driver/device probes shown on the Drivers page. */
    int drm_present;            /* /dev/dri/card0 */
    int touch_events;           /* /dev/input/event* count */
    int usb_udc_present;        /* /sys/class/udc has a controller */
    int backlight_present;      /* /sys/class/backlight */
    int usb_lun_present;        /* /run/qeaf-lun.img */
    uint64_t usb_lun_size;      /* bytes */

    /* Root filesystem usage, from statvfs("/"), in bytes. */
    int storage_valid;
    int storage_pct;
    uint64_t storage_total;
    uint64_t storage_free;

    /* Physical memory, from /proc/meminfo, in kB. */
    int mem_valid;
    uint32_t mem_total_kb;
    uint32_t mem_available_kb;

    uint64_t uptime_s;

    /* Panel backlight, 0..100, the value last written to sysfs. */
    int brightness;

    /* Volume level, 0..100 (no audio backend yet). */
    int volume;

    /* Screen timeout: 0 never, otherwise seconds of inactivity. */
    int screen_timeout_s;
    int screen_off;
} vqeaf_state_t;

extern vqeaf_state_t vqeaf_state;

/* Re-reads every provider. Safe to call once a second; all reads are
 * best-effort and leave the previous value in place on failure. */
void vqeaf_state_refresh(void);

/* Writes the backlight and remembers the value. Returns 0 on success. */
int vqeaf_state_set_brightness(int percent);
int vqeaf_state_set_volume(int percent);
int vqeaf_state_set_screen_timeout(int seconds);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_STATE_H */
