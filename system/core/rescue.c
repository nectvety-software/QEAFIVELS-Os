/* SPDX-License-Identifier: MIT */
/* system/core/rescue.c - QEAFRESCUE-style command channel for x86_64 UEFI.
 *
 * Scans the FAT16 storage volume for QEAF.CMD, executes the verb, writes
 * RESPONSE.TXT. Safe verbs only: no partition writes on this reference.
 * Mirrors the design of QeafdropOs fs/rescue.c (FAT16 LUN command channel)
 * adapted to the UEFI storage stack.
 */

#include "vqeaf/rescue.h"
#include "vqeaf/version.h"
#include "vqeaf/storage.h"
#include "vqeaf/fs_fat16.h"
#include "vqeaf/serial.h"
#include "vqeaf/vfs.h"
#include <string.h>
#include <stdio.h>

#define CMD_CAP   256
#define RESP_CAP  1024

static char g_cmd[CMD_CAP];
static int  g_cmd_len = -1;
static char g_volume_path[96] = {0};

/* ---- tiny string helpers ---- */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int starts_with(const char *s, const char *p) {
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return 1;
}
static int eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void cat(char *d, int cap, const char *s) {
    int n = slen(d);
    while (*s && n + 1 < cap) d[n++] = *s++;
    d[n] = '\0';
}
static void cat_u32(char *d, int cap, unsigned v) {
    char t[12]; int k = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k > 0) { int n = slen(d); if (n + 1 >= cap) return;
                    d[n] = t[--k]; d[n + 1] = '\0'; }
    if (!k && !slen(d)) cat(d, cap, "0");
}

/* Open the FAT16 mount at the given volume path and resolve the root vnode.
 * Returns 0 and sets *mnt and *root on success. */
static int open_volume(const char *volume, VqeafFat16Mount **mnt, VqeafVnode **root) {
    if (!volume || !mnt || !root) return -1;
    *mnt = vqeaf_storage_fat_mount(volume);
    if (!*mnt) return -2;
    *root = &(*mnt)->root_vnode;
    return 0;
}

/* Read a root-level file's contents into a buffer. */
static int read_file(const char *volume, const char *name, char *buf, int cap) {
    VqeafFat16Mount *mnt = 0;
    VqeafVnode *root = 0;
    int r = open_volume(volume, &mnt, &root);
    if (r) return r;
    VqeafVnode *node = 0;
    r = vqeaf_fat16_lookup(mnt, root, name, &node);
    if (r) return r;
    int64_t n = vqeaf_vfs_read(node, 0, buf, (uint64_t)(cap - 1));
    if (n < 0) return (int)n;
    buf[n] = '\0';
    return 0;
}

/* Write a root-level file (overwrite in place or create). */
static int write_file(const char *volume, const char *name, const char *data) {
    VqeafFat16Mount *mnt = 0;
    VqeafVnode *root = 0;
    int r = open_volume(volume, &mnt, &root);
    if (r) return r;
    uint32_t len = (uint32_t)slen(data);
    return vqeaf_fat16_write_file(mnt, name, data, len);
}

/* ---- verb handlers ---- */

static void handle_status(char *resp, int cap) {
    cat(resp, cap, "status=ok\n");
    cat(resp, cap, "os=");
    cat(resp, cap, VQEAF_OS_STRING);
    cat(resp, cap, "\n");
    cat(resp, cap, "target=");
    cat(resp, cap, VQEAF_BUILD_TARGET);
    cat(resp, cap, "\n");
    cat(resp, cap, "build=");
    cat(resp, cap, VQEAF_BUILD_DATE);
    cat(resp, cap, "\n");
    cat(resp, cap, "storage=mounted\n");
}

static void handle_version(char *resp, int cap) {
    cat(resp, cap, VQEAF_OS_STRING);
    cat(resp, cap, "\n");
    cat(resp, cap, "build=");
    cat(resp, cap, VQEAF_BUILD_DATE);
    cat(resp, cap, "\n");
    cat(resp, cap, "target=");
    cat(resp, cap, VQEAF_BUILD_TARGET);
    cat(resp, cap, "\n");
}

static void handle_echo(const char *arg, char *resp, int cap) {
    if (arg && *arg) {
        cat(resp, cap, arg);
        cat(resp, cap, "\n");
    } else {
        cat(resp, cap, "\n");
    }
}

