/* SPDX-License-Identifier: MIT */
/* platform/violet/wifi_bringup.c - real WCN3990 bring-up on stock violet.
 *
 * Evidence this file is built on (measured on the phone, not assumed):
 *   - /sys/module/wlan, /sys/module/cfg80211 and /sys/module/bluetooth are
 *     present: the stock kernel has qcacld-3.0 and cfg80211 built in.
 *   - kmsg says "wlan_hdd_state wlan major(500) initialized", "icnss:
 *     Platform driver probed successfully" and the DT exposes
 *     qcom,icnss@18800000 with qcom,wlan-msa-memory.
 *   - There is no wlan netdev, because request_firmware() has no filesystem
 *     to read: /vendor and /vendor/firmware_mnt were never mounted.
 *
 * The stock fstab (vendor/etc/fstab.qcom) is the reference:
 *   /dev/block/bootdevice/by-name/vendor -> /vendor              ext4 ro
 *   /dev/block/bootdevice/by-name/modem  -> /vendor/firmware_mnt vfat ro
 * and vendor/etc/fstab.persist:
 *   persist -> /mnt/vendor/persist ext4
 * The wlan config files are symlinks into those mounts:
 *   /vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini -> /vendor/etc/wifi/...
 *   /vendor/firmware/wlan/qca_cld/wlan_mac.bin      -> /mnt/vendor/persist/...
 *
 * The modem partition is vfat, but this kernel was built without the NLS
 * charsets, so the mount fails with EINVAL ("IO charset iso8859-1 not
 * found"). The blobs use plain 8.3 names, so fat_ro.c copies /image into
 * /lib/firmware instead - read-only, no module, no charset table.
 *
 * Nothing here writes a partition: every mount is read-only and no verb is
 * added to the flash path.
 */
#define _GNU_SOURCE

#include "wifi_bringup.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "boot_journal.h"
#include "fat_ro.h"
#include "host_services.h"
#include "qrtr_ns.h"
#include "violet.h"

#define VENDOR_DEV  "/dev/mmcblk0p86"
#define MODEM_DEV   "/dev/mmcblk0p82"
#define BT_DEV      "/dev/mmcblk0p38"
#define PERSIST_DEV "/dev/mmcblk0p74"

#define VENDOR_DIR  "/vendor"
#define MODEM_DIR   "/vendor/firmware_mnt"
#define BT_DIR      "/vendor/bt_firmware"
#define PERSIST_DIR "/mnt/vendor/persist"
#define IMAGE_DIR   "/vendor/firmware_mnt/image"
#define FW_LINK_DIR "/lib/firmware"

/* Boot-time state machine. Stage is only advanced forward; every transition
 * is logged with the reason so QEAFBOOT.LOG tells the truth even when the
 * hardware refuses. */
enum {
    WIFI_STAGE_IDLE = 0,
    WIFI_STAGE_MOUNTED,
    WIFI_STAGE_WAIT,
    WIFI_STAGE_GAVE_UP,
    WIFI_STAGE_UP
};

static int wifi_stage;
static uint64_t wifi_t0;
static uint64_t wifi_t_kick;
static int wifi_modem_started;
static int wifi_icnss_bound;
static int wifi_modem_rebound;
static int wifi_recover_tried;
static unsigned wifi_retries;
static unsigned wifi_mount_retries;
static int wifi_vendor_ok;
static int wifi_modem_ok;
static int wifi_modem_mounted;
static int wifi_fw_copied;
static int wifi_fw_files;
static unsigned long long wifi_fw_bytes;
static int wifi_persist_ok;
static int wifi_links;
static char wifi_p82_info[160] = "not probed";
static char wifi_note[192] = "not started";
static int wifi_modem_fd = -1;   /* held open: close() shuts the modem down */
static volatile int wifi_modem_open_rc; /* 0 = pending, 1 = online, <0 errno */
static volatile int wifi_modem_thread_done;
static pthread_t wifi_modem_thread;
static int wifi_modem_thread_started;
/* Set by `wifi off`: no boot, no retry, and close() the fd if it is
 * online, which is subsystem_put() for the modem PIL. */
static int wifi_modem_hold;
/* Boot does not start the modem implicitly. QEAF starts the native
 * QRTR/PDR/TFTP/RMTFS stack first; the rescue `wifi up` command explicitly
 * opts into PIL boot after those services have been launched. */
static int wifi_modem_opt_in;

static int dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int dir_has_entries(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int found = 0;

    if(dir == NULL) return 0;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') continue;
        found = 1;
        break;
    }
    closedir(dir);
    return found;
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int read_text(const char *path, char *buf, size_t cap)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n;

    if(fd < 0) return -1;
    n = read(fd, buf, (size_t)cap - 1);
    close(fd);
    if(n <= 0) return -1;
    buf[n] = '\0';
    while(n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return (int)n;
}

static int write_text(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    size_t length, done = 0;

    if(fd < 0) return -1;
    length = strlen(text);
    while(done < length) {
        ssize_t n = write(fd, text + done, length - done);
        if(n < 0) {
            if(errno == EINTR) continue;
            close(fd);
            return -1;
        }
        done += (size_t)n;
    }
    close(fd);
    return 0;
}

/* Mount only when the target is still the empty initramfs directory. A
 * second call is a no-op, and an already-mounted target is success. */
static int mount_partition(const char *source, const char *target,
                           const char *type, unsigned long flags,
                           const char *data)
{
    if(!dir_exists(target) && vio_mkdir_p(target, 0755) < 0) {
        vio_log("wifi: cannot create %s: %s", target, strerror(errno));
        return -1;
    }
    if(mount(source, target, type, flags, data) < 0) {
        if(errno == EBUSY) return 0;
        return -1;
    }
    vio_log("wifi: mounted %s on %s (%s, ro)", source, target, type);
    return 0;
}

/* Violet's fstab keeps Bluetooth firmware on p38. Try VFAT first, then the
 * short-name FAT driver for kernels built without the VFAT NLS charset. */
