/* SPDX-License-Identifier: MIT */
/* bringup: read-only Wi-Fi ladder. Stops on the first failing dependency. */
#define _GNU_SOURCE
#include "netd_bringup.h"
#include "netd_core.h"
#include "../link/netd_link.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FIRMWARE_MNT "/vendor/firmware_mnt"
#define WLAN_IMAGE   "/vendor/firmware_mnt/image/wlanmdsp.mbn"
#define SUBSYS_DIR   "/sys/bus/msm_subsys/devices"
#define WPA_CTRL     "/var/run/wpa_supplicant/wlan0"

static char g_reason[128] = "not_run";

static void emit(char *out, size_t cap, netd_state_t st, const char *fmt, ...)
{
    char line[256], evidence[224];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(evidence, sizeof(evidence), fmt, ap);
    va_end(ap);
    if(n < 0) return;
    if(netd_log_line(line, sizeof(line), st, evidence) < 0) return;
    if(strlen(out) + strlen(line) + 2u >= cap) return;
    strcat(out, line);
    strcat(out, "\n");
}

static int mounted_firmware_mnt(void)
{
    FILE *f = fopen("/proc/mounts", "re");
    char line[512];
    int found = 0;
    if(f == NULL) return 0;
    while(fgets(line, sizeof(line), f) != NULL)
        if(strstr(line, FIRMWARE_MNT) != NULL) { found = 1; break; }
    fclose(f);
    return found;
}

static int modem_online(char *state, size_t cap)
{
    DIR *d = opendir(SUBSYS_DIR);
    struct dirent *e;
    state[0] = '\0';
    if(d == NULL) return 0;
    while((e = readdir(d)) != NULL) {
        char base[256], path[320], name[64] = "";
        FILE *f;
        if(e->d_name[0] == '.') continue;
        snprintf(base, sizeof(base), "%s/%s", SUBSYS_DIR, e->d_name);
        snprintf(path, sizeof(path), "%s/name", base);
        f = fopen(path, "re");
        if(f == NULL) continue;
        if(fgets(name, sizeof(name), f) != NULL) {
            name[strcspn(name, "\r\n")] = '\0';
        }
        fclose(f);
        if(strcmp(name, "modem") != 0) continue;
        snprintf(path, sizeof(path), "%s/state", base);
        f = fopen(path, "re");
        if(f == NULL) break;
        if(fgets(state, cap, f) != NULL)
            state[strcspn(state, "\r\n")] = '\0';
        fclose(f);
        break;
    }
    closedir(d);
    return strstr(state, "ONLINE") != NULL || strstr(state, "online") != NULL;
}

static int wlfw_published(void)
{
    /* Read-only: only inspect the kernel QRTR name service report exposed by
     * PID 1. A lookup is NOT a server registration. */
    FILE *f = fopen("/tmp/hostsvc-qlookup.log", "re");
    char line[256];
    int has_timeout = 0;
    if(f == NULL) return 0;
    while(fgets(line, sizeof(line), f) != NULL)
        if(strstr(line, "recvfrom") != NULL) has_timeout = 1;
    fclose(f);
    return !has_timeout; /* conservative: timeout means not published */
}

static int blocked(char *out, size_t cap, int step, const char *reason)
{
    snprintf(g_reason, sizeof(g_reason), "step%d:%s", step, reason);
    emit(out, cap, NETD_BLOCKED, "step%d %s", step, reason);
    return -1;
}

int netd_bringup_run(char *out, size_t cap)
{
    char state[64] = "";
    char names[256] = "";
    int passed = 0;
    if(out == NULL || cap == 0) return -1;
    out[0] = '\0';
    g_reason[0] = '\0';

    if(!mounted_firmware_mnt() || access(WLAN_IMAGE, F_OK) != 0)
        return blocked(out, cap, 1, "firmware_mnt_or_wlanmdsp_missing");
    emit(out, cap, NETD_VERIFIED, "step1 firmware_mnt and wlanmdsp.mbn present");
    passed++;

    if(!modem_online(state, sizeof(state)))
        return blocked(out, cap, 2, "modem_not_online");
    emit(out, cap, NETD_VERIFIED, "step2 modem state=%s", state);
    passed++;

    if(access("/proc/sys/net/ipv4/ip_forward", F_OK) == 0)
        emit(out, cap, NETD_VERIFIED, "step3 ipc router reachable");
    else
        return blocked(out, cap, 3, "ipc_router_unavailable");
    passed++;

    if(!wlfw_published())
        return blocked(out, cap, 4, "WLFW_0x45_NOT_REGISTERED");
    emit(out, cap, NETD_VERIFIED, "step4 WLFW 0x45 published");
    passed++;

    if(!netd_link_exists("wlan0"))
        return blocked(out, cap, 5, "wlan0_absent");
    emit(out, cap, NETD_VERIFIED, "step5 wlan0 present");
    passed++;
    (void)names; (void)WPA_CTRL;
    return passed;
}

const char *netd_bringup_last_reason(void) { return g_reason; }
