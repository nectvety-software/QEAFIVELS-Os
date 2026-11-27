#include "violet_drivers.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;
static VioDriverEvidence current_evidence = {
    VIO_DRIVERS_ABI, sizeof(VioDriverEvidence)
};
static VioDriverStatus current_status[VIO_DRIVERS_MAX];
static size_t current_count;
static int probe_ok;

static const char *state_name(VqeafCapabilityState state)
{
    switch(state) {
    case VQEAF_CAP_UNSUPPORTED: return "UNSUPPORTED";
    case VQEAF_CAP_PRESENT: return "PRESENT";
    case VQEAF_CAP_READY: return "READY";
    case VQEAF_CAP_DEGRADED: return "DEGRADED";
    case VQEAF_CAP_BLOCKED_FIRMWARE: return "BLOCKED_FIRMWARE";
    case VQEAF_CAP_ERROR: return "ERROR";
    }
    return "?";
}

static int evidence_valid(const VioDriverEvidence *evidence)
{
    return evidence != NULL && evidence->abi == VIO_DRIVERS_ABI &&
           evidence->size == sizeof(*evidence);
}

void vio_drivers_evidence_init(VioDriverEvidence *evidence)
{
    if(evidence == NULL) return;
    memset(evidence, 0, sizeof(*evidence));
    evidence->abi = VIO_DRIVERS_ABI;
    evidence->size = sizeof(*evidence);
}

int vio_drivers_set_evidence(const VioDriverEvidence *evidence)
{
    if(!evidence_valid(evidence)) return -1;
    pthread_mutex_lock(&registry_lock);
    current_evidence = *evidence;
    pthread_mutex_unlock(&registry_lock);
    return 0;
}

void vio_drivers_mark_camera_ready(int ready)
{
    pthread_mutex_lock(&registry_lock);
    current_evidence.camera_frame_ok = ready != 0;
    pthread_mutex_unlock(&registry_lock);
}

static VioDriverStatus status(const char *name, const char *interface,
                              VqeafCapabilityState state, int32_t detail,
                              const char *reason)
{
    VioDriverStatus item;
    item.name = name;
    item.linux_interface = interface;
    item.state = state;
    item.detail = detail;
    item.reason = reason;
    return item;
}