static int mount_bluetooth_firmware(void)
{
    static const char *const types[] = { "vfat", "msdos" };
    for(size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if(mount_partition(BT_DEV, BT_DIR, types[i], MS_RDONLY,
                           "shortname=lower") == 0 &&
           dir_has_entries(BT_DIR)) {
            vio_log("bluetooth: p38 firmware available at %s", BT_DIR);
            return 0;
        }
    }
    vio_log("bluetooth: p38 firmware partition unavailable or empty");
    return -1;
}

/* request_firmware() always looks in /lib/firmware. Rather than guessing the
 * names the PIL/icnss drivers ask for, link the whole modem image directory
 * plus the vendor wlan directory into it. The links point at the read-only
 * mounts; nothing is copied and no blob is bundled in the image. */
static int link_firmware_tree(void)
{
    DIR *dir;
    struct dirent *entry;
    int made = 0;

    if(!dir_exists(FW_LINK_DIR) && vio_mkdir_p(FW_LINK_DIR, 0755) < 0)
        return -1;

    dir = opendir(IMAGE_DIR);
    if(dir == NULL) {
        vio_log("wifi: %s not readable: %s", IMAGE_DIR, strerror(errno));
        return -1;
    }
    while((entry = readdir(dir)) != NULL) {
        char source[512];
        char link[512];
        struct stat st;

        if(entry->d_name[0] == '.') continue;
        if(snprintf(source, sizeof(source), "%s/%s", IMAGE_DIR,
                    entry->d_name) >= (int)sizeof(source)) continue;
        if(snprintf(link, sizeof(link), "%s/%s", FW_LINK_DIR,
                    entry->d_name) >= (int)sizeof(link)) continue;
        if(lstat(link, &st) == 0) continue;
        if(symlink(source, link) == 0) made++;
    }
    closedir(dir);

    /* wlan/qca_cld/WCNSS_qcom_cfg.ini and wlan_mac.bin live under the vendor
     * partition, not the modem image. One directory symlink covers both. */
    {
        const char *link = FW_LINK_DIR "/wlan";
        struct stat st;
        /* A real config staged from the crDroid device tree must win over the
         * broken vendor symlink; never overwrite that directory with a link. */
        if(lstat(link, &st) == 0 && S_ISDIR(st.st_mode)) {
            vio_log("wifi: using staged Wi-Fi config directory");
        } else if(lstat(link, &st) != 0 &&
                  symlink("/vendor/firmware/wlan", link) == 0) {
            made++;
        }
    }

    wifi_links = made;
    vio_log("wifi: %d firmware links under %s", made, FW_LINK_DIR);
    return made > 0 ? 0 : -1;
}

/* RAM-only ROM firmware override. The Fastboot-ROM WLAN/SERVREG/DSP files are
 * already in /lib/firmware from the initramfs. p82 still holds the large modem
 * image, so link any file we did NOT stage from p82 into /lib/firmware. This
 * writes no partition: /lib/firmware is rootfs RAM and symlinks are RAM-only. */
static int use_staged_rom_firmware(void)
{
    DIR *dir;
    struct dirent *de;
    int linked = 0;

    if(!file_exists("/etc/qeaf/rom-firmware-staged")) return 0;
    dir = opendir(IMAGE_DIR);
    if(dir == NULL) return 0;
    while((de = readdir(dir)) != NULL) {
        char source[320], target[320];
        struct stat st;
        if(de->d_name[0] == '.') continue;
        if(snprintf(source, sizeof(source), "%s/%s", IMAGE_DIR,
                    de->d_name) >= (int)sizeof(source)) continue;
        if(snprintf(target, sizeof(target), "/lib/firmware/%s",
                    de->d_name) >= (int)sizeof(target)) continue;
        if(stat(target, &st) == 0) continue;   /* staged file wins */
        if(symlink(source, target) == 0) linked++;
    }
    closedir(dir);
    vio_log("wifi: staged ROM firmware active (%d p82 links in /lib/firmware)",
            linked);
    return 1;
}

static int set_firmware_paths(void)
{
    const char *fw_class = "/sys/module/firmware_class/parameters/path";
    const char *wlan_path = "/sys/module/wlan/parameters/fwpath";
    char current[256] = "";
    int ok = 0;

    if(file_exists(fw_class)) {
        const char *path = use_staged_rom_firmware() ?
                           "/lib/firmware" : IMAGE_DIR;
        if(read_text(fw_class, current, sizeof(current)) > 0)
            vio_log("wifi: firmware_class.path was '%s'", current);
        if(write_text(fw_class, path) == 0) ok++;
        else vio_log("wifi: cannot set firmware_class.path: %s",
                     strerror(errno));
    }
    else vio_log("wifi: %s absent (kernel keeps /lib/firmware only)", fw_class);

    if(file_exists(wlan_path)) {
        if(read_text(wlan_path, current, sizeof(current)) > 0)
            vio_log("wifi: wlan fwpath was '%s'", current);
        if(write_text(wlan_path, "/vendor/firmware") == 0) ok++;
        else vio_log("wifi: cannot set wlan fwpath: %s", strerror(errno));
    }
    else vio_log("wifi: %s absent", wlan_path);

    return ok;
}

/* Mount the modem partition; if the kernel refuses it (missing NLS), copy
 * the /image tree through the read-only FAT reader instead. */
