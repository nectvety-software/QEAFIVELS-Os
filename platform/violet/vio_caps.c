/* platform/violet/vio_caps.c - honest hardware capability census.
 *
 * Maps real read-only probe signals to the v0.2 capability registry. The
 * publish/report path is pure so a host gate can prove every state transition
 * without a device; vio_caps_probe_device() gathers the signals from sysfs and
 * dev nodes on the violet interim transport only. Nothing is marked READY
 * without a concrete probe signal. */
#include "vio_caps.h"
#include "vqeaf_camera.h"
#include <string.h>
#include <stdio.h>

static const char *state_name(VqeafCapabilityState s)
{
    switch (s) {
    case VQEAF_CAP_UNSUPPORTED: return "UNSUPPORTED";
    case VQEAF_CAP_PRESENT: return "PRESENT";
    case VQEAF_CAP_READY: return "READY";
    case VQEAF_CAP_DEGRADED: return "DEGRADED";
    case VQEAF_CAP_BLOCKED_FIRMWARE: return "BLOCKED_FIRMWARE";
    case VQEAF_CAP_ERROR: return "ERROR";
    }
    return "?";
}

static void set(const char *n, VqeafCapabilityState s, int32_t detail)
{
    vqeaf_cap_set(n, s, 1u, detail);
}

void vio_caps_publish(const VioHwProbe *p)
{
    if (!p) return;
    set("storage.emmc", p->mmcblk_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED, p->mmcblk_present);
    set("storage.ufs", p->ufs_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED, p->ufs_present);
    set("usb.gadget", p->usb_udc_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED, p->usb_udc_present);
    set("usb.host", p->usb_host_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED, p->usb_host_present);
    set("display.drm", p->drm_card_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED, p->drm_card_present);
    set("display.refresh_high", p->panel_max_refresh > 60 ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED, p->panel_max_refresh);
    set("input.touch", p->touch_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED, p->touch_present);
    set("wifi", p->wlan_iface_present ? VQEAF_CAP_READY :
        (p->wlan_fw_present ? VQEAF_CAP_PRESENT : VQEAF_CAP_BLOCKED_FIRMWARE), p->wlan_iface_present);
    set("bluetooth", p->bt_hci_present ? VQEAF_CAP_READY :
        (p->bt_fw_present ? VQEAF_CAP_PRESENT : VQEAF_CAP_BLOCKED_FIRMWARE), p->bt_hci_present);
    set("audio", p->sound_cards > 0 ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED, p->sound_cards);
    set("gpu", p->kgsl_present ? VQEAF_CAP_READY :
        (p->drm_render_present ? VQEAF_CAP_PRESENT : VQEAF_CAP_UNSUPPORTED), p->kgsl_present);
    set("sensor.accel", p->accel_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED, p->accel_present);
    set("kvm", p->kvm_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED, p->kvm_present);
    /* Live violet evidence: the stock kernel registers V4L2 camera nodes.
     * A node list proves the driver exists, not that a frame can be captured,
     * so camera stays PRESENT until a native V4L2 grab actually succeeds. */
    set("camera", p->v4l2_capture_nodes > 0 ? VQEAF_CAP_PRESENT :
        (p->v4l2_video_nodes > 0 ? VQEAF_CAP_BLOCKED_FIRMWARE :
                                   VQEAF_CAP_UNSUPPORTED), p->v4l2_capture_nodes);
    set("camera.v4l2", p->v4l2_video_nodes > 0 ? VQEAF_CAP_PRESENT :
                                                 VQEAF_CAP_UNSUPPORTED,
        p->v4l2_video_nodes);
    set("fingerprint", p->fp_node_present ? VQEAF_CAP_PRESENT :
                                            VQEAF_CAP_UNSUPPORTED,
        p->fp_node_present);
    /* Explicitly out of scope for this OS build. */
    vqeaf_cap_set("nfc", VQEAF_CAP_UNSUPPORTED, 0u, 0);
    vqeaf_cap_set("baseband", VQEAF_CAP_UNSUPPORTED, 0u, 0);
    (void)p->bt_dev_present; /* diagnostic detail; HCI state still decides */
}

static const char *kNames[] = {
    "storage.emmc", "storage.ufs", "usb.gadget", "usb.host",
    "display.drm", "display.refresh_high", "input.touch",
    "wifi", "bluetooth", "audio", "gpu", "sensor.accel", "kvm",
    "camera", "camera.v4l2", "fingerprint", "nfc", "baseband",
};

