/* SPDX-License-Identifier: MIT */
#include "bt_bringup.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int dir_has_entry(const char *path)
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

static int first_entry(const char *path, char *out, size_t cap)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int found = 0;

    if(out != NULL && cap > 0) out[0] = '\0';
    if(dir == NULL) return 0;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') continue;
        if(out != NULL && cap > 0) snprintf(out, cap, "%s", entry->d_name);
        found = 1;
        break;
    }
    closedir(dir);
    return found;
}

static void append(char *out, int cap, const char *text)
{
    size_t used;

    if(out == NULL || cap <= 0 || text == NULL) return;
    used = strlen(out);
    if(used + 1u >= (size_t)cap) return;
    snprintf(out + used, (size_t)cap - used, "%s", text);
}

void vio_bt_report(char *out, int cap)
{
    char line[192];
    char hci[32] = "";
    int btif = path_exists("/sys/bus/platform/drivers/mtkbt_btif") ||
               path_exists("/sys/bus/platform/drivers/mtk_btif");

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';
    snprintf(line, sizeof(line),
             "bt: transport=MediaTek BTIF; driver-owner=Linux kernel\n");
    append(out, cap, line);
    snprintf(line, sizeof(line), "bt: btif-driver=%s\n",
             btif ? "registered" : "absent");
    append(out, cap, line);
    snprintf(line, sizeof(line), "bt: hci=%s\n",
             first_entry("/sys/class/bluetooth", hci, sizeof(hci)) ? hci : "absent");
    append(out, cap, line);
    snprintf(line, sizeof(line),
             "bt: firmware=%s; userspace-mmio=disabled; ready=0\n",
             dir_has_entry("/vendor/firmware/mediatek/mt6893") ||
             dir_has_entry("/lib/firmware/mediatek/mt6893") ?
             "directory populated" : "directory absent");
    append(out, cap, line);
}

void vio_bt_firmware_report(char *out, int cap)
{
    char line[192];
    int vendor = dir_has_entry("/vendor/firmware/mediatek/mt6893");
    int system = dir_has_entry("/lib/firmware/mediatek/mt6893");

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';
    snprintf(line, sizeof(line),
             "bt fw: vendor=%s system=%s; validation/loading belongs to Linux\n",
             vendor ? "populated" : "absent", system ? "populated" : "absent");
    append(out, cap, line);
}

void vio_bt_slim_report(char *out, int cap)
{
    if(out == NULL || cap <= 0) return;
    out[0] = '\0';
    append(out, cap,
           "slim: not applicable on MT689x; Bluetooth uses the MediaTek BTIF\n");
}

void vio_bt_diag(char *out, int cap)
{
    char line[192];
    char hci[32] = "";

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';
    snprintf(line, sizeof(line), "bt diag: mtkbt_btif=%s mtk_btif=%s\n",
             path_exists("/sys/bus/platform/drivers/mtkbt_btif") ? "present" : "absent",
             path_exists("/sys/bus/platform/drivers/mtk_btif") ? "present" : "absent");
    append(out, cap, line);
    snprintf(line, sizeof(line), "bt diag: hci=%s stpbt=%s\n",
             first_entry("/sys/class/bluetooth", hci, sizeof(hci)) ? hci : "absent",
             path_exists("/dev/stpbt") ? "present" : "absent");
    append(out, cap, line);
    append(out, cap,
           "bt diag: userspace-mmio=disabled; no HCI transaction attempted\n");
}

/* MT689x has no validated persistent journal partition. */
void vio_journal_trace_raw(const char *line)
{
    (void)line;
}
