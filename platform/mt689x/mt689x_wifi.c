/* SPDX-License-Identifier: MIT */
/* platform/mt689x/mt689x_wifi.c - Wi-Fi state for the QEAFRESCUE `wifi` verb.
 *
 * MediaTek MT6893 keeps Wi-Fi behind the conninfra/WLAN driver, and QEAF only
 * mounts what is needed to let that driver read its firmware. This file is the
 * bridge the shared rescue command loop expects: it reports what the kernel
 * exposes right now (module, firmware directory, netdev, operstate) and never
 * claims a network that is not there. There is no WPA supplicant in the image,
 * so even a created wlan0 is reported as "down, unmanaged".
 */
#include "wifi_bringup.h"
#include "mt689x.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char state[160] = "wifi: not started";

/* MT689x has no Qualcomm modem PIL to start/stop. The shared rescue verbs
 * still accept the commands, so provide honest no-ops instead of leaving an
 * undefined symbol in the userspace image. */
void vio_wifi_modem_start(void)
{
    vio_log("wifi: no Qualcomm modem on MT689x; start ignored");
}

void vio_wifi_modem_stop(void)
{
    vio_log("wifi: no Qualcomm modem on MT689x; stop ignored");
}

static int path_exists(const char *p) { return access(p, F_OK) == 0; }

static int module_loaded(const char *name)
{
    char p[128];
    snprintf(p, sizeof(p), "/sys/module/%s", name);
    return path_exists(p);
}

/* The MT6893 connectivity stack is built into the Image (MTK_COMBO is bool and
 * the Makefiles use obj-y), so /sys/module says nothing about it. A registered
 * platform driver under /sys/bus/platform/drivers is the real evidence:
 * "mtk_conninfra" (conninfra/platform/consys_hw.c) and "wlan"
 * (wlan_drv_gen4m/os/linux/hif/axi/axi.c). */
static int driver_registered(const char *name)
{
    char p[128];
    snprintf(p, sizeof(p), "/sys/bus/platform/drivers/%s", name);
    return path_exists(p);
}

static int conninfra_present(void)
{
    return driver_registered("mtk_conninfra") || module_loaded("conninfra") ||
           module_loaded("mtk_conninfra");
}

static int wlandrv_present(void)
{
    return driver_registered("wlan") || module_loaded("wlan_drv_gen4m") ||
           module_loaded("wlan");
}

static int btif_present(void)
{
    return driver_registered("mtkbt_btif") ||
           driver_registered("mtk_btif") || module_loaded("btmtk_btif");
}

static int find_wlan(char *out, size_t cap)
{
    DIR *d = opendir("/sys/class/net");
    struct dirent *e;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "wlan", 4) == 0) {
            snprintf(out, cap, "%s", e->d_name);
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    return -1;
}

static int firmware_dir(char *out, size_t cap)
{
    static const char *const dirs[] = {
        "/vendor/firmware/mediatek/mt6893",
        "/lib/firmware/mediatek/mt6893",
        "/vendor/firmware/mediatek",
        "/lib/firmware/mediatek",
    };
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (!path_exists(dirs[i])) continue;
        DIR *d = opendir(dirs[i]);
        struct dirent *e;
        if (!d) continue;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            snprintf(out, cap, "%s", dirs[i]);
            closedir(d);
            return 0;
        }
        closedir(d);
    }
    return -1;
}

int vio_wifi_bringup(void)
{
    /* The conninfra/WLAN modules are loaded by mt689x_bringup(); nothing is
     * mounted here because firmware already lives in the image. */
    vio_wifi_poll();
    return 0;
}

void vio_wifi_poll(void)
{
    char ifname[32], fw[160], buf[32];
    int iface, conninfra, wlandrv;

    iface = find_wlan(ifname, sizeof(ifname)) == 0;
    conninfra = conninfra_present();
    wlandrv = wlandrv_present();
    if (firmware_dir(fw, sizeof(fw)) != 0) fw[0] = '\0';

    if (iface) {
        snprintf(buf, sizeof(buf), "/sys/class/net/%s/operstate", ifname);
        char oper[24] = "unknown";
        if (vio_read_file(buf, oper, sizeof(oper)) > 0)
            oper[strcspn(oper, "\r\n")] = '\0';
        snprintf(state, sizeof(state), "wifi: %s %s (no supplicant)", ifname,
                 oper);
    }
    else if (wlandrv || conninfra) {
        snprintf(state, sizeof(state),
                 "wifi: driver loaded, no wlan netdev%s",
                 fw[0] ? "" : " (firmware dir missing)");
    }
    else {
        snprintf(state, sizeof(state), "wifi: driver not loaded%s",
                 fw[0] ? "" : ", firmware dir missing");
    }
}

