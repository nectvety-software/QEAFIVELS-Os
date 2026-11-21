/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_state.c - sysfs/clock providers for the shell. */
#define _GNU_SOURCE

#include "vqeaf_state.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "violet.h"

vqeaf_state_t vqeaf_state = {
    .clock_text = "--:--",
    .date_text = "Chưa đặt thời gian",
    .wifi_up = -1,
    .bt_up = -1,
    .brightness = 85,
    .volume = 50,
    .screen_timeout_s = 60,
    .screen_off = 0,
};

static int read_first_line(const char *path, char *buf, size_t cap)
{
    FILE *file = fopen(path, "r");
    size_t len;

    if(file == NULL) return -1;
    if(fgets(buf, (int)cap, file) == NULL) {
        fclose(file);
        return -1;
    }
    fclose(file);
    len = strlen(buf);
    while(len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return 0;
}

static int read_int(const char *path)
{
    char line[32];
    int value = -1;

    if(read_first_line(path, line, sizeof(line)) < 0) return -1;
    if(sscanf(line, "%d", &value) != 1) return -1;
    return value;
}

static void refresh_clock(void)
{
    static const char *const weekdays[] = {
        "Chủ nhật", "Thứ 2", "Thứ 3", "Thứ 4", "Thứ 5", "Thứ 6", "Thứ 7"
    };
    struct timespec now;
    struct tm local;

    if(clock_gettime(CLOCK_REALTIME, &now) != 0) return;
    if(localtime_r(&now.tv_sec, &local) == NULL) return;

    /* A handset without an RTC boots at the epoch; showing 01:00 on
     * 1 January 1970 would be a fabricated time. */
    if(local.tm_year + 1900 < 2020) {
        vqeaf_state.time_valid = 0;
        snprintf(vqeaf_state.clock_text, sizeof(vqeaf_state.clock_text),
                 "--:--");
        snprintf(vqeaf_state.date_text, sizeof(vqeaf_state.date_text),
                 "Chưa đặt thời gian");
        return;
    }

    vqeaf_state.time_valid = 1;
    vqeaf_state.hour = local.tm_hour;
    vqeaf_state.minute = local.tm_min;
    snprintf(vqeaf_state.clock_text, sizeof(vqeaf_state.clock_text),
             "%02d:%02d", local.tm_hour, local.tm_min);
    snprintf(vqeaf_state.date_text, sizeof(vqeaf_state.date_text),
             "%s, %d Tháng %d, %d",
             weekdays[local.tm_wday % 7], local.tm_mday,
             local.tm_mon + 1, local.tm_year + 1900);
}

static void refresh_battery(void)
{
    static const char *const roots[] = {
        "/sys/class/power_supply/battery",
        "/sys/class/power_supply/BAT0",
        "/sys/class/power_supply/BAT1",
    };
    size_t i;

    for(i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        char path[128];
        int capacity;
        char status[32];

        snprintf(path, sizeof(path), "%s/capacity", roots[i]);
        capacity = read_int(path);
        if(capacity < 0) continue;
        if(capacity > 100) capacity = 100;
        vqeaf_state.battery_valid = 1;
        vqeaf_state.battery_pct = capacity;

        snprintf(path, sizeof(path), "%s/status", roots[i]);
        if(read_first_line(path, status, sizeof(status)) == 0)
            vqeaf_state.charging = strstr(status, "Charging") != NULL;
        else
            vqeaf_state.charging = 0;
        return;
    }
    vqeaf_state.battery_valid = 0;
    vqeaf_state.charging = 0;
}

static int dir_has_name(const char *path, const char *needle)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int found = 0;

    if(dir == NULL) return 0;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') continue;
        if(strstr(entry->d_name, needle) != NULL) {
            found = 1;
            break;
        }
    }
    closedir(dir);
    return found;
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

/* Firmware is never bundled: the blobs have to come off the user's own
 * phone. Probe the directories the vendor drivers ask for instead of
 * claiming a radio that cannot leave reset. This mirrors the split HakiOS
 * documents between bring-up (firmware/driver) and the Wi-Fi stack.
 * MediaTek's MT6893 stack asks for mediatek/mt6893/..., so those subdirs are
 * probed too; Qualcomm's WCN3990 blobs sit flat. */