static void bring_up_modem_firmware(void)
{
    static const char *const types[] = {"vfat", "vfat", "msdos", "exfat",
                                        "ext4"};
    static const char *const data[] = {"shortname=lower", NULL, NULL, NULL, ""};
    char info[160] = "";
    unsigned i;

    if(fat_ro_probe(MODEM_DEV, info, (int)sizeof(info)) == 0)
        snprintf(wifi_p82_info, sizeof(wifi_p82_info), "%s", info);
    else
        snprintf(wifi_p82_info, sizeof(wifi_p82_info), "not FAT: %s", info);
    vio_log("wifi: p82 %s", wifi_p82_info);

    for(i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if(mount_partition(MODEM_DEV, MODEM_DIR, types[i], MS_RDONLY,
                           data[i]) == 0 && dir_exists(IMAGE_DIR)) {
            wifi_modem_mounted = 1;
            wifi_modem_ok = 1;
            return;
        }
    }
    vio_log("wifi: modem vfat mount refused (no NLS charset); using FAT reader");

    if(!dir_exists(FW_LINK_DIR) && vio_mkdir_p(FW_LINK_DIR, 0755) < 0)
        return;
    {
        int copied = 0;
        unsigned long long bytes = 0;
        int rc = fat_ro_copy_dir(MODEM_DEV, "image", FW_LINK_DIR, &copied,
                                 &bytes);
        if(rc == 0 && copied > 0) {
            wifi_modem_ok = 1;
            wifi_fw_copied = 1;
            wifi_fw_files = copied;
            wifi_fw_bytes = bytes;
            vio_log("wifi: copied %d firmware files (%llu KiB) from p82",
                    copied, bytes / 1024);
        }
        else vio_log("wifi: FAT copy failed rc=%d copied=%d", rc, copied);
    }
}

/* Start the modem. msm-4.14 exposes /sys/kernel/boot_<name>/boot (the vendor
 * init.qcom.rc writes boot_slpi/boot the same way). The modem firmware is
 * what loads the WCN3990 MSA image, so wlan only appears after this runs. */
/* The kernel exposes one char device per msm subsystem (class "subsys").
 * Opening /dev/subsys_modem calls subsystem_get_with_fwname("modem", ...) in
 * subsystem_restart.c, which is pil_boot("modem.mdt"): the image is read from
 * firmware_class.path, authenticated by TZ and the modem core is released
 * from reset. close() is subsystem_put() and powers it off again, so the fd
 * is held for the lifetime of this process. */
static int open_modem_subsystem(void)
{
    char dev[64];
    char node[64] = "/dev/subsys_modem";
    unsigned major_no, minor_no;
    struct stat st;
    int fd;

    if(wifi_modem_fd >= 0) return 0;
    if(wifi_modem_hold) return -ECANCELED;

    if(read_text("/sys/class/subsys/subsys_modem/dev", dev, sizeof(dev)) < 0) {
        vio_log("wifi: /sys/class/subsys/subsys_modem/dev absent: %s",
                strerror(errno));
        return -ENOENT;
    }
    if(sscanf(dev, "%u:%u", &major_no, &minor_no) != 2) {
        vio_log("wifi: bad subsys_modem dev string '%s'", dev);
        return -EINVAL;
    }

    if(stat(node, &st) == 0) {
        if((st.st_mode & S_IFMT) != S_IFCHR ||
           major(st.st_rdev) != major_no || minor(st.st_rdev) != minor_no) {
            unlink(node);
        }
    }
    if(stat(node, &st) != 0) {
        if(mknod(node, S_IFCHR | 0666, makedev(major_no, minor_no)) < 0) {
            vio_log("wifi: mknod %s failed: %s", node, strerror(errno));
            return -errno;
        }
    }

    vio_log("wifi: opening %s (%u:%u) -> modem.mdt", node, major_no,
            minor_no);
    fd = open(node, O_RDONLY | O_CLOEXEC);
    if(fd < 0) {
        int err = errno;
        vio_log("wifi: modem boot failed: %s", strerror(err));
        return -err;
    }
    wifi_modem_fd = fd;
    vio_log("wifi: modem subsystem online (fd %d held)", fd);
    return 0;
}

static int start_modem(void)
{
    int rc;

    if(file_exists("/sys/kernel/boot_modem/boot")) {
        if(write_text("/sys/kernel/boot_modem/boot", "1") == 0) {
            vio_log("wifi: wrote /sys/kernel/boot_modem/boot=1");
            return 0;
        }
        vio_log("wifi: boot_modem write failed: %s", strerror(errno));
    }
    rc = open_modem_subsystem();
    return rc == 0 ? 0 : -1;
}

static void *modem_thread(void *arg)
{
    int rc;

    (void)arg;
    /* Let the rescue LUN and the panel come up before a call that can spend
     * up to ten seconds inside pil_boot(). */
    vio_sleep_ms(1500);
    if(wifi_modem_hold) {
        wifi_modem_thread_done = 1;
        return NULL;
    }
    rc = start_modem();
    wifi_modem_open_rc = rc == 0 ? 1 : (errno ? -errno : -1);
    wifi_modem_thread_done = 1;
    if(rc == 0) {
        vio_log("wifi: modem image loaded, WLFW service should follow");
    }
    return NULL;
}

/* If the device never bound (probe ran before the firmware existed), bind it
 * now. The driver name is discovered from /sys/bus/platform/drivers, never
 * assumed. */
static int bind_platform_device(const char *match, const char *driver_match)
{
    DIR *dir = opendir("/sys/bus/platform/devices");
    struct dirent *entry;
    int bound = 0;

    if(dir == NULL) return -1;
    while((entry = readdir(dir)) != NULL) {
        char path[512];
        char driver[256] = "";
        DIR *drivers;
        struct dirent *candidate;

        if(entry->d_name[0] == '.') continue;
        if(strstr(entry->d_name, match) == NULL) continue;

        snprintf(path, sizeof(path), "/sys/bus/platform/devices/%s/driver",
                 entry->d_name);
        if(readlink(path, driver, sizeof(driver) - 1) > 0) {
            char *slash = strrchr(driver, '/');
            vio_log("wifi: %s already bound to %s", entry->d_name,
                    slash ? slash + 1 : driver);
            bound = 1;
            continue;
        }

        drivers = opendir("/sys/bus/platform/drivers");
        if(drivers == NULL) continue;
        while((candidate = readdir(drivers)) != NULL) {
            char bind[512];
            if(candidate->d_name[0] == '.') continue;
            if(strstr(candidate->d_name, driver_match) == NULL) continue;
            snprintf(bind, sizeof(bind), "/sys/bus/platform/drivers/%s/bind",
                     candidate->d_name);
            if(write_text(bind, entry->d_name) == 0) {
                vio_log("wifi: bound %s to %s", entry->d_name,
                        candidate->d_name);
                bound = 1;
            }
            else vio_log("wifi: bind %s -> %s failed: %s", entry->d_name,
                         candidate->d_name, strerror(errno));
        }
        closedir(drivers);
    }
    closedir(dir);
    return bound ? 0 : -1;
}