const char *vio_wifi_state_text(void)
{
    vio_wifi_poll();
    return state;
}

void vio_wifi_report(char *out, int cap)
{
    char ifname[32], fw[160];
    int n = 0;

    if (!out || cap <= 0) return;
    out[0] = '\0';
    n += snprintf(out + n, (size_t)(cap - n), "MediaTek MT6893 Wi-Fi\n");
    n += snprintf(out + n, (size_t)(cap - n), "conninfra: %s\n",
                  conninfra_present() ? "registered" : "not registered");
    n += snprintf(out + n, (size_t)(cap - n), "wlan driver: %s\n",
                  wlandrv_present() ? "registered" : "not registered");
    n += snprintf(out + n, (size_t)(cap - n), "firmware dir: %s\n",
                  firmware_dir(fw, sizeof(fw)) == 0 ? fw : "(none)");
    n += snprintf(out + n, (size_t)(cap - n), "netdev: %s\n",
                  find_wlan(ifname, sizeof(ifname)) == 0 ? ifname : "(none)");
    n += snprintf(out + n, (size_t)(cap - n),
                  "state: %s\n", state);
    if (n < cap)
        snprintf(out + n, (size_t)(cap - n),
                 "note: QEAF has no WPA supplicant; association is not "
                 "attempted and is not claimed.\n");
}

void vio_wifi_sysdump(char *out, int cap)
{
    int n = 0;
    if (!out || cap <= 0) return;
    out[0] = '\0';
    n += snprintf(out + n, (size_t)(cap - n), "modules/drivers:\n");
    n += snprintf(out + n, (size_t)(cap - n), "  mtk_conninfra    %s\n",
                  conninfra_present() ? "registered" : "-");
    n += snprintf(out + n, (size_t)(cap - n), "  wlan (axi)      %s\n",
                  wlandrv_present() ? "registered" : "-");
    n += snprintf(out + n, (size_t)(cap - n), "  mtkbt_btif      %s\n",
                  btif_present() ? "registered" : "-");
    n += snprintf(out + n, (size_t)(cap - n), "dri/net nodes:\n");
    n += snprintf(out + n, (size_t)(cap - n), "  /sys/class/net %s\n",
                  path_exists("/sys/class/net/wlan0") ? "wlan0" : "(no wlan0)");
    n += snprintf(out + n, (size_t)(cap - n), "  /dev/stpbt %s\n",
                  path_exists("/dev/stpbt") ? "present" : "-");
}

void vio_wifi_diag(char *out, int cap)
{
    if(out == NULL || cap <= 0) return;
    vio_wifi_report(out, cap);
}

static int firmware_file_count(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int count = 0;

    if(dir == NULL) return 0;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') continue;
        count++;
    }
    closedir(dir);
    return count;
}

int vio_wifi_fw_report(char *out, size_t cap)
{
    static const char *const dirs[] = {
        "/vendor/firmware/mediatek/mt6893",
        "/lib/firmware/mediatek/mt6893",
        "/vendor/firmware/mediatek",
        "/lib/firmware/mediatek",
    };
    size_t used = 0;
    int found = 0;

    if(out == NULL || cap == 0) return -1;
    out[0] = '\0';
    for(unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        int count = firmware_file_count(dirs[i]);
        int n;
        if(count == 0) continue;
        found = 1;
        if(used + 1u >= cap) break;
        n = snprintf(out + used, cap - used,
                     "fw check: %s: %d file(s) present\n", dirs[i], count);
        if(n < 0 || (size_t)n >= cap - used) {
            out[cap - 1u] = '\0';
            used = cap - 1u;
            break;
        }
        used += (size_t)n;
    }
    if(!found && cap > 1u)
        snprintf(out, cap, "fw check: no MediaTek firmware directory populated\n");
    if(found && used + 1u < cap)
        snprintf(out + used, cap - used,
                 "fw check: presence only; kernel/driver validation not performed\n");
    return found ? 0 : -1;
}
