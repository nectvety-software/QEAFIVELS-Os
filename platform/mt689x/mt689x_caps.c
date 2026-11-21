/* SPDX-License-Identifier: MIT */
/* platform/mt689x/mt689x_caps.c - honest capability census for MT6891/MT6893.
 *
 * The publish/report half is pure, exactly like violet's, so a host test can
 * drive every state without hardware. The probe half reads sysfs and dev
 * nodes; it never opens a partition for writing and never asserts a device
 * that is not there.
 */
#include "mt689x_caps.h"
#include "mt689x.h"

#include <stdio.h>
#include <string.h>

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

void mtk_caps_publish(const MtkHwProbe *p)
{
    if (!p) return;
    /* MT6891/MT6893 phones are UFS-only: there is no eMMC slot, so the eMMC
     * row is reported unsupported rather than left blank. */
    set("storage.emmc", VQEAF_CAP_UNSUPPORTED, 0);
    set("storage.ufs", p->ufs_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
        p->ufs_present);
    set("usb.gadget", p->usb_udc_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
        p->usb_udc_present);
    set("usb.host", p->usb_host_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
        p->usb_host_present);
    set("display.drm", p->drm_card_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
        p->drm_card_present);
    set("display.refresh_high",
        p->panel_max_refresh > 60 ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
        p->panel_max_refresh);
    set("input.touch", p->touch_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
        p->touch_present);
    set("wifi", p->wlan_iface_present ? VQEAF_CAP_READY :
        (p->wlan_fw_present ? VQEAF_CAP_PRESENT : VQEAF_CAP_BLOCKED_FIRMWARE),
        p->wlan_iface_present);
    set("bluetooth", p->bt_hci_present ? VQEAF_CAP_READY :
        (p->bt_fw_present ? VQEAF_CAP_PRESENT : VQEAF_CAP_BLOCKED_FIRMWARE),
        p->bt_hci_present);
    set("audio", p->sound_cards > 0 ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
        p->sound_cards);
    /* Panfrost publishes renderD128; the vendor kbase driver publishes
     * /dev/mali0. Either proves a GPU is bound, but only the render node lets
     * userspace actually submit work, so that is the READY line. */
    set("gpu", p->drm_render_present ? VQEAF_CAP_READY :
        (p->mali_node_present ? VQEAF_CAP_PRESENT : VQEAF_CAP_UNSUPPORTED),
        p->drm_render_present);
    set("sensor.accel", p->accel_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
        p->accel_present);
    set("kvm", p->kvm_present ? VQEAF_CAP_READY : VQEAF_CAP_UNSUPPORTED,
        p->kvm_present);
    /* Out of scope for this build, stated rather than omitted. */
    vqeaf_cap_set("nfc", VQEAF_CAP_UNSUPPORTED, 0u, 0);
    vqeaf_cap_set("camera", VQEAF_CAP_UNSUPPORTED, 0u, 0);
    vqeaf_cap_set("baseband", VQEAF_CAP_UNSUPPORTED, 0u, 0);
}

static const char *kNames[] = {
    "storage.emmc", "storage.ufs", "usb.gadget", "usb.host",
    "display.drm", "display.refresh_high", "input.touch",
    "wifi", "bluetooth", "audio", "gpu", "sensor.accel", "kvm",
    "nfc", "camera", "baseband",
};

int mtk_caps_report(char *buf, int cap)
{
    if (!buf || cap <= 0) return 0;
    int off = 0;
    int n = snprintf(buf + off, (size_t)(cap - off),
                     "QEAF capability census (MT6891/MT6893, %s)\n",
                     mtk_board_name());
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

static int path_exists(const char *p) { return access(p, F_OK) == 0; }

static int read_small(const char *p, char *buf, int cap)
{
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, (size_t)(cap - 1));
    close(fd);
    if (r <= 0) return -1;
    buf[r] = '\0';
    return (int)r;
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

static void lower(char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + 32);
}

static int dir_name_has_tag(const char *dir, const char *const *tags, int ntags)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int hit = 0;
    while ((e = readdir(d)) != NULL && !hit) {
        char low[256];
        if (e->d_name[0] == '.') continue;
        snprintf(low, sizeof(low), "%s", e->d_name);
        lower(low);
        for (int t = 0; t < ntags; t++)
            if (strstr(low, tags[t])) { hit = 1; break; }
    }
    closedir(d);
    return hit;
}