static void refresh_wifi_firmware(void)
{
    static const char *const dirs[] = {
        "/vendor/firmware", "/system/vendor/firmware",
        "/etc/firmware", "/system/etc/firmware",
        "/lib/firmware", "/usr/share/firmware",
        "/vendor/firmware/mediatek/mt6893", "/lib/firmware/mediatek/mt6893",
    };
    static const char *const blobs[] = { "wcn", "WCN", "wlan", "WLAN",
                                         "bdwlan", "qca",
                                         "WIFI_RAM", "wifi_ram", "WIFI_MT",
                                         "mt6893" };

    vqeaf_state.wifi_firmware_present = 0;
    vqeaf_state.wifi_firmware_path[0] = '\0';
    for(size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        for(size_t b = 0; b < sizeof(blobs) / sizeof(blobs[0]); b++) {
            if(dir_has_name(dirs[i], blobs[b])) {
                vqeaf_state.wifi_firmware_present = 1;
                snprintf(vqeaf_state.wifi_firmware_path,
                         sizeof(vqeaf_state.wifi_firmware_path), "%s",
                         dirs[i]);
                return;
            }
        }
    }
}

static void refresh_wifi(void)
{
    static int firmware_tick;
    DIR *net;
    struct dirent *entry;
    char state[32];
    char path[64];
    char ifname[16] = "";

    vqeaf_state.wifi_if_present = 0;
    vqeaf_state.wifi_ifname[0] = '\0';
    vqeaf_state.wifi_mac[0] = '\0';
    vqeaf_state.wifi_link = 0;

    net = opendir("/sys/class/net");
    if(net != NULL) {
        while((entry = readdir(net)) != NULL) {
            if(strncmp(entry->d_name, "wlan", 4) == 0) {
                snprintf(ifname, sizeof(ifname), "%s", entry->d_name);
                break;
            }
        }
        closedir(net);
    }

    vqeaf_state.wifi_driver_loaded =
        access("/sys/module/wlan", F_OK) == 0 ||
        access("/sys/module/cfg80211", F_OK) == 0 ||
        access("/sys/module/wlan_drv_gen4m", F_OK) == 0 ||
        access("/sys/module/conninfra", F_OK) == 0 ||
        /* MediaTek builds the stack in: the platform drivers are the
         * evidence, not /sys/module. */
        access("/sys/bus/platform/drivers/wlan", F_OK) == 0 ||
        access("/sys/bus/platform/drivers/mtk_conninfra", F_OK) == 0;

    if(firmware_tick++ % 10 == 0 || firmware_tick == 0)
        refresh_wifi_firmware();

    if(ifname[0] == '\0') {
        vqeaf_state.wifi_up = -1;
        return;
    }

    vqeaf_state.wifi_if_present = 1;
    snprintf(vqeaf_state.wifi_ifname, sizeof(vqeaf_state.wifi_ifname), "%s",
             ifname);
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", ifname);
    if(read_first_line(path, state, sizeof(state)) == 0)
        vqeaf_state.wifi_up = strcmp(state, "up") == 0 ? 1 : 0;
    else
        vqeaf_state.wifi_up = 0;

    snprintf(path, sizeof(path), "/sys/class/net/%s/address", ifname);
    if(read_first_line(path, vqeaf_state.wifi_mac,
                       sizeof(vqeaf_state.wifi_mac)) < 0)
        vqeaf_state.wifi_mac[0] = '\0';

    /* /proc/net/wireless carries the link quality the driver reports; the
     * SSID needs nl80211/WEXT, so it stays unknown when no supplicant runs. */
    {
        FILE *file = fopen("/proc/net/wireless", "r");
        char line[192];

        if(file != NULL) {
            int header = 0;
            while(fgets(line, sizeof(line), file) != NULL) {
                char name[16];
                unsigned status = 0;
                int link = 0;
                if(header < 2) { header++; continue; }
                if(sscanf(line, "%15[^:]: %x %d", name, &status, &link) == 3 &&
                   strcmp(name, ifname) == 0) {
                    if(link < 0) link = 0;
                    if(link > 100) link = 100;
                    vqeaf_state.wifi_link = link;
                    break;
                }
            }
            fclose(file);
        }
    }
}

