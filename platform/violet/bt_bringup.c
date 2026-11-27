/* SPDX-License-Identifier: MIT */
/* platform/violet/bt_bringup.c - read-only Linux Bluetooth evidence for violet. */
#define _GNU_SOURCE

#include "bt_bringup.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BT_FW_DIR  "/vendor/bt_firmware"
#define BT_FW_PATH "/vendor/bt_firmware/BTFM.bin"
#define BT_CLASS   "/sys/class/bluetooth"

static int dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static long file_size(const char *path)
{
    struct stat st;
    if(stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

static int read_text(const char *path, char *buf, size_t cap)
{
    int fd;
    ssize_t n;

    if(cap < 2) return -1;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return -1;
    n = read(fd, buf, cap - 1);
    close(fd);
    if(n <= 0) return -1;
    buf[n] = '\0';
    while(n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return (int)n;
}

static int first_entry(const char *path, char *out, size_t cap)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int found = 0;

    if(dir == NULL) return 0;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') continue;
        snprintf(out, cap, "%s", entry->d_name);
        found = 1;
        break;
    }
    closedir(dir);
    return found;
}

static int read_hex(const char *path, char *out, size_t cap)
{
    unsigned char raw[8];
    size_t used = 0;
    ssize_t n;
    int fd;

    if(cap < 4u) return 0;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return 0;
    n = read(fd, raw, sizeof(raw));
    close(fd);
    if(n <= 0) return 0;
    out[0] = '\0';
    for(ssize_t i = 0; i < n; i++) {
        int w = snprintf(out + used, cap - used, i ? " %02x" : "%02x", raw[i]);
        if(w <= 0 || (size_t)w >= cap - used) return 0;
        used += (size_t)w;
    }
    return 1;
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
    char text[96];
    char hci[32];
    long size;

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';

    snprintf(line, sizeof(line),
             "bt: transport=slimbus@62e40000 (qca,wcn3990 behind qcom,btfmslim_slave)\n");
    append(out, cap, line);

    if(read_text(VIO_BT_DT_NODE "/compatible", text, sizeof(text)) > 0) {
        snprintf(line, sizeof(line), "bt: dt-node=present compatible=%s\n", text);
    } else {
        snprintf(line, sizeof(line), "bt: dt-node=absent (%s)\n", VIO_BT_DT_NODE);
    }
    append(out, cap, line);

    if(read_text(VIO_BT_SLIM_CTRL "/compatible", text, sizeof(text)) > 0) {
        snprintf(line, sizeof(line), "bt: controller=%s compatible=%s\n",
                 VIO_BT_SLIM_CTRL, text);
    } else {
        snprintf(line, sizeof(line), "bt: controller=absent (%s)\n", VIO_BT_SLIM_CTRL);
    }
    append(out, cap, line);

    if(read_text(VIO_BT_SLIM_NODE "/compatible", text, sizeof(text)) > 0) {
        snprintf(line, sizeof(line), "bt: slim-node=present compatible=%s\n", text);
    } else {
        snprintf(line, sizeof(line), "bt: slim-node=absent (%s)\n", VIO_BT_SLIM_NODE);
    }
    append(out, cap, line);

    {
        char ea[40] = "unreadable";
        char ifd[40] = "unreadable";
        read_hex(VIO_BT_SLIM_NODE "/elemental-addr", ea, sizeof(ea));
        read_hex(VIO_BT_SLIM_NODE "/qcom,btfm-slim-ifd-elemental-addr", ifd, sizeof(ifd));
        snprintf(line, sizeof(line), "bt: slim-ea=%s ifd-ea=%s\n", ea, ifd);
        append(out, cap, line);
    }

    size = file_size(BT_FW_PATH);
    if(size >= 0) {
        snprintf(line, sizeof(line), "bt: firmware=%s %ld bytes\n", BT_FW_PATH, size);
    } else if(dir_exists(BT_FW_DIR)) {
        snprintf(line, sizeof(line), "bt: firmware=absent in %s (p38 mounted)\n", BT_FW_DIR);
    } else {
        snprintf(line, sizeof(line), "bt: firmware=absent (%s not mounted)\n", BT_FW_DIR);
    }
    append(out, cap, line);

    if(first_entry(BT_CLASS, hci, sizeof(hci))) {
        snprintf(line, sizeof(line),
                 "bt: hci=%s (registered by Linux; QEAF service integration required)\n",
                 hci);
    } else {
        snprintf(line, sizeof(line), "bt: hci=absent (%s is empty)\n", BT_CLASS);
    }
    append(out, cap, line);

    append(out, cap, "bt: driver-owner=Linux kernel; userspace-mmio=disabled\n");
    append(out, cap, "bt: ready=0 (READY needs a verified QEAF Bluetooth service)\n");
}

void vio_bt_firmware_report(char *out, int cap)
{
    char line[192];
    long size;

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';
    size = file_size(BT_FW_PATH);
    if(size < 0) {
        snprintf(line, sizeof(line),
                 "bt fw: %s absent (p38 firmware partition not mounted)\n", BT_FW_PATH);
        append(out, cap, line);
        return;
    }
    snprintf(line, sizeof(line), "bt fw: %s %ld bytes\n", BT_FW_PATH, size);
    append(out, cap, line);
    append(out, cap, "bt fw: validation/loading belongs to the Linux Bluetooth service\n");
}

void vio_bt_slim_report(char *out, int cap)
{
    char line[256];
    char text[96];

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';
    if(read_text(VIO_BT_SLIM_CTRL "/compatible", text, sizeof(text)) > 0) {
        snprintf(line, sizeof(line), "slim: controller=present compatible=%s\n", text);
    } else {
        snprintf(line, sizeof(line), "slim: controller=absent (%s)\n", VIO_BT_SLIM_CTRL);
    }
    append(out, cap, line);
    append(out, cap, "slim: driver-owner=Linux kernel; userspace-mmio=disabled\n");
    append(out, cap, "slim: ready=0 (no userspace HCI transaction is attempted)\n");
}

void vio_bt_diag(char *out, int cap)
{
    char line[192];
    char text[96];
    char hci[32];
    long size;

    if(out == NULL || cap <= 0) return;
    out[0] = '\0';

    snprintf(line, sizeof(line), "bt diag: node %s -> %s\n", VIO_BT_DT_NODE,
             read_text(VIO_BT_DT_NODE "/compatible", text, sizeof(text)) > 0
                 ? "present" : "absent");
    append(out, cap, line);

    snprintf(line, sizeof(line), "bt diag: ctrl %s -> %s\n", VIO_BT_SLIM_CTRL,
             read_text(VIO_BT_SLIM_CTRL "/compatible", text, sizeof(text)) > 0
                 ? text : "absent");
    append(out, cap, line);

    snprintf(line, sizeof(line), "bt diag: slim %s -> %s\n", VIO_BT_SLIM_NODE,
             read_text(VIO_BT_SLIM_NODE "/compatible", text, sizeof(text)) > 0
                 ? "present" : "absent");
    append(out, cap, line);

    {
        char ea[40] = "unreadable";
        char ifd[40] = "unreadable";
        read_hex(VIO_BT_SLIM_NODE "/elemental-addr", ea, sizeof(ea));
        read_hex(VIO_BT_SLIM_NODE "/qcom,btfm-slim-ifd-elemental-addr", ifd, sizeof(ifd));
        snprintf(line, sizeof(line), "bt diag: ea=%s ifd-ea=%s\n", ea, ifd);
        append(out, cap, line);
    }

    size = file_size(BT_FW_PATH);
    snprintf(line, sizeof(line), "bt diag: BTFM.bin -> %s\n",
             size >= 0 ? "present" : (dir_exists(BT_FW_DIR) ? "missing on p38" : "p38 not mounted"));
    append(out, cap, line);

    snprintf(line, sizeof(line), "bt diag: /sys/class/bluetooth -> %s\n",
             first_entry(BT_CLASS, hci, sizeof(hci)) ? hci : "empty");
    append(out, cap, line);
    append(out, cap, "bt diag: userspace-mmio=disabled; no HCI transaction attempted\n");
}