/* MediaTek firmware lives under vendor/firmware/mediatek/mt6893 (vendor) or
 * lib/firmware/mediatek/mt6893 (a packaged root). Check both, recursively
 * one level into the mt6893 directory because that is where the stack asks
 * the kernel to look. */
static int firmware_has_tag(const char *const *tags, int ntags)
{
    static const char *dirs[] = {
        "/vendor/firmware/mediatek/mt6893",
        "/vendor/firmware/mediatek",
        "/lib/firmware/mediatek/mt6893",
        "/lib/firmware/mediatek",
        "/etc/firmware/mediatek/mt6893",
        "/firmware/mediatek/mt6893",
    };
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
        if (dir_name_has_tag(dirs[i], tags, ntags)) return 1;
    return 0;
}

static int count_sound_cards(void)
{
    char buf[2048];
    if (read_small("/proc/asound/cards", buf, (int)sizeof(buf)) < 0) return 0;
    int cards = 0;
    for (char *line = buf; line && *line; ) {
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
        char pp[256], modes[4096];
        if (strncmp(e->d_name, "card0-", 6) != 0) continue;
        snprintf(pp, sizeof(pp), "/sys/class/drm/%s/modes", e->d_name);
        if (read_small(pp, modes, (int)sizeof(modes)) < 0) continue;
        for (char *line = modes; line && *line; ) {
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

static int input_has_tag(const char *const *tags, int ntags)
{
    DIR *d = opendir("/sys/class/input");
    if (!d) return 0;
    struct dirent *e;
    int hit = 0;
    while ((e = readdir(d)) != NULL && !hit) {
        char pp[256], name[256];
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        snprintf(pp, sizeof(pp), "/sys/class/input/%s/device/name", e->d_name);
        if (read_small(pp, name, (int)sizeof(name)) < 0) continue;
        lower(name);
        for (int t = 0; t < ntags; t++)
            if (strstr(name, tags[t])) { hit = 1; break; }
    }
    closedir(d);
    return hit;
}

static int accel_present(void)
{
    /* The SCP sensor hub publishes an IIO device named after the physical
     * sensor; on chopin that is an accelerometer behind sensorhub. Accept
     * the generic name and the common parts. */
    static const char *tags[] = { "accel", "lsm6", "bmi", "icm", "mpu",
                                  "stk8", "kxcjk", "lis2", "sensorhub" };
    DIR *d = opendir("/sys/bus/iio/devices");
    if (!d) return 0;
    struct dirent *e;
    int hit = 0;
    while ((e = readdir(d)) != NULL && !hit) {
        char pp[256], name[256];
        if (strncmp(e->d_name, "iio:device", 10) != 0) continue;
        snprintf(pp, sizeof(pp), "/sys/bus/iio/devices/%s/name", e->d_name);
        if (read_small(pp, name, (int)sizeof(name)) < 0) continue;
        lower(name);
        for (unsigned t = 0; t < sizeof(tags) / sizeof(tags[0]); t++)
            if (strstr(name, tags[t])) { hit = 1; break; }
    }
    closedir(d);
    return hit;
}

int mtk_caps_probe_device(MtkHwProbe *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    /* mt6893 asks for these names; the manifest extractor copies whatever the
     * stock vendor image actually contains, and this probe only reports what
     * is on disk now. */
    static const char *wlan_tags[] = { "wifi_ram_code", "wifi_mt", "mt6893", "wifi" };
    static const char *bt_tags[] = { "bt_ram_code", "rom_patch", "bt_mt", "bt_addr" };
    static const char *touch_tags[] = { "novatek", "nvt", "touch" };

    /* The MediaTek glue driver registers as "ufshcd-mtk" and binds the DT node
     * 11270000.ufshci; a SCSI disk behind it (sda, or a by-name link on a
     * stock vendor root) proves the link actually trained. */
    out->ufs_present =
        path_exists("/sys/bus/platform/drivers/ufshcd-mtk/11270000.ufshci") ||
        path_exists("/sys/bus/platform/drivers/ufshcd-mtk") ||
        path_exists("/sys/block/sda") ||
        dir_has_entry("/sys/class/scsi_device") ||
        path_exists("/dev/block/by-name/vendor");
    out->usb_udc_present = dir_has_entry("/sys/class/udc");
    out->usb_host_present = dir_has_entry("/sys/bus/usb/devices");
    out->drm_card_present = path_exists("/dev/dri/card0");
    out->drm_render_present = path_exists("/dev/dri/renderD128");
    out->mali_node_present = path_exists("/dev/mali0");
    out->panel_max_refresh = panel_max_refresh();
    out->touch_present = input_has_tag(touch_tags, 3);
    out->wlan_iface_present = path_exists("/sys/class/net/wlan0");
    out->wlan_fw_present = firmware_has_tag(wlan_tags, 4);
    out->bt_hci_present = dir_has_entry("/sys/class/bluetooth");
    out->bt_fw_present = firmware_has_tag(bt_tags, 4);
    out->sound_cards = count_sound_cards();
    out->accel_present = accel_present();
    out->kvm_present = path_exists("/dev/kvm");
    return 0;
}

const char *mtk_board_name(void)
{
    static char name[64] = "MT6891/MT6893";
    static int done;
    char compat[256];

    if (done) return name;
    done = 1;
    if (read_small("/proc/device-tree/compatible", compat,
                   (int)sizeof(compat)) > 0) {
        /* "mediatek,MT6893" — read_small NUL-terminates the first string. */
        if (strlen(compat) > 0) {
            const char *comma = strchr(compat, ',');
            snprintf(name, sizeof(name), "%s", comma ? comma + 1 : compat);
        }
    }
    return name;
}

#else
int mtk_caps_probe_device(MtkHwProbe *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    return -1;
}

const char *mtk_board_name(void) { return "MT6891/MT6893"; }
#endif

/* ---- adapters for the shared rescue command loop -------------------------
 * fs/rescue.c is one source for both boards and calls the violet-named
 * capability entry points. On MT6893 they must answer with MediaTek probes,
 * not with violet's eMMC/WCN3990 assumptions. */
#include "vio_caps.h"

void vio_caps_publish(const VioHwProbe *p)
{
    MtkHwProbe m;
    if (!p) return;
    memset(&m, 0, sizeof(m));
    m.ufs_present = p->ufs_present;
    m.usb_udc_present = p->usb_udc_present;
    m.usb_host_present = p->usb_host_present;
    m.drm_card_present = p->drm_card_present;
    m.drm_render_present = p->drm_render_present;
    m.mali_node_present = p->kgsl_present;
    m.panel_max_refresh = p->panel_max_refresh;
    m.touch_present = p->touch_present;
    m.wlan_iface_present = p->wlan_iface_present;
    m.wlan_fw_present = p->wlan_fw_present;
    m.bt_hci_present = p->bt_hci_present;
    m.bt_fw_present = p->bt_fw_present;
    m.sound_cards = p->sound_cards;
    m.accel_present = p->accel_present;
    m.kvm_present = p->kvm_present;
    mtk_caps_publish(&m);
}

int vio_caps_report(char *buf, int cap)
{
    return mtk_caps_report(buf, cap);
}

int vio_caps_probe_device(VioHwProbe *out)
{
    MtkHwProbe m;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (mtk_caps_probe_device(&m) != 0) return -1;
    out->ufs_present = m.ufs_present;
    out->usb_udc_present = m.usb_udc_present;
    out->usb_host_present = m.usb_host_present;
    out->drm_card_present = m.drm_card_present;
    out->drm_render_present = m.drm_render_present;
    out->kgsl_present = m.mali_node_present;
    out->panel_max_refresh = m.panel_max_refresh;
    out->touch_present = m.touch_present;
    out->wlan_iface_present = m.wlan_iface_present;
    out->wlan_fw_present = m.wlan_fw_present;
    out->bt_hci_present = m.bt_hci_present;
    out->bt_fw_present = m.bt_fw_present;
    out->sound_cards = m.sound_cards;
    out->accel_present = m.accel_present;
    out->kvm_present = m.kvm_present;
    return 0;
}