void vio_wifi_modem_start(void)
{
    if(!vio_host_wlan_policy_ready()) {
        snprintf(wifi_note, sizeof(wifi_note), "%s",
                 vio_host_wlan_policy_note());
        wifi_modem_hold = 1;
        wifi_modem_opt_in = 0;
        wifi_modem_thread_started = 0;
        wifi_modem_thread_done = 0;
        wifi_modem_open_rc = 0;
        vio_log("wifi: %s", wifi_note);
        return;
    }

    /* Arm the RAM-only live trace before the modem runs. UART/kmsg remains
     * available if the SoC resets before the trace can be collected. */
    vio_journal_trace_arm(1);
    wifi_modem_hold = 0;
    wifi_modem_opt_in = 1;
    wifi_t0 = vio_monotonic_ms();
    wifi_t_kick = wifi_t0;
    if(wifi_stage == WIFI_STAGE_GAVE_UP) wifi_stage = WIFI_STAGE_MOUNTED;
    wifi_modem_thread_started = 0;
    wifi_modem_thread_done = 0;
    wifi_modem_open_rc = 0;
    snprintf(wifi_note, sizeof(wifi_note),
             "modem start requested by QEAF native service manager");
    vio_log("wifi: modem start requested by the host");
}

void vio_wifi_modem_stop(void)
{
    vio_journal_trace_arm(0);
    wifi_modem_hold = 1;
    if(wifi_modem_fd >= 0) {
        close(wifi_modem_fd);
        wifi_modem_fd = -1;
        vio_log("wifi: modem subsystem released (fd closed)");
    } else {
        vio_log("wifi: modem hold set before it started");
    }
    wifi_modem_started = 0;
    snprintf(wifi_note, sizeof(wifi_note), "modem released on request");
    if(wifi_stage == WIFI_STAGE_MOUNTED || wifi_stage == WIFI_STAGE_WAIT)
        wifi_stage = WIFI_STAGE_GAVE_UP;
}

int vio_wifi_netdev_present(void)
{
    DIR *dir = opendir("/sys/class/net");
    struct dirent *entry;
    int present = 0;

    if(dir == NULL) return 0;
    while((entry = readdir(dir)) != NULL) {
        if(strncmp(entry->d_name, "wlan", 4) == 0) {
            present = 1;
            break;
        }
    }
    closedir(dir);
    return present;
}

int vio_wifi_bringup(void)
{
    if(wifi_stage != WIFI_STAGE_IDLE) return 0;

    /* Before anything else: claim the QRTR control port. The stock kernel has
     * no in-kernel name service, so icnss's WLFW lookup is otherwise posted
     * into the void. Binding here (before the modem boots) also re-arms the
     * HELLO handshake that carries the modem's service list to this port. */
    if(vio_qrtr_ns_start() != 0)
        vio_log("wifi: QRTR name service did not bind; icnss cannot see WLFW");

    vio_mkdir_p("/lib", 0755);
    vio_mkdir_p("/mnt/vendor", 0755);
    /* debugfs is where icnss would have put its state; it probed before any
     * filesystem existed ("Unable to create debugfs"), so mount it now and
     * let a rebind create the entries. */
    vio_mkdir_p("/sys/kernel/debug", 0755);
    (void)vio_mount_fs("debugfs", "/sys/kernel/debug", "debugfs", 0, "");

    /* p86 vendor: ext4, read-only. Without it even the qca_cld config file
     * symlink has no target. */
    wifi_vendor_ok = mount_partition(VENDOR_DEV, VENDOR_DIR, "ext4",
                                     MS_RDONLY, "") == 0 &&
                     file_exists("/vendor/etc/fstab.qcom");
    if(!wifi_vendor_ok)
        vio_log("wifi: vendor partition did not expose /vendor/etc/fstab.qcom");

    /* Bluetooth firmware is on its own read-only VFAT partition (p38). */
    (void)mount_bluetooth_firmware();

    /* p82 modem: vfat per fstab, with a read-only FAT fallback. */
    bring_up_modem_firmware();

    /* p74 persist: ext4. wlan_mac.bin is a symlink into this mount. */
    wifi_persist_ok = mount_partition(PERSIST_DEV, PERSIST_DIR, "ext4",
                                      MS_RDONLY, "") == 0 &&
                      dir_exists(PERSIST_DIR);

    if(wifi_modem_mounted) (void)link_firmware_tree();
    (void)set_firmware_paths();

    /* The modem needs the AP-side service registry and firmware TFTP server
     * before it boots, not after. These daemons block on QRTR until someone
     * asks, so starting them at bring-up is harmless and beats racing `wifi
     * up`. Their binaries are packaged in /sbin/qeaf when built. */
    (void)vio_host_services_start();

    snprintf(wifi_note, sizeof(wifi_note),
             "vendor=%d modem=%d mounted=%d copied=%d persist=%d links=%d",
             wifi_vendor_ok, wifi_modem_ok, wifi_modem_mounted,
             wifi_fw_copied, wifi_persist_ok, wifi_links);
    vio_log("wifi: bring-up mounts %s", wifi_note);

    wifi_stage = WIFI_STAGE_MOUNTED;
    wifi_t0 = vio_monotonic_ms();
    return 0;
}