int vio_drivers_collect(const VioHwProbe *probe,
                        const VioDriverEvidence *evidence,
                        VioDriverStatus *out, size_t cap)
{
    VioDriverEvidence empty;
    size_t count = 0;

    if(probe == NULL || out == NULL) return -1;
    if(evidence == NULL) {
        vio_drivers_evidence_init(&empty);
        evidence = &empty;
    }
    if(!evidence_valid(evidence)) return -1;

#define ADD(item) do { \
        if(count >= VIO_DRIVERS_MAX) return -1; \
        if(count < cap) out[count] = (item); \
        count++; \
    } while(0)
    ADD(status("storage.emmc", "/sys/block/mmcblk0",
               probe->mmcblk_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
               probe->mmcblk_present,
               probe->mmcblk_present ? "eMMC block device present" :
                                       "no eMMC block device"));
    ADD(status("storage.ufs", "ufshcd / SCSI",
               probe->ufs_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
               probe->ufs_present,
               probe->ufs_present ? "UFS provider present" :
                                    "no UFS provider"));
    ADD(status("display.drm", "DRM/KMS /dev/dri/card0",
               probe->drm_card_present ?
                   (evidence->drm_modeset_ok ? VQEAF_CAP_READY : VQEAF_CAP_PRESENT) :
                   VQEAF_CAP_UNSUPPORTED,
               probe->drm_card_present,
               probe->drm_card_present ?
                   (evidence->drm_modeset_ok ? "modeset and scanout proved" :
                                               "DRM node present; modeset not proved") :
                   "no DRM card node"));
    ADD(status("display.refresh_high", "DRM connector modes",
               probe->panel_max_refresh > 60 ?
                   (evidence->drm_modeset_ok ? VQEAF_CAP_READY : VQEAF_CAP_PRESENT) :
                   VQEAF_CAP_UNSUPPORTED,
               probe->panel_max_refresh,
               probe->panel_max_refresh > 60 ? "high-refresh mode advertised" :
                                               "no mode above 60 Hz"));
    ADD(status("input.touch", "evdev ABS_MT",
               probe->touch_present ?
                   (evidence->touch_open ? VQEAF_CAP_READY : VQEAF_CAP_PRESENT) :
                   VQEAF_CAP_UNSUPPORTED,
               probe->touch_present,
               probe->touch_present ?
                   (evidence->touch_open ? "touch evdev opened and bound" :
                                           "touch node present; evdev not opened") :
                   "no touch evdev node"));
    ADD(status("input.power_keys", "evdev KEY_POWER/VOLUME",
               evidence->power_input_open ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
               evidence->power_input_open,
               evidence->power_input_open ? "power-key evdev opened" :
                                            "no power-key evdev opened"));
    ADD(status("usb.gadget", "configfs + UDC",
               probe->usb_udc_present ?
                   (evidence->usb_gadget_bound ? VQEAF_CAP_READY : VQEAF_CAP_PRESENT) :
                   VQEAF_CAP_UNSUPPORTED,
               probe->usb_udc_present,
               probe->usb_udc_present ?
                   (evidence->usb_gadget_bound ? "gadget function bound" :
                                                 "UDC present; gadget not bound") :
                   "no USB UDC"));
    ADD(status("usb.host", "/sys/bus/usb/devices",
               probe->usb_host_present ? VQEAF_CAP_PRESENT : VQEAF_CAP_UNSUPPORTED,
               probe->usb_host_present,
               probe->usb_host_present ? "USB host devices present" :
                                         "no USB host devices"));
    ADD(status("wifi", "cfg80211/netdev + WLFW",
               probe->wlan_iface_present ? VQEAF_CAP_READY :
                   (probe->wlan_fw_present ? VQEAF_CAP_PRESENT :
                                             VQEAF_CAP_BLOCKED_FIRMWARE),
               probe->wlan_iface_present,
               probe->wlan_iface_present ? "wlan0 present" :
                   (probe->wlan_fw_present ? "firmware present; wlan0 absent" :
                                             "WLFW service/netdev unavailable")));
    ADD(status("bluetooth", "HCI + SLIMbus",
               probe->bt_hci_present ? VQEAF_CAP_READY :
                   (probe->bt_fw_present ? VQEAF_CAP_PRESENT :
                                           VQEAF_CAP_BLOCKED_FIRMWARE),
               probe->bt_hci_present,
               probe->bt_hci_present ? "HCI controller present" :
                   (probe->bt_fw_present ? "firmware present; HCI absent" :
                                           "HCI transport/firmware unavailable")));
    ADD(status("audio", "ALSA /proc/asound/cards",
               probe->sound_cards > 0 ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
               probe->sound_cards,
               probe->sound_cards > 0 ? "sound card registered" :
                                        "no sound card registered"));
    ADD(status("gpu", "KGSL / DRM render node",
               (probe->kgsl_present || probe->drm_render_present) ?
                   VQEAF_CAP_PRESENT : VQEAF_CAP_UNSUPPORTED,
               probe->kgsl_present || probe->drm_render_present,
               (probe->kgsl_present || probe->drm_render_present) ?
                   "GPU node present; render test required" :
                   "no GPU device node"));
    ADD(status("sensor.accel", "IIO device",
               probe->accel_present ? VQEAF_CAP_PRESENT : VQEAF_CAP_UNSUPPORTED,
               probe->accel_present,
               probe->accel_present ? "accelerometer node present; sample required" :
                                      "no accelerometer IIO node"));
    ADD(status("camera", "V4L2 capture",
               evidence->camera_frame_ok && probe->v4l2_capture_nodes > 0 ?
                   VQEAF_CAP_READY :
                   (probe->v4l2_capture_nodes > 0 ? VQEAF_CAP_PRESENT :
                    (probe->v4l2_video_nodes > 0 ? VQEAF_CAP_BLOCKED_FIRMWARE :
                                                  VQEAF_CAP_UNSUPPORTED)),
               probe->v4l2_capture_nodes,
               evidence->camera_frame_ok && probe->v4l2_capture_nodes > 0 ?
                   "real V4L2 frame captured" :
                   (probe->v4l2_capture_nodes > 0 ? "capture node present; no frame proof" :
                    (probe->v4l2_video_nodes > 0 ? "V4L2 nodes present; no capture stream" :
                                                  "no V4L2 video node"))));
    ADD(status("camera.v4l2", "/sys/class/video4linux",
               probe->v4l2_video_nodes > 0 ? VQEAF_CAP_PRESENT : VQEAF_CAP_UNSUPPORTED,
               probe->v4l2_video_nodes,
               probe->v4l2_video_nodes > 0 ? "V4L2 video nodes present" :
                                             "no V4L2 video node"));
    ADD(status("fingerprint", "misc/input node",
               probe->fp_node_present ? VQEAF_CAP_PRESENT : VQEAF_CAP_UNSUPPORTED,
               probe->fp_node_present,
               probe->fp_node_present ? "node present; secure match required" :
                                        "no fingerprint node"));
    ADD(status("kvm", "/dev/kvm",
               probe->kvm_present ? VQEAF_CAP_PRESENT : VQEAF_CAP_UNSUPPORTED,
               probe->kvm_present,
               probe->kvm_present ? "KVM node present" : "no KVM node"));
    ADD(status("nfc", "not implemented", VQEAF_CAP_UNSUPPORTED, 0,
               "no QEAF NFC provider"));
    ADD(status("baseband", "not implemented", VQEAF_CAP_UNSUPPORTED, 0,
               "no QEAF baseband provider"));
