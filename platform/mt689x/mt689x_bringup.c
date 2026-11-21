/* SPDX-License-Identifier: MIT */
/* platform/mt689x/mt689x_bringup.c - UFS firmware, modules and sensors.
 *
 * The MT6893 board file (mt6893-xiaomi-chopin.dts) asks for firmware under
 * /vendor/firmware and puts Wi-Fi/BT behind the downstream connectivity
 * stack, which is modular in the QEAF build. This file mounts what is needed
 * to read those blobs, then loads exactly the modules the QEAF kernel
 * fragment builds. Nothing is written to the device.
 */
#include "mt689x_bringup.h"
#include "mt689x.h"

#include "hal/include/display.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MODULE_DIR "/lib/modules/mt689x"

/* Load order matters. Only the ALSA stack is modular: chopin_defconfig pins
 * SND=m, so SND_SOC and everything that hangs off it are modules too. The
 * connectivity stack (conninfra, wlan_drv_gen4m, btif/bt_drv_mt66xx) and the
 * SCP sensor hub are built into the Image because MTK_COMBO/MTK_BTIF/
 * MTK_SENSORHUB are bool and the Makefiles use obj-y for them, so they have
 * no .ko and must not be listed here.
 *
 * Names come from the kernel Makefiles:
 *   sound/soc/Makefile                  -> snd-soc-core.ko
 *   sound/soc/codecs/Makefile           -> snd-soc-cs35l41{,-lib,-i2c}.ko,
 *                                          snd-soc-mt6359.ko, mt6359-accdet.ko
 *   sound/soc/mediatek/mt8192/Makefile  -> snd-soc-mt8192-afe.ko,
 *                                          mt6893-mt6359.ko
 * The codec and the AFE must exist before the machine card binds them. */
static const char *const kModules[] = {
    "snd-soc-core.ko",
    "snd-soc-cs35l41-lib.ko",
    "snd-soc-cs35l41.ko",
    "snd-soc-cs35l41-i2c.ko",
    "snd-soc-mt6359.ko",
    "mt6359-accdet.ko",
    "snd-soc-mt8192-afe.ko",
    "mt6893-mt6359.ko",
    /* chopin_defconfig pins DRM_PANFROST=m, so the Mali render node only
     * exists once this is loaded. Panthor is for CSF parts and is not used
     * on MT6893 (Mali-G77). */
    "panfrost.ko",
    NULL,
};

static int file_readable(const char *p) { return access(p, R_OK) == 0; }

static int dir_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int mount_if_dir(const char *source, const char *target,
                        const char *type)
{
    if (!file_readable(source)) return -1;
    vio_mkdir_p(target, 0755);
    if (mount(source, target, type, MS_RDONLY, NULL) != 0) {
        vio_log("bringup: mount %s (%s) -> %s failed: %s", source, type,
                target, strerror(errno));
        return -1;
    }
    vio_log("bringup: %s mounted read-only at %s", source, target);
    return 0;
}

/* Newer vendor images keep vendor inside super, but some ship a
 * real vendor partition and every stock dump has the by-name link. Try the
 * plain names first, then nothing: the packaged firmware copy is the normal
 * path for QEAF, so a missing vendor mount is a degraded boot, not a failure.
 */
static int mount_vendor(void)
{
    static const char *const nodes[] = {
        "/dev/block/by-name/vendor",
        "/dev/block/by-name/cust",
        NULL,
    };
    static const char *const types[] = { "ext4", "erofs", "f2fs" };

    for (int n = 0; nodes[n]; n++) {
        for (unsigned t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
            if (mount_if_dir(nodes[n], "/vendor", types[t]) == 0) return 0;
        }
    }
    return -1;
}

static int find_firmware_dir(char *out, size_t cap)
{
    static const char *const dirs[] = {
        "/vendor/firmware/mediatek/mt6893",
        "/vendor/firmware/mediatek",
        "/lib/firmware/mediatek/mt6893",
        "/lib/firmware/mediatek",
        "/firmware/mediatek/mt6893",
    };
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (!dir_exists(dirs[i])) continue;
        DIR *d = opendir(dirs[i]);
        if (!d) continue;
        int any = 0;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            any = 1;
            break;
        }
        closedir(d);
        if (!any) continue;
        snprintf(out, cap, "%s", dirs[i]);
        return 0;
    }
    return -1;
}

int mtk_load_module(const char *file)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", MODULE_DIR, file);
    if (!file_readable(path)) return -1;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    int rc = (int)syscall(SYS_finit_module, fd, "", 0);
    int err = errno;
    close(fd);
    /* EEXIST means an earlier load (or a built-in) already owns it. */
    if (rc != 0 && err == EEXIST) return 0;
    if (rc != 0)
        vio_log("bringup: module %s: %s", file, strerror(err));
    return rc == 0 ? 0 : -1;
}