void vio_wifi_poll(void)
{
    static uint64_t last_check;
    uint64_t now;

    if(wifi_stage == WIFI_STAGE_IDLE) {
        vio_wifi_bringup();
        return;
    }
    now = vio_monotonic_ms();
    /* sysfs walks are cheap but not free; four times a second is plenty. */
    if(now - last_check < 250) return;
    last_check = now;

    if(wifi_stage == WIFI_STAGE_MOUNTED) {
        if(now - wifi_t0 < 600) return;
        /* Device nodes can appear after the first mount attempt on mainline;
         * retry the firmware mounts instead of staying half-bricked. */
        if(!wifi_modem_mounted && wifi_mount_retries < 8 &&
           now - wifi_t0 > 2000 * (wifi_mount_retries + 1)) {
            wifi_mount_retries++;
            bring_up_modem_firmware();
            if(wifi_modem_mounted) {
                (void)link_firmware_tree();
                (void)set_firmware_paths();
            }
        }
        if(vio_wifi_netdev_present()) {
            wifi_stage = WIFI_STAGE_UP;
            vio_log("wifi: wlan netdev already present before kick");
            return;
        }
        if(!wifi_modem_opt_in) {
            /* The partitions and firmware links are done; the modem is not
             * started. Starting it without the vendor service layer that
             * runs beside it makes the SoC reset, so it takes `wifi up`. */
            snprintf(wifi_note, sizeof(wifi_note),
                     "modem not started (opt-in; `wifi up` boots it)");
            return;
        }
        if(!wifi_modem_thread_started) {
            wifi_modem_thread_started = 1;
            if(pthread_create(&wifi_modem_thread, NULL, modem_thread, NULL) != 0) {
                vio_log("wifi: cannot start modem thread: %s",
                        strerror(errno));
                wifi_modem_open_rc = -errno;
                wifi_modem_thread_done = 1;
            }
            else pthread_detach(wifi_modem_thread);
        }
        wifi_stage = WIFI_STAGE_WAIT;
        wifi_t_kick = now;
        return;
    }

    if(wifi_stage == WIFI_STAGE_WAIT) {
        if(vio_wifi_netdev_present()) {
            char mac[32] = "";
            wifi_stage = WIFI_STAGE_UP;
            snprintf(wifi_note, sizeof(wifi_note),
                     "wlan netdev up after %ums",
                     (unsigned)(now - wifi_t_kick));
            if(read_text("/sys/class/net/wlan0/address", mac, sizeof(mac)) > 0)
                vio_log("wifi: wlan0 up, mac %s", mac);
            else
                vio_log("wifi: wlan0 up");
            return;
        }
        /* The hdd/icnss probe ran at kernel init with no firmware filesystem.
         * Do NOT unbind it: icnss keeps a global "already initialized" state
         * and refuses to probe again (-17), which leaves the stack dead. A
         * bind is only attempted when the device is unbound for some other
         * reason; the report carries the evidence either way. */
        if(now - wifi_t_kick > 6000 && !wifi_icnss_bound) {
            wifi_icnss_bound = 1;
            (void)bind_platform_device("icnss", "icnss");
        }
        if(now - wifi_t_kick > 9000 && !wifi_recover_tried) {
            wifi_recover_tried = 1;
            if(write_text("/sys/module/wlan/parameters/recover", "1") == 0)
                vio_log("wifi: wrote wlan recover=1");
            else
                vio_log("wifi: wlan recover param absent (%s)",
                        strerror(errno));
        }
        if(now - wifi_t_kick > 12000 && !wifi_modem_rebound) {
            wifi_modem_rebound = 1;
            (void)bind_platform_device("4080000", "pil");
            (void)bind_platform_device("4080000", "q6v5");
            (void)bind_platform_device("mss", "pil");
        }
        if(wifi_modem_thread_done && !wifi_modem_started) {
            wifi_modem_started = wifi_modem_open_rc > 0;
            vio_log("wifi: modem thread done, rc=%d", wifi_modem_open_rc);
            if(wifi_modem_started) {
                /* The modem is up: now the WLFW handshake can start. It will
                 * copy the per-unit BDF/regdb from persist and drive
                 * /dev/wlan; wlan0 appears only if the firmware agrees. */
                vio_host_services_start_wlan();
            }
        }
        if(now - wifi_t_kick > 60000) {
            wifi_stage = WIFI_STAGE_GAVE_UP;
            snprintf(wifi_note, sizeof(wifi_note),
                     "no wlan netdev %ums after modem start (modem rc=%d)",
                     (unsigned)(now - wifi_t_kick), wifi_modem_open_rc);
            vio_log("wifi: %s (vendor=%d modem=%d copied=%d links=%d)",
                    wifi_note, wifi_vendor_ok, wifi_modem_ok, wifi_fw_copied,
                    wifi_links);
            wifi_t_kick = now;
        }
        return;
    }

    /* GAVE_UP is not final: the modem can finish loading after the first
     * attempt and the icnss bind can race the firmware files. Retry every
     * 30 s, five times, and keep the count in the report - a failed bring-up
     * must not need a reboot to try again. */
    if(wifi_stage == WIFI_STAGE_GAVE_UP) {
        if(wifi_modem_opt_in && wifi_retries < 5 &&
           now - wifi_t_kick >= 30000) {
            wifi_retries++;
            wifi_t_kick = now;
            wifi_stage = WIFI_STAGE_WAIT;
            wifi_icnss_bound = 0;
            wifi_modem_rebound = 0;
            wifi_recover_tried = 0;
            if(wifi_modem_thread_started && wifi_modem_open_rc <= 0) {
                wifi_modem_thread_started = 0;
                wifi_modem_thread_done = 0;
            }
            snprintf(wifi_note, sizeof(wifi_note),
                     "retry %u/5 after a failed kick", wifi_retries);
            vio_log("wifi: %s", wifi_note);
        }
        return;
    }

    /* UP/GAVE_UP: only notice a later removal; never resurrect silently. */
    if(wifi_stage == WIFI_STAGE_UP && !vio_wifi_netdev_present()) {
        wifi_stage = WIFI_STAGE_WAIT;
        wifi_t_kick = now;
        wifi_icnss_bound = 0;
        wifi_modem_rebound = 0;
        snprintf(wifi_note, sizeof(wifi_note), "wlan netdev disappeared");
        vio_log("wifi: wlan netdev disappeared; waiting for it to return");
    }
}

const char *vio_wifi_state_text(void)
{
    if(wifi_stage == WIFI_STAGE_UP) return "Đã có wlan0";
    if(wifi_stage == WIFI_STAGE_GAVE_UP) return "Không có wlan0";
    if(wifi_stage == WIFI_STAGE_IDLE) return "Chưa khởi động";
    return "Đang nạp firmware";
}