static void refresh_bluetooth(void)
{
    static int firmware_tick;
    DIR *dir;
    struct dirent *entry;
    char path[64];
    char state[32];
    char ifname[16] = "";

    vqeaf_state.bt_up = -1;
    vqeaf_state.bt_if_present = 0;
    vqeaf_state.bt_ifname[0] = '\0';

    dir = opendir("/sys/class/bluetooth");
    if(dir != NULL) {
        while((entry = readdir(dir)) != NULL) {
            if(entry->d_name[0] == '.') continue;
            snprintf(ifname, sizeof(ifname), "%s", entry->d_name);
            break;
        }
        closedir(dir);
    }
    vqeaf_state.bt_driver_loaded =
        access("/sys/module/hci_uart", F_OK) == 0 ||
        access("/sys/module/hci_smd", F_OK) == 0 ||
        access("/sys/module/bluetooth_power", F_OK) == 0 ||
        access("/sys/module/hci_qca", F_OK) == 0 ||
        access("/sys/module/btmtk", F_OK) == 0 ||
        access("/sys/module/btmtk_uart", F_OK) == 0 ||
        access("/sys/module/btif", F_OK) == 0 ||
        access("/sys/bus/platform/drivers/mtkbt_btif", F_OK) == 0 ||
        access("/sys/bus/platform/drivers/mtk_btif", F_OK) == 0 ||
        access("/sys/module/bluetooth", F_OK) == 0;

    if(firmware_tick++ % 10 == 0 || firmware_tick == 0) {
        vqeaf_state.bt_firmware_present = 0;
        vqeaf_state.bt_firmware_path[0] = '\0';
        if(dir_has_entries("/vendor/bt_firmware")) {
            vqeaf_state.bt_firmware_present = 1;
            snprintf(vqeaf_state.bt_firmware_path,
                     sizeof(vqeaf_state.bt_firmware_path),
                     "/vendor/bt_firmware");
            goto bt_done;
        }
        static const char *const dirs[] = {
            "/vendor/firmware", "/system/vendor/firmware",
            "/etc/firmware", "/lib/firmware", "/usr/lib/firmware",
            "/vendor/firmware/mediatek/mt6893", "/lib/firmware/mediatek/mt6893" };
        static const char *const blobs[] = { "rampatch", "RAMPATCH", "nvm",
                                             "NVM", "qca", "QCA",
                                             "BT_RAM", "bt_ram", "mt6893" };
        for(size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); i++) {
            for(size_t b = 0; b < sizeof(blobs)/sizeof(blobs[0]); b++) {
                if(dir_has_name(dirs[i], blobs[b])) {
                    vqeaf_state.bt_firmware_present = 1;
                    snprintf(vqeaf_state.bt_firmware_path,
                             sizeof(vqeaf_state.bt_firmware_path), "%s", dirs[i]);
                    goto bt_done;
                }
            }
        }
    bt_done: ;
    }

    if(ifname[0] == '\0') {
        vqeaf_state.bt_up = -1;
        return;
    }
    vqeaf_state.bt_if_present = 1;
    snprintf(vqeaf_state.bt_ifname, sizeof(vqeaf_state.bt_ifname), "%s", ifname);
    snprintf(path, sizeof(path), "/sys/class/bluetooth/%s/power/control", ifname);
    if(read_first_line(path, state, sizeof(state)) == 0)
        vqeaf_state.bt_up = strcmp(state, "on") == 0 ? 1 : 0;
    else
        vqeaf_state.bt_up = 0;
}
static void refresh_devices(void)
{
    static const char *const input = "/dev/input";
    DIR *dir;
    struct dirent *entry;
    struct stat info;

    vqeaf_state.drm_present = access("/dev/dri/card0", R_OK) == 0;

    vqeaf_state.touch_events = 0;
    dir = opendir(input);
    if(dir != NULL) {
        while((entry = readdir(dir)) != NULL)
            if(strncmp(entry->d_name, "event", 5) == 0)
                vqeaf_state.touch_events++;
        closedir(dir);
    }

    vqeaf_state.usb_udc_present = 0;
    dir = opendir("/sys/class/udc");
    if(dir != NULL) {
        while((entry = readdir(dir)) != NULL)
            if(entry->d_name[0] != '.')
                vqeaf_state.usb_udc_present = 1;
        closedir(dir);
    }

    vqeaf_state.backlight_present = 0;
    dir = opendir("/sys/class/backlight");
    if(dir != NULL) {
        while((entry = readdir(dir)) != NULL)
            if(entry->d_name[0] != '.')
                vqeaf_state.backlight_present = 1;
        closedir(dir);
    }

    if(stat("/run/qeaf-lun.img", &info) == 0 && info.st_size > 0) {
        vqeaf_state.usb_lun_present = 1;
        vqeaf_state.usb_lun_size = (uint64_t)info.st_size;
    }
    else {
        vqeaf_state.usb_lun_present = 0;
        vqeaf_state.usb_lun_size = 0;
    }
}

