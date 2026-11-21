#ifndef VIO_CAPS_H
#define VIO_CAPS_H
#include <stdint.h>
#include "vqeaf/capability.h"

/* Read-only hardware census for the violet interim transport (vendor Linux
 * kernel). The publish/report logic is pure and host-testable; the device
 * probe reads real sysfs/dev nodes. A capability is only READY when a real
 * probe signal is present, never assumed. NFC/camera/baseband are out of
 * scope and published UNSUPPORTED. */
typedef struct {
    int mmcblk_present;     /* /sys/block/mmcblk0 (violet storage is eMMC) */
    int ufs_present;        /* UFS controller / SCSI disk present */
    int usb_udc_present;    /* /sys/class/udc has a device (gadget) */
    int usb_host_present;   /* /sys/bus/usb/devices has devices (host) */
    int drm_card_present;   /* /dev/dri/card0 */
    int drm_render_present; /* /dev/dri/renderD128 (GPU render node) */
    int panel_max_refresh;  /* highest Hz parsed from connector modes */
    int touch_present;      /* evdev touch controller node */
    int wlan_iface_present; /* /sys/class/net/wlan0 */
    int wlan_fw_present;    /* WCN3990 wlan firmware blobs present */
    int bt_hci_present;     /* /sys/class/bluetooth has hciN */
    int bt_fw_present;      /* populated /vendor/bt_firmware mount or known blob */
    int sound_cards;        /* /proc/asound/cards usable card count */
    int kgsl_present;       /* /dev/kgsl-3d0 (Adreno GPU) */
    int accel_present;      /* IIO accelerometer (auto-rotate source) */
    int kvm_present;        /* /dev/kvm */
    int v4l2_video_nodes;   /* /sys/class/video4linux/videoN count */
    int v4l2_capture_nodes; /* videoN nodes that are stream candidates */
    int fp_node_present;    /* goodix_fp / fpc / fingerprint device node */
    int bt_dev_present;     /* /sys/class/bt-dev has a controller */
} VioHwProbe;

/* Publish capability states from a probe. Pure: no I/O. */
void vio_caps_publish(const VioHwProbe *p);
/* Render the published census as text. Returns bytes written. */
int vio_caps_report(char *buf, int cap);
/* Fill a probe from real device sysfs/dev nodes (read-only). Returns 0 on
 * success; on non-Linux hosts returns -1 and zeroes the probe. */
int vio_caps_probe_device(VioHwProbe *out);

#endif