/* Append helper: never overflows, always NUL-terminates. */
static void addf(char *out, int cap, const char *fmt, ...)
{
    int used;
    va_list args;

    if(out == NULL || cap <= 0) return;
    used = (int)strlen(out);
    if(used >= cap - 1) return;
    va_start(args, fmt);
    vsnprintf(out + used, (size_t)(cap - used), fmt, args);
    va_end(args);
}

void vio_wifi_report(char *out, int cap)
{
    char value[256];
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    int shown = 0;

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';

    addf(out, cap, "wifi bring: stage=%d vendor=%d modem=%d mounted=%d "
         "copied=%d files=%d persist=%d links=%d wlan=%d modem_started=%d "
         "retries=%u\n",
         wifi_stage, wifi_vendor_ok, wifi_modem_ok, wifi_modem_mounted,
         wifi_fw_copied, wifi_fw_files, wifi_persist_ok, wifi_links,
         vio_wifi_netdev_present(), wifi_modem_started, wifi_retries);
    {
        char svc[512];
        vio_host_services_report(svc, (int)sizeof(svc));
        addf(out, cap, "%s", svc);
    }
    addf(out, cap, "wifi note: %s\n", wifi_note);
    addf(out, cap, "wifi modem: fd=%d rc=%d thread_done=%d\n",
         wifi_modem_fd, wifi_modem_open_rc, wifi_modem_thread_done);
    {
        char nsrep[1024];
        vio_qrtr_ns_report(nsrep, (int)sizeof(nsrep));
        addf(out, cap, "%s", nsrep);
    }
    addf(out, cap, "wifi p82: %s\n", wifi_p82_info);
    if(wifi_fw_copied)
        addf(out, cap, "wifi fw copied: %d files, %llu KiB\n",
             wifi_fw_files, wifi_fw_bytes / 1024);

    if(read_text("/sys/module/firmware_class/parameters/path", value,
                 sizeof(value)) > 0)
        addf(out, cap, "wifi firmware_class.path: %s\n", value);
    else
        addf(out, cap, "wifi firmware_class.path: unreadable\n");
    if(read_text("/sys/module/wlan/parameters/fwpath", value,
                 sizeof(value)) > 0)
        addf(out, cap, "wifi wlan fwpath: %s\n", value);
    else
        addf(out, cap, "wifi wlan fwpath: unreadable\n");

    dir = opendir("/sys/module/wlan/parameters");
    if(dir != NULL) {
        int shown = 0;
        addf(out, cap, "wifi wlan params:");
        while((entry = readdir(dir)) != NULL) {
            char path[320];
            char param[96] = "";
            if(entry->d_name[0] == '.') continue;
            if(shown >= 10) break;
            snprintf(path, sizeof(path), "/sys/module/wlan/parameters/%s",
                     entry->d_name);
            if(read_text(path, param, sizeof(param)) < 0) continue;
            addf(out, cap, " %s=%s", entry->d_name, param);
            shown++;
        }
        closedir(dir);
        addf(out, cap, "\n");
    }

    addf(out, cap, "wifi boot:");
    {
        static const char *const names[] = {"modem", "adsp", "cdsp", "slpi"};
        for(unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            char path[96];
            char state[32] = "";
            snprintf(path, sizeof(path), "/sys/kernel/boot_%s/boot", names[i]);
            if(read_text(path, state, sizeof(state)) >= 0)
                addf(out, cap, " %s=%s", names[i], state);
        }
    }
    addf(out, cap, "\n");

    dir = opendir("/sys/class/remoteproc");
    addf(out, cap, "wifi rproc:");
    if(dir == NULL) addf(out, cap, " absent");
    else {
        while((entry = readdir(dir)) != NULL) {
            char path[320];
            char name[64] = "?";
            char state[64] = "?";
            if(entry->d_name[0] == '.') continue;
            snprintf(path, sizeof(path), "/sys/class/remoteproc/%s/name",
                     entry->d_name);
            (void)read_text(path, name, sizeof(name));
            snprintf(path, sizeof(path), "/sys/class/remoteproc/%s/state",
                     entry->d_name);
            (void)read_text(path, state, sizeof(state));
            addf(out, cap, " %s=%s/%s", entry->d_name, name, state);
        }
        closedir(dir);
    }
    addf(out, cap, "\n");

    dir = opendir(IMAGE_DIR);
    if(dir == NULL) {
        addf(out, cap, "wifi image: %s absent\n", IMAGE_DIR);
    }
    else {
        while((entry = readdir(dir)) != NULL) {
            if(entry->d_name[0] == '.') continue;
            count++;
            if(shown < 6) {
                addf(out, cap, "%s%s", shown ? ", " : "wifi image: ",
                     entry->d_name);
                shown++;
            }
        }
        closedir(dir);
        addf(out, cap, " (%d entries)\n", count);
    }

    /* The PIL image needs modem.mdt plus modem.bNN segments; name them so a
     * missing piece is visible instead of guessed. */
    dir = opendir(IMAGE_DIR);
    if(dir != NULL) {
        int pil = 0;
        addf(out, cap, "wifi pil files:");
        while((entry = readdir(dir)) != NULL) {
            if(entry->d_name[0] == '.') continue;
            if(strncmp(entry->d_name, "modem.", 6) != 0 &&
               strncmp(entry->d_name, "mba.", 4) != 0 &&
               strncmp(entry->d_name, "wlan", 4) != 0)
                continue;
            if(pil < 40) addf(out, cap, " %s", entry->d_name);
            pil++;
        }
        closedir(dir);
        if(pil > 40) addf(out, cap, " ...(%d)", pil);
        addf(out, cap, "\n");
    }

    if(read_text("/sys/class/net/wlan0/address", value, sizeof(value)) > 0)
        addf(out, cap, "wifi wlan0 mac: %s\n", value);
    else
        addf(out, cap, "wifi wlan0 mac: absent\n");
}

/* ---- sysfs census ----------------------------------------------------- */