int vio_caps_report(char *buf, int cap)
{
    if (!buf || cap <= 0) return 0;
    int off = 0;
    int n = snprintf(buf + off, (size_t)(cap - off),
                     "VQEAF capability census (violet interim transport)\n");
    if (n < 0) return 0;
    off += n;
    for (unsigned i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        if (off >= cap - 1) break;
        VqeafCapability c;
        if (vqeaf_cap_get(kNames[i], &c) != 0) continue;
        n = snprintf(buf + off, (size_t)(cap - off), "%-22s %-17s %d\n",
                     c.name, state_name(c.state), (int)c.detail);
        if (n < 0) break;
        off += n;
    }
    if (off > cap - 1) off = cap - 1;
    buf[off] = '\0';
    return off;
}

#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

static int path_exists(const char *p)
{
    return access(p, F_OK) == 0;
}

static int dir_has_entry(const char *p)
{
    DIR *d = opendir(p);
    if (!d) return 0;
    struct dirent *e;
    int any = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        any = 1;
        break;
    }
    closedir(d);
    return any;
}

static int read_file(const char *p, char *buf, int cap)
{
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, (size_t)(cap - 1));
    close(fd);
    if (r <= 0) return -1;
    buf[r] = '\0';
    return (int)r;
}

/* Scan a directory's entry names for any of the given case-insensitive tags. */
static int dir_name_has_tag(const char *dir, const char *const *tags, int ntags)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int hit = 0;
    while ((e = readdir(d)) != NULL && !hit) {
        if (e->d_name[0] == '.') continue;
        char low[256];
        int i = 0;
        for (; e->d_name[i] && i < (int)sizeof(low) - 1; i++) {
            char ch = e->d_name[i];
            low[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
        }
        low[i] = '\0';
        for (int t = 0; t < ntags; t++)
            if (strstr(low, tags[t])) { hit = 1; break; }
    }
    closedir(d);
    return hit;
}

static int firmware_has_tag(const char *const *tags, int ntags)
{
    const char *dirs[] = { "/vendor/firmware", "/system/vendor/firmware",
                           "/etc/firmware", "/system/etc/firmware",
                           "/lib/firmware", "/firmware" };
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
        if (dir_name_has_tag(dirs[i], tags, ntags)) return 1;
    return 0;
}

static int count_sound_cards(void)
{
    char buf[2048];
    if (read_file("/proc/asound/cards", buf, (int)sizeof(buf)) < 0) return 0;
    int cards = 0;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s >= '0' && *s <= '9') cards++;
        line = nl ? nl + 1 : NULL;
    }
    return cards;
}

static int panel_max_refresh(void)
{
    DIR *d = opendir("/sys/class/drm");
    if (!d) return 0;
    struct dirent *e;
    int best = 0;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "card0-", 6) != 0) continue;
        char pp[256], modes[4096];
        snprintf(pp, sizeof(pp), "/sys/class/drm/%s/modes", e->d_name);
        if (read_file(pp, modes, (int)sizeof(modes)) < 0) continue;
        char *line = modes;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            int w = 0, h = 0, hz = 0;
            if (sscanf(line, "%dx%dx%d", &w, &h, &hz) == 3 && hz > best)
                best = hz;
            line = nl ? nl + 1 : NULL;
        }
    }
    closedir(d);
    return best;
}

static int touch_present(void)
{
    static const char *tags[] = { "touch", "nvt", "fts", "goodix", "himax",
                                  "synaptics", "atmel", "ilitek", "focal" };
    DIR *d = opendir("/sys/class/input");
    if (!d) return 0;
    struct dirent *e;
    int hit = 0;
    while ((e = readdir(d)) != NULL && !hit) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char pp[256], name[256];
        snprintf(pp, sizeof(pp), "/sys/class/input/%s/device/name", e->d_name);
        if (read_file(pp, name, (int)sizeof(name)) < 0) continue;
        for (int i = 0; name[i]; i++)
            if (name[i] >= 'A' && name[i] <= 'Z') name[i] = (char)(name[i] + 32);
        for (unsigned t = 0; t < sizeof(tags) / sizeof(tags[0]); t++)
            if (strstr(name, tags[t])) { hit = 1; break; }
    }
    closedir(d);
    return hit;
}