static void set_firmware_path(const char *dir)
{
    /* firmware_class.path takes one directory. Point it at the parent so the
     * driver's request for "mediatek/mt6893/..." resolves. */
    char parent[160];
    snprintf(parent, sizeof(parent), "%s", dir);
    char *slash = strrchr(parent, '/');
    if (slash && strcmp(slash + 1, "mt6893") == 0) *slash = '\0';
    if (vio_write_file("/sys/module/firmware_class/parameters/path", parent) == 0)
        vio_log("bringup: firmware_class.path = %s", parent);
    else
        vio_log("bringup: firmware_class.path not writable; drivers use "
                "/lib/firmware");
}

static int find_exec(const char *const *cands, char *out, size_t cap)
{
    for (int i = 0; cands[i]; i++) {
        if (access(cands[i], X_OK) == 0) {
            snprintf(out, cap, "%s", cands[i]);
            return 0;
        }
    }
    return -1;
}

void mtk_bringup(MtkBringup *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    out->vendor_mounted = mount_vendor() == 0;
    if (find_firmware_dir(out->firmware_dir, sizeof(out->firmware_dir)) == 0) {
        out->firmware_ready = 1;
        set_firmware_path(out->firmware_dir);
        vio_log("bringup: MT6893 firmware at %s", out->firmware_dir);
    }
    else {
        vio_log("bringup: no mediatek/mt6893 firmware dir; Wi-Fi/BT stay "
                "blocked until the stock vendor blobs are packaged");
    }

    for (int i = 0; kModules[i]; i++) {
        if (access(MODULE_DIR, F_OK) != 0) break;
        char path[320];
        snprintf(path, sizeof(path), "%s/%s", MODULE_DIR, kModules[i]);
        if (access(path, F_OK) != 0) continue;
        if (mtk_load_module(kModules[i]) == 0) {
            out->modules_loaded++;
            if (strstr(kModules[i], "mt6893-mt6359")) out->audio_module = 1;
            if (strstr(kModules[i], "sensorhub")) out->sensor_module = 1;
        }
        else {
            out->modules_failed++;
        }
    }
    vio_log("bringup: %d module(s) loaded, %d rejected", out->modules_loaded,
            out->modules_failed);

    {
        static const char *const denial[] = {
            "/usr/bin/denial", "/usr/bin/denial-shell", "/opt/denial/denial",
            NULL,
        };
        if (find_exec(denial, out->denial_path, sizeof(out->denial_path)) == 0) {
            out->denial_present = 1;
            vio_log("bringup: Denial compositor found at %s", out->denial_path);
        }
        else {
            vio_log("bringup: no Denial binary; QEAF shell is the session");
        }
    }
}

/* The SCP sensor hub names its IIO device after the physical part, so scan
 * for the first device that exposes in_accel_x_raw instead of assuming
 * iio:device0. */
static int accel_dir(char *out, size_t cap)
{
    static const char *const roots[] = { "/sys/bus/iio/devices",
                                         "/sys/class/iio" };
    for (unsigned r = 0; r < sizeof(roots) / sizeof(roots[0]); r++) {
        DIR *d = opendir(roots[r]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            char probe[320];
            if (strncmp(e->d_name, "iio:device", 10) != 0) continue;
            snprintf(probe, sizeof(probe), "%s/%s/in_accel_x_raw", roots[r],
                     e->d_name);
            if (access(probe, R_OK) == 0) {
                snprintf(out, cap, "%s/%s", roots[r], e->d_name);
                closedir(d);
                return 0;
            }
        }
        closedir(d);
    }
    return -1;
}

int mtk_accel_available(void)
{
    char dir[256];
    return accel_dir(dir, sizeof(dir)) == 0;
}

void mtk_sensor_poll(void)
{
    char dir[256], path[360], buf[32];
    int x, y;

    if (!display_auto_rotate()) return;
    if (accel_dir(dir, sizeof(dir)) != 0) return;
    snprintf(path, sizeof(path), "%s/in_accel_x_raw", dir);
    if (vio_read_file(path, buf, sizeof(buf)) <= 0) return;
    x = atoi(buf);
    snprintf(path, sizeof(path), "%s/in_accel_y_raw", dir);
    if (vio_read_file(path, buf, sizeof(buf)) <= 0) return;
    y = atoi(buf);
    /* display_feed_sensor wants milli-g; the sensor's raw unit is close
     * enough for orientation thresholds and is scaled by 1000/16384 for a
     * +/-2 g part when in_accel_scale is present. */
    display_feed_sensor(x * 1000 / 16384, y * 1000 / 16384);
}