static void list_dir(char *out, int cap, const char *label, const char *path,
                     int max)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int shown = 0;
    int total = 0;

    addf(out, cap, "%s:", label);
    if(dir == NULL) {
        addf(out, cap, " absent (%s)\n", strerror(errno));
        return;
    }
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') continue;
        total++;
        if(shown < max) addf(out, cap, " %s", entry->d_name);
        shown++;
    }
    closedir(dir);
    if(total > max) addf(out, cap, " ...(%d)", total);
    addf(out, cap, "\n");
}

/* Homologation-style diagnostic: every line is verified from live sysfs,
 * mounted files and the QRTR/service census. No inferred or mocked status. */
void vio_wifi_diag(char *out, int cap)
{
    char mounts[8192] = "";
    char qr[6000] = "";
    char subsys[8192] = "";
    char svc[2048] = "";
    DIR *dir;
    struct dirent *entry;
    int modem_state = 0;          /* 1 = ONLINE */
    char state[64] = "absent";
    int fw_mounted = 0, fw_image = 0;
    int rmt_ok = 0, tftp_ok = 0, binder_ok = 0, cnss_ok = 0;
    int wlfw_ok = 0, wlan0_ok = 0;
    const char *servers;

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';

    /* Step 1: modem firmware mount and the real WLAN DSP image. */
    (void)read_text("/proc/mounts", mounts, sizeof(mounts));
    fw_mounted = strstr(mounts, "/vendor/firmware_mnt") != NULL;
    fw_image = access("/vendor/firmware_mnt/image/wlanmdsp.mbn", F_OK) == 0 ||
               access("/lib/firmware/wlanmdsp.mbn", F_OK) == 0;
    addf(out, cap,
         "STEP1 firmware_mnt=%s wlanmdsp.mbn=%s mount_source=%s %s\n",
         fw_mounted ? "mounted" : "NOT_MOUNTED",
         fw_image ? "present" : "MISSING",
         strstr(mounts, "/dev/mmcblk0p82 ") ? "/dev/mmcblk0p82" : "unknown",
         (fw_mounted && fw_image) ? "VERIFIED" : "FAILED");

    /* Step 2: msm_subsys modem state, straight from the kernel. */
    dir = opendir("/sys/bus/msm_subsys/devices");
    if(dir != NULL) {
        while((entry = readdir(dir)) != NULL) {
            char base[320], path[400], name[64] = "";
            if(entry->d_name[0] == '.') continue;
            snprintf(base, sizeof(base), "/sys/bus/msm_subsys/devices/%s",
                     entry->d_name);
            snprintf(path, sizeof(path), "%s/name", base);
            if(read_text(path, name, sizeof(name)) > 0 &&
               strcmp(name, "modem") == 0) {
                snprintf(path, sizeof(path), "%s/state", base);
                (void)read_text(path, state, sizeof(state));
                if(strstr(state, "ONLINE") != NULL ||
                   strstr(state, "online") != NULL) modem_state = 1;
                break;
            }
        }
        closedir(dir);
    }
    addf(out, cap, "STEP2 modem_subsys state=%s %s\n", state,
         modem_state ? "VERIFIED" : "FAILED");

    /* Step 3: userspace stack from the live service registry. */
    vio_host_services_report(svc, (int)sizeof(svc));
    rmt_ok = strstr(svc, "rmtfs: running") != NULL;
    tftp_ok = strstr(svc, "tqftpserv: running") != NULL;
    binder_ok = strstr(svc, "binder: present") != NULL;
    cnss_ok = strstr(svc, "cnss-daemon: running") != NULL;
    addf(out, cap,
         "STEP3 rmtfs=%s tqftpserv=%s binder=%s cnss-daemon=%s %s\n",
         rmt_ok ? "running" : "MISSING",
         tftp_ok ? "running" : "MISSING",
         binder_ok ? "present" : "absent",
         cnss_ok ? "running" : "MISSING",
         (rmt_ok && tftp_ok && binder_ok && cnss_ok) ? "VERIFIED" : "FAILED");

    /* Step 4: WLFW 0x45 must be in the QRTR SERVERS table, not LOOKUPS. */
    vio_qrtr_ns_report(qr, (int)sizeof(qr));
    servers = strstr(qr, "qrtr-ns servers:");
    if(servers != NULL) {
        const char *end = strstr(servers, "qrtr-ns nodes:");
        size_t n = end != NULL ? (size_t)(end - servers) : strlen(servers);
        char table[2048];
        snprintf(table, sizeof(table), "%.*s", (int)n, servers);
        wlfw_ok = strstr(table, "0x45/") != NULL;
    }
    addf(out, cap, "STEP4 WLFW_0x45 server=%s %s\n",
         wlfw_ok ? "present" : "NOT_REGISTERED",
         wlfw_ok ? "VERIFIED" : "FAILED");

    /* Step 5: the driver must have created a real netdev. */
    wlan0_ok = access("/sys/class/net/wlan0", F_OK) == 0;
    addf(out, cap, "STEP5 wlan0=%s %s\n",
         wlan0_ok ? "present" : "absent",
         wlan0_ok ? "VERIFIED" : "FAILED");
    (void)subsys;
}


static void attr(char *out, int cap, const char *label, const char *path)
{
    char value[128] = "";

    if(read_text(path, value, sizeof(value)) >= 0)
        addf(out, cap, "  %s=%s\n", label, value);
    else
        addf(out, cap, "  %s: absent\n", label);
}

/* Every subsystem the kernel knows about, with its restart level and the
 * attributes that actually exist. This is the only place that says whether
 * the modem is running. */