static int accel_present(void)
{
    static const char *tags[] = { "accel", "lsm6", "bma", "icm", "mpu",
                                  "stk8", "kxcjk", "bmc150", "lis2" };
    DIR *d = opendir("/sys/bus/iio/devices");
    if (!d) return 0;
    struct dirent *e;
    int hit = 0;
    while ((e = readdir(d)) != NULL && !hit) {
        if (strncmp(e->d_name, "iio:device", 10) != 0) continue;
        char pp[256], name[256];
        snprintf(pp, sizeof(pp), "/sys/bus/iio/devices/%s/name", e->d_name);
        if (read_file(pp, name, (int)sizeof(name)) < 0) continue;
        for (int i = 0; name[i]; i++)
            if (name[i] >= 'A' && name[i] <= 'Z') name[i] = (char)(name[i] + 32);
        for (unsigned t = 0; t < sizeof(tags) / sizeof(tags[0]); t++)
            if (strstr(name, tags[t])) { hit = 1; break; }
    }
    closedir(d);
    return hit;
}

static void v4l2_census(int *video_nodes, int *capture_nodes)
{
    DIR *d = opendir("/sys/class/video4linux");
    struct dirent *e;

    *video_nodes = *capture_nodes = 0;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        int i;
        if (strncmp(e->d_name, "video", 5) != 0) continue;
        for (i = 5; e->d_name[i]; i++)
            if (e->d_name[i] < '0' || e->d_name[i] > '9') break;
        if (i == 5 || e->d_name[i]) continue;
        {
            char sp[192], name[128] = "", dev[192];
            VqeafV4l2Role r;
            int fd, n;
            snprintf(sp, sizeof(sp), "/sys/class/video4linux/%s/name",
                     e->d_name);
            fd = open(sp, O_RDONLY | O_CLOEXEC);
            if (fd >= 0) {
                n = (int)read(fd, name, sizeof(name) - 1);
                close(fd);
                if (n > 0) name[n] = '\0';
                for (int k = 0; name[k]; k++)
                    if (name[k] == '\n' || name[k] == '\r') name[k] = '\0';
            }
            (*video_nodes)++;
            r = vqeaf_v4l2_role(name[0] ? name : e->d_name);
            /* Empty sysfs name: ask the kernel what the node really is. */
            if (r == VQEAF_V4L2_ROLE_CAPTURE && name[0] == '\0') {
                VqeafCameraNode info;
                snprintf(dev, sizeof(dev), "/dev/%s", e->d_name);
                if (vqeaf_camera_query(dev, &info) == 0 &&
                    info.role != VQEAF_V4L2_ROLE_CAPTURE) r = info.role;
            }
            if (r == VQEAF_V4L2_ROLE_CAPTURE) (*capture_nodes)++;
        }
    }
    closedir(d);
}

int vio_caps_probe_device(VioHwProbe *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    static const char *wlan_tags[] = { "wcn", "wlan", "qca6", "qwlan" };
    static const char *fp_tags[] = { "goodix_fp", "fpc", "fingerprint", "biometric" };
    static const char *bt_tags[] = { "rampatch", "nvm", "btqca", "bluetooth", "btnv" };
    out->mmcblk_present = path_exists("/sys/block/mmcblk0");
    out->ufs_present = path_exists("/sys/bus/platform/drivers/ufshcd") ||
                       path_exists("/dev/sda");
    out->usb_udc_present = dir_has_entry("/sys/class/udc");
    out->usb_host_present = dir_has_entry("/sys/bus/usb/devices");
    out->drm_card_present = path_exists("/dev/dri/card0");
    out->drm_render_present = path_exists("/dev/dri/renderD128");
    out->panel_max_refresh = panel_max_refresh();
    out->touch_present = touch_present();
    out->wlan_iface_present = path_exists("/sys/class/net/wlan0");
    out->wlan_fw_present = firmware_has_tag(wlan_tags, 4);
    out->bt_hci_present = dir_has_entry("/sys/class/bluetooth");
    /* Stock Violet mounts its dedicated Bluetooth VFAT partition here. */
    out->bt_fw_present = dir_has_entry("/vendor/bt_firmware") ||
                         firmware_has_tag(bt_tags, 5);
    out->sound_cards = count_sound_cards();
    out->kgsl_present = path_exists("/dev/kgsl-3d0");
    out->accel_present = accel_present();
    out->kvm_present = path_exists("/dev/kvm");
    v4l2_census(&out->v4l2_video_nodes, &out->v4l2_capture_nodes);
    out->fp_node_present = dir_name_has_tag("/sys/class/misc", fp_tags, 4) ||
                           dir_name_has_tag("/dev", fp_tags, 4);
    out->bt_dev_present = dir_has_entry("/sys/class/bt-dev");
    return 0;
}
#else
int vio_caps_probe_device(VioHwProbe *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    return -1;
}
#endif
