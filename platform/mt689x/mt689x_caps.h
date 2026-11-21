/* SPDX-License-Identifier: MIT */
/* platform/mt689x/mt689x_caps.h - hardware census for MT6891/MT6893.
 *
 * Same capability names as the violet port, so the QEAF UI and the rescue
 * `caps` verb work unchanged. The probe paths are MediaTek's: UFS via
 * ufshcd, the Mali GPU via a DRM render node, connectivity firmware under
 * /vendor/firmware/mediatek/mt6893. A capability is READY only when the
 * device node that proves it exists.
 */
#ifndef QEAF_MT689X_CAPS_H
#define QEAF_MT689X_CAPS_H

#include <stdint.h>
#include "vqeaf/capability.h"

typedef struct {
    int ufs_present;        /* ufshcd bound and a SCSI disk behind it */
    int usb_udc_present;    /* /sys/class/udc has a controller */
    int usb_host_present;   /* /sys/bus/usb/devices has a device */
    int drm_card_present;   /* /dev/dri/card0 */
    int drm_render_present; /* /dev/dri/renderD128 (Mali via Panfrost) */
    int mali_node_present;  /* /dev/mali0 (vendor Mali kbase node) */
    int panel_max_refresh;  /* highest Hz from the connector's mode list */
    int touch_present;      /* Novatek NVT SPI touch evdev */
    int wlan_iface_present; /* /sys/class/net/wlan0 */
    int wlan_fw_present;    /* mediatek/mt6893 WIFI_RAM_CODE present */
    int bt_hci_present;     /* /sys/class/bluetooth/hci0 */
    int bt_fw_present;      /* BT_RAM_CODE / rom patch present */
    int sound_cards;        /* usable cards in /proc/asound/cards */
    int accel_present;      /* IIO accelerometer (SCP sensor hub) */
    int kvm_present;        /* /dev/kvm */
} MtkHwProbe;

void mtk_caps_publish(const MtkHwProbe *p);
int mtk_caps_report(char *buf, int cap);
int mtk_caps_probe_device(MtkHwProbe *out);

/* Reads the board identity from the device tree ("mediatek,MT6893") and
 * distinguishes chopin (MT6891/MT6893 phone) from ares. Returns a static
 * string; never claims a model the DT does not name. */
const char *mtk_board_name(void);

#endif /* QEAF_MT689X_CAPS_H */