void vio_wifi_sysdump(char *out, int cap)
{
    DIR *dir;
    struct dirent *entry;
    int shown = 0;

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';

    list_dir(out, cap, "wifi sys /sys/kernel", "/sys/kernel", 40);
    list_dir(out, cap, "wifi sys /sys/class", "/sys/class", 80);
    list_dir(out, cap, "wifi sys /sys/bus/msm_subsys/devices",
             "/sys/bus/msm_subsys/devices", 20);

    dir = opendir("/sys/bus/msm_subsys/devices");
    if(dir != NULL) {
        while((entry = readdir(dir)) != NULL && shown < 12) {
            char base[320];
            char path[400];
            char name[64] = "?";
            if(entry->d_name[0] == '.') continue;
            snprintf(base, sizeof(base), "/sys/bus/msm_subsys/devices/%s",
                     entry->d_name);
            snprintf(path, sizeof(path), "%s/name", base);
            (void)read_text(path, name, sizeof(name));
            addf(out, cap, "wifi subsys %s name=%s\n", entry->d_name, name);
            snprintf(path, sizeof(path), "%s/restart_level", base);
            attr(out, cap, "restart_level", path);
            snprintf(path, sizeof(path), "%s/state", base);
            attr(out, cap, "state", path);
            snprintf(path, sizeof(path), "%s/restart", base);
            attr(out, cap, "restart", path);
            snprintf(path, sizeof(path), "%s/shutdown", base);
            attr(out, cap, "shutdown", path);
            shown++;
        }
        closedir(dir);
    }

    list_dir(out, cap, "wifi sys mss attrs",
             "/sys/bus/platform/devices/4080000.qcom,mss", 30);
    attr(out, cap, "mss boot",
         "/sys/bus/platform/devices/4080000.qcom,mss/boot");
    attr(out, cap, "mss shutdown",
         "/sys/bus/platform/devices/4080000.qcom,mss/shutdown");
    attr(out, cap, "mss subsystem",
         "/sys/bus/platform/devices/4080000.qcom,mss/subsystem");
    attr(out, cap, "mss runtime",
         "/sys/bus/platform/devices/4080000.qcom,mss/power/runtime_status");
    list_dir(out, cap, "wifi sys pil-tz",
             "/sys/bus/platform/drivers/subsys-pil-tz", 30);
    list_dir(out, cap, "wifi sys boot_adsp", "/sys/kernel/boot_adsp", 12);
    list_dir(out, cap, "wifi sys boot_cdsp", "/sys/kernel/boot_cdsp", 12);
    list_dir(out, cap, "wifi sys boot_slpi", "/sys/kernel/boot_slpi", 12);
    attr(out, cap, "adsp boot", "/sys/kernel/boot_adsp/boot");
    attr(out, cap, "wifi sys wlan initstate", "/sys/module/wlan/initstate");
    attr(out, cap, "wifi sys icnss initstate", "/sys/module/icnss/initstate");
    list_dir(out, cap, "wifi sys /sys/class/remoteproc",
             "/sys/class/remoteproc", 12);
    list_dir(out, cap, "wifi sys /sys/class/subsys", "/sys/class/subsys", 12);
    list_dir(out, cap, "wifi sys /sys/class/misc", "/sys/class/misc", 80);
    list_dir(out, cap, "wifi sys /sys/class/uio", "/sys/class/uio", 20);
    list_dir(out, cap, "wifi sys /sys/class/rmtfs", "/sys/class/rmtfs", 20);
    list_dir(out, cap, "wifi persist root", "/mnt/vendor/persist", 80);
    list_dir(out, cap, "wifi persist wlan", "/mnt/vendor/persist/wlan", 60);
    list_dir(out, cap, "wifi persist wifi", "/mnt/vendor/persist/wifi", 60);
    list_dir(out, cap, "wifi persist qca", "/mnt/vendor/persist/wlan/qca_cld", 60);
    {
        DIR *u = opendir("/sys/class/uio");
        struct dirent *e;
        while(u && (e = readdir(u)) != NULL) {
            char p[200], nm[64] = "";
            if(strncmp(e->d_name, "uio", 3) != 0) continue;
            snprintf(p, sizeof(p), "/sys/class/uio/%s/name", e->d_name);
            if(read_text(p, nm, sizeof(nm)) == 0) {
                addf(out, cap, "wifi uio %s name=%s\n", e->d_name, nm);
                if(strcmp(nm, "rmtfs") == 0) {
                    snprintf(p, sizeof(p), "/sys/class/uio/%s/dev",
                             e->d_name);
                    attr(out, cap, "rmtfs uio dev", p);
                    snprintf(p, sizeof(p),
                             "/sys/class/uio/%s/maps/map0/size",
                             e->d_name);
                    attr(out, cap, "rmtfs uio map0 size", p);
                }
            }
        }
        if(u) closedir(u);
    }
    list_dir(out, cap, "wifi sys rmtfs mem1",
             "/sys/class/rmtfs/qcom_rmtfs_mem1", 20);
    attr(out, cap, "rmtfs mem1 dev",
         "/sys/class/rmtfs/qcom_rmtfs_mem1/dev");
    attr(out, cap, "rmtfs mem1 phys_addr",
         "/sys/class/rmtfs/qcom_rmtfs_mem1/phys_addr");
    attr(out, cap, "rmtfs mem1 size",
         "/sys/class/rmtfs/qcom_rmtfs_mem1/size");
    list_dir(out, cap, "wifi sys rmtfs attrs",
             "/sys/class/misc/qcom_rmtfs_mem0", 20);
    list_dir(out, cap, "wifi sys misc rmtfs", "/sys/class/misc/rmtfs", 20);
    attr(out, cap, "misc rmtfs dev", "/sys/class/misc/rmtfs/dev");
    attr(out, cap, "misc rmtfs phys", "/sys/class/misc/rmtfs/phys_addr");
    attr(out, cap, "misc rmtfs size", "/sys/class/misc/rmtfs/size");
    attr(out, cap, "rmtfs phys_addr",
         "/sys/class/misc/qcom_rmtfs_mem0/device/phys_addr");
    attr(out, cap, "rmtfs size",
         "/sys/class/misc/qcom_rmtfs_mem0/device/size");
    list_dir(out, cap, "wifi sys /sys/bus/qrtr/devices",
             "/sys/bus/qrtr/devices", 12);
    list_dir(out, cap, "wifi sys /sys/kernel/debug/qrtr",
             "/sys/kernel/debug/qrtr", 12);
    list_dir(out, cap, "wifi sys /dev", "/dev", 40);
}