static void refresh_storage(void)
{
    struct statvfs vfs;
    uint64_t total;
    uint64_t free_bytes;

    if(statvfs("/", &vfs) != 0 || vfs.f_blocks == 0) {
        vqeaf_state.storage_valid = 0;
        return;
    }
    total = (uint64_t)vfs.f_blocks * vfs.f_frsize;
    free_bytes = (uint64_t)vfs.f_bfree * vfs.f_frsize;
    if(total == 0) {
        vqeaf_state.storage_valid = 0;
        return;
    }
    vqeaf_state.storage_valid = 1;
    vqeaf_state.storage_total = total;
    vqeaf_state.storage_free = free_bytes;
    vqeaf_state.storage_pct =
        (int)(((total - free_bytes) * 100u + total / 2u) / total);
}

static void refresh_memory(void)
{
    char line[128];
    FILE *file;
    unsigned long total_kb = 0;
    unsigned long available_kb = 0;

    file = fopen("/proc/meminfo", "r");
    if(file == NULL) {
        vqeaf_state.mem_valid = 0;
        return;
    }
    while(fgets(line, sizeof(line), file) != NULL) {
        if(strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line + 9, "%lu", &total_kb);
        else if(strncmp(line, "MemAvailable:", 13) == 0)
            sscanf(line + 13, "%lu", &available_kb);
        if(total_kb != 0 && available_kb != 0) break;
    }
    fclose(file);

    if(total_kb == 0) {
        vqeaf_state.mem_valid = 0;
        return;
    }
    if(available_kb > total_kb) available_kb = total_kb;
    vqeaf_state.mem_valid = 1;
    vqeaf_state.mem_total_kb = (uint32_t)total_kb;
    vqeaf_state.mem_available_kb = (uint32_t)available_kb;
}

static void refresh_uptime(void)
{
    char line[64];
    double seconds = 0.0;

    if(read_first_line("/proc/uptime", line, sizeof(line)) == 0 &&
       sscanf(line, "%lf", &seconds) == 1 && seconds >= 0.0) {
        vqeaf_state.uptime_s = (uint64_t)seconds;
        return;
    }
    vqeaf_state.uptime_s = (uint64_t)(vio_monotonic_ms() / 1000u);
}

void vqeaf_state_refresh(void)
{
    refresh_clock();
    refresh_battery();
    refresh_wifi();
    refresh_bluetooth();
    refresh_devices();
    refresh_storage();
    refresh_memory();
    refresh_uptime();
}

int vqeaf_state_set_brightness(int percent)
{
    if(percent < 5) percent = 5;
    if(percent > 100) percent = 100;
    if(vio_set_backlight(percent) == 0) {
        vqeaf_state.brightness = percent;
        return 0;
    }
    return -1;
}

int vqeaf_state_set_volume(int percent)
{
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;
    vqeaf_state.volume = percent;
    return 0;
}

int vqeaf_state_set_screen_timeout(int seconds)
{
    if(seconds != 0 && seconds != 30 && seconds != 60 && seconds != 300 &&
       seconds != 1800)
        return -1;
    vqeaf_state.screen_timeout_s = seconds;
    vqeaf_state.screen_off = 0;
    return 0;
}
