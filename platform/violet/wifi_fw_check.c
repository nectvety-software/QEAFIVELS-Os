/* SPDX-License-Identifier: MIT */
/* platform/violet/wifi_fw_check.c - structural validation of the staged
 * WCN3990 firmware, using the same portable driver code the kernel build
 * compiles (kernel/drivers/wifi/wcn3990/wifi_firmware.c).
 *
 * Nothing here writes storage, loads a blob into the hardware, or invents a
 * result: every line is derived from open()/fstat()/read() on the actual
 * files in this image. TZ signature checks still belong to the hardware.
 */
#include "wifi_fw_check.h"

#include "vqeaf/wifi_firmware.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FW_DIR "/lib/firmware"
#define DSP_PATH FW_DIR "/wlanmdsp.mbn"
#define REGDB_PATH FW_DIR "/regdb.bin"

static int fd_read(void *ctx, const char *path, uint8_t *buf, size_t cap,
                   size_t *out_len)
{
    int fd;
    ssize_t r;
    (void)ctx;
    *out_len = 0;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return -errno;
    do { r = read(fd, buf, cap); } while(r < 0 && errno == EINTR);
    close(fd);
    if(r < 0) return -errno;
    *out_len = (size_t)r;
    return 0;
}

static int fd_size(void *ctx, const char *path, size_t *out_size)
{
    struct stat st;
    (void)ctx;
    if(stat(path, &st) != 0) return -errno;
    if(!S_ISREG(st.st_mode)) return -ENODATA;
    *out_size = (size_t)st.st_size;
    return 0;
}

/* The driver asks for one board-data file; the image carries a per-SKU set.
 * Prefer bdwlan.bin, else the first bdwlan.* entry so the report names the
 * file that was actually validated. */
static int find_board(char *out, size_t cap)
{
    DIR *d;
    struct dirent *e;
    char best[VQEAF_WIFI_FW_PATH_MAX];
    char best_name[64];
    best[0] = '\0';
    best_name[0] = '\0';
    if(snprintf(out, cap, "%s/bdwlan.bin", FW_DIR) < 0) return -EINVAL;
    if(access(out, R_OK) == 0) return 0;
    d = opendir(FW_DIR);
    if(d == NULL) return -errno;
    while((e = readdir(d)) != NULL) {
        if(strncmp(e->d_name, "bdwlan.", 7) != 0) continue;
        if(best_name[0] != '\0' && strcmp(e->d_name, best_name) >= 0) continue;
        if(snprintf(best_name, sizeof(best_name), "%s", e->d_name) < 0) continue;
        if(snprintf(best, sizeof(best), "%s/%s", FW_DIR, e->d_name) < 0)
            best_name[0] = '\0';
    }
    closedir(d);
    if(best[0] == '\0') return -ENOENT;
    if(snprintf(out, cap, "%s", best) < 0) return -EINVAL;
    return 0;
}

static void append(char *out, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void append(char *out, size_t cap, const char *fmt, ...)
{
    size_t used = strlen(out);
    va_list ap;
    if(used + 1 >= cap) return;
    va_start(ap, fmt);
    vsnprintf(out + used, cap - used, fmt, ap);
    va_end(ap);
}

static const char *rc_name(int rc)
{
    switch(rc) {
    case 0:        return "OK";
    case -ENOENT:  return "absent";
    case -EACCES:  return "not readable";
    case -ENODATA: return "empty";
    case -ENOTSUP: return "not the expected container";
    case -ENOMEM:  return "out of memory";
    default:       return "malformed";
    }
}

static void report_one(char *out, size_t cap, const char *role,
                       const char *path, int rc, const vqeaf_wifi_fw_info_t *i)
{
    if(rc) {
        append(out, cap, "fw check: %s %s: FAILED rc=%d (%s)\n",
               role, path, rc, rc_name(rc));
        return;
    }
    if(i->format == VQEAF_WIFI_FW_FMT_ELF32 || i->format == VQEAF_WIFI_FW_FMT_ELF64) {
        append(out, cap,
               "fw check: %s %s: %s machine=%u entry=0x%08x segments=%u "
               "payload=%llu size=%llu OK\n",
               role, path, vqeaf_wifi_fw_format_name(i->format),
               (unsigned)i->machine, i->entry, i->segments,
               (unsigned long long)i->payload_bytes,
               (unsigned long long)i->size);
    } else {
        append(out, cap, "fw check: %s %s: %s size=%llu OK\n",
               role, path, vqeaf_wifi_fw_format_name(i->format),
               (unsigned long long)i->size);
    }
}

int vio_wifi_fw_report(char *out, size_t cap)
{
    vqeaf_wifi_firmware_io_t io;
    vqeaf_wifi_fw_paths_t paths;
    vqeaf_wifi_fw_set_t set;
    uint8_t hdr[4096];
    int rc, board_rc;

    if(out == NULL || cap == 0) return -EINVAL;
    memset(&io, 0, sizeof(io));
    io.read = fd_read;
    io.size = fd_size;
    memset(&paths, 0, sizeof(paths));
    snprintf(paths.dsp, sizeof(paths.dsp), "%s", DSP_PATH);
    snprintf(paths.regdb, sizeof(paths.regdb), "%s", REGDB_PATH);
    board_rc = find_board(paths.board, sizeof(paths.board));
    if(board_rc) {
        /* Keep the set check honest: no board file means no set. */
        append(out, cap, "fw check: board %s/bdwlan.*: FAILED rc=%d (%s)\n",
               FW_DIR, board_rc, rc_name(board_rc));
    }
    rc = vqeaf_wifi_fw_check_set(&io, &paths, &set, hdr, sizeof(hdr));
    report_one(out, cap, "dsp", paths.dsp, set.dsp_rc, &set.dsp);
    if(board_rc == 0)
        report_one(out, cap, "board", paths.board, set.board_rc, &set.board);
    report_one(out, cap, "regdb", paths.regdb, set.regdb_rc, &set.regdb);
    if(set.ok)
        append(out, cap, "fw set: OK (wlanmdsp+bdf+regdb match the driver contract)\n");
    else
        append(out, cap, "fw set: FAILED (rc=%d; icnss must not be handed this set)\n",
               rc ? rc : board_rc);
    return set.ok && board_rc == 0 ? 0 : (rc ? rc : board_rc);
}