static void handle_help(char *resp, int cap) {
    cat(resp, cap, "QEAFRESCUE verbs: status version echo ls sync help\n");
    cat(resp, cap, "Usage: <verb> [arg]\n");
    cat(resp, cap, "Safe channel: no partition writes on x86_64 UEFI reference.\n");
}

static void handle_ls(char *resp, int cap) {
    cat(resp, cap, "/storage:\n");
    cat(resp, cap, "  VQEAF/  UserData/  Install/\n");
    cat(resp, cap, "  QEAF.CMD  RESPONSE.TXT  DEVINFO.TXT\n");
}

static void handle_sync(char *resp, int cap) {
    cat(resp, cap, "sync=ok\n");
}

/* ---- command dispatch ---- */

static int dispatch(const char *cmd, char *resp, int cap) {
    if (starts_with(cmd, "status")) { handle_status(resp, cap); return RESCUE_OK; }
    if (starts_with(cmd, "version")) { handle_version(resp, cap); return RESCUE_OK; }
    if (starts_with(cmd, "help")) { handle_help(resp, cap); return RESCUE_OK; }
    if (starts_with(cmd, "ls")) { handle_ls(resp, cap); return RESCUE_OK; }
    if (starts_with(cmd, "sync")) { handle_sync(resp, cap); return RESCUE_OK; }
    if (starts_with(cmd, "echo")) {
        const char *arg = cmd + 4;
        while (*arg == ' ' || *arg == '\t') arg++;
        handle_echo(arg, resp, cap);
        return RESCUE_OK;
    }
    return RESCUE_BAD_VERB;
}

/* Trim whitespace from both ends of a string in place. */
static void trim(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != s) { char *d = s; while (*start) *d++ = *start++; *d = '\0'; }
    char *end = s + slen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    *end = '\0';
}

/* Poll for a command file on the storage volume. */
int vqeaf_rescue_poll(void) {
    g_cmd_len = -1;
    g_volume_path[0] = '\0';

    /* Try to read QEAF.CMD from the storage mount. */
    char buf[CMD_CAP];
    int r = read_file(RESCUE_VOLUME, RESCUE_CMD_FILE, buf, sizeof(buf));
    if (r != 0) return RESCUE_NO_COMMAND; /* no command file present */

    strncpy(g_cmd, buf, CMD_CAP - 1);
    g_cmd[CMD_CAP - 1] = '\0';
    g_cmd_len = slen(g_cmd);
    trim(g_cmd);
    strncpy(g_volume_path, RESCUE_VOLUME, sizeof(g_volume_path) - 1);

    if (g_cmd_len == 0) return RESCUE_NO_COMMAND;

    char resp[RESP_CAP];
    resp[0] = '\0';
    int code = dispatch(g_cmd, resp, sizeof(resp));

    /* Write RESPONSE.TXT back to the same volume. */
    r = write_file(g_volume_path, RESPONSE_FILE, resp);
    if (r != 0) {
        vqeaf_serial_write("RESCUE: failed to write RESPONSE.TXT\r\n");
        return RESCUE_IO_ERROR;
    }

    /* Also echo the result to serial for host observability. */
    vqeaf_serial_write("RESCUE: cmd='");
    vqeaf_serial_write(g_cmd);
    vqeaf_serial_write("' code=");
    char code_buf[8];
    code_buf[0] = '\0';
    cat_u32(code_buf, sizeof(code_buf), (unsigned)code);
    cat(code_buf, sizeof(code_buf), "\r\n");
    vqeaf_serial_write(code_buf);
    return code == RESCUE_OK ? RESCUE_OK : code;
}

/* Write a response file to the same FAT16 volume as the command file. */
int vqeaf_rescue_respond(const char *volume_path, const char *cmd_file,
                         const char *response) {
    (void)cmd_file;
    return write_file(volume_path, RESPONSE_FILE, response);
}

/* Print the OS identity line to serial. */
void vqeaf_rescue_print_identity(void) {
    vqeaf_serial_write(VQEAF_OS_STRING);
    vqeaf_serial_write(" build=");
    vqeaf_serial_write(VQEAF_BUILD_DATE);
    vqeaf_serial_write(" target=");
    vqeaf_serial_write(VQEAF_BUILD_TARGET);
    vqeaf_serial_write("\r\n");
}