#undef ADD

    return count <= INT32_MAX ? (int)count : -1;
}

int vio_drivers_refresh(void)
{
    VioHwProbe probe;
    VioDriverEvidence evidence;
    VioDriverStatus snapshot[VIO_DRIVERS_MAX];
    int count;

    pthread_mutex_lock(&registry_lock);
    evidence = current_evidence;
    pthread_mutex_unlock(&registry_lock);

    if(vio_caps_probe_device(&probe) != 0) {
        pthread_mutex_lock(&registry_lock);
        probe_ok = 0;
        current_count = 0;
        pthread_mutex_unlock(&registry_lock);
        return -1;
    }

    count = vio_drivers_collect(&probe, &evidence, snapshot,
                                sizeof(snapshot) / sizeof(snapshot[0]));
    if(count < 0) return -1;

    pthread_mutex_lock(&registry_lock);
    memcpy(current_status, snapshot, (size_t)count * sizeof(snapshot[0]));
    current_count = (size_t)count;
    probe_ok = 1;
    pthread_mutex_unlock(&registry_lock);
    return count;
}

size_t vio_drivers_count(void)
{
    size_t count;
    pthread_mutex_lock(&registry_lock);
    count = probe_ok ? current_count : 0;
    pthread_mutex_unlock(&registry_lock);
    return count;
}

int vio_drivers_get(size_t index, VioDriverStatus *out)
{
    int rc = -1;
    if(out == NULL) return -1;
    pthread_mutex_lock(&registry_lock);
    if(probe_ok && index < current_count) {
        *out = current_status[index];
        rc = 0;
    }
    pthread_mutex_unlock(&registry_lock);
    return rc;
}

static int appendf(char *buf, int cap, int *off, const char *fmt, ...)
{
    va_list args;
    int n;

    if(buf == NULL || cap <= 0 || off == NULL || *off < 0 || *off >= cap)
        return -1;
    va_start(args, fmt);
    n = vsnprintf(buf + *off, (size_t)(cap - *off), fmt, args);
    va_end(args);
    if(n < 0) return -1;
    if(n >= cap - *off) {
        buf[cap - 1] = '\0';
        *off = cap - 1;
        return -1;
    }
    *off += n;
    return 0;
}

int vio_drivers_render(const VioDriverStatus *items, size_t count,
                       char *buf, int cap)
{
    int off = 0;
    size_t i;

    if(items == NULL || buf == NULL || cap <= 0) return -1;
    buf[0] = '\0';
    appendf(buf, cap, &off,
            "VQEAF Linux driver registry (userspace ABI %u)\n", VIO_DRIVERS_ABI);
    for(i = 0; i < count; i++) {
        if(appendf(buf, cap, &off, "%-20s %-17s %5d  %-28s  %s\n",
                   items[i].name, state_name(items[i].state), items[i].detail,
                   items[i].linux_interface, items[i].reason) != 0)
            break;
    }
    return off;
}

int vio_drivers_report(char *buf, int cap)
{
    int rc;
    if(buf == NULL || cap <= 0) return -1;
    pthread_mutex_lock(&registry_lock);
    if(!probe_ok) {
        snprintf(buf, (size_t)cap,
                 "VQEAF Linux driver registry: probe unavailable\n");
        rc = (int)strlen(buf);
        pthread_mutex_unlock(&registry_lock);
        return rc;
    }
    rc = vio_drivers_render(current_status, current_count, buf, cap);
    pthread_mutex_unlock(&registry_lock);
    return rc;
}
