/* SPDX-License-Identifier: MIT */
/* fs/rescue.c - the QEAFRESCUE command channel, in the image itself.
 *
 * Derived from QeafdropOs fs/rescue.c (MIT, same owner), proven on violet.
 * The volume is built in RAM by fat16.c and served to the PC as a LUN. A host
 * drops QEAF.CMD onto it; this loop notices the new bytes, runs the verbs and
 * writes RESPONSE.TXT back. Everything is a real FAT16 file - no separate
 * socket, no hidden side channel.
 *
/* The flash verb is the one privileged action. It refuses anything that is
 * not exactly "flash recovery BOOTIMG.BIN confirm", checks the boot-image magic and
 * the p77 size, then walks BOOTIMG.BIN's cluster chain - a file the host
 * scattered over the volume streams through in several runs instead of
 * being refused - writes each run to /dev/mmcblk0p77 at the offset its
 * bytes occupy, then READS THE PARTITION BACK and compares every byte
 * before it says "verified".
 *
 * Host gate: rescue_set_partition_path() points the same code at a fixture
 * file, so the loop and the verification are measured without a phone.
 * The `tap` verb injects a canvas touch through the platform hook so the
 * touch path can be exercised from the host without a finger on the glass.
 */

#include "fs/rescue.h"
#include "fs/fat16.h"
#include "core/sysio.h"
#include "core/log.h"
#include "qeaf_version.h"
#include "hal/include/usb_gadget.h"
#include "hal/include/display.h"
#include "../../ui/vqeaf_qeapp_device.h"
#include "../../ui/vqeaf_qeapp.h"
#include "../../ui/vqeaf_pages.h"
#include "wifi_bringup.h"
#include "../../host_services.h"
#include "../../violet.h"
#include "../../qrtr_ns.h"
#include "../../qrtr_probe.h"
#include "../../vio_caps.h"
#include "../../vqeaf_camera.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../appstore.h"

#ifdef __linux__
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <linux/reboot.h>
#endif

#define CMD_CAP   1024
#define RESP_CAP  8192
#define LOG_CAP   8192
#define P77_SIZE  (64u * 1024u * 1024u)

static const char *g_partition = "/dev/mmcblk0p77";
static rescue_snapshot_fn g_snapshot;
static rescue_screenshot_fn g_screenshot;
static rescue_language_fn g_language;
static rescue_status_fn g_status;
static rescue_tap_fn g_tap;
static rescue_hold_fn g_hold;
static rescue_swipe_fn g_swipe;
static rescue_page_fn g_page;
static rescue_power_fn g_power;
static rescue_fault_fn g_fault;
static rescue_unlock_fn g_unlock;
static uint8_t g_last[CMD_CAP];
/* Increases once per executed batch: RESPONSE.TXT starts with seq=N so a
 * host can never mistake the previous answer for this batch. */
static unsigned g_seq;
static int g_last_len = -1;
static int g_result;
static char g_log[LOG_CAP];
static int g_log_len;

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int starts(const char *s, const char *p) {
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
static void cat_u(char *d, int cap, unsigned v) {
    char t[12]; int k = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k > 0) { int n = slen(d); if (n + 1 >= cap) return;
                    d[n] = t[--k]; d[n + 1] = '\0'; }
    if (!k && !slen(d)) cat(d, cap, "0");
}
static void put(struct fat16_volume *vol, const char *name, const char *text) {
    fat16_write_file(vol, name, (const uint8_t *)text, (size_t)slen(text));
}
static void note(const char *s) {
    int n = slen(s), i;
    if (g_log_len + n + 2 >= LOG_CAP) {
        /* keep the tail: the last verification line is what matters */
        int keep = LOG_CAP / 2;
        memmove(g_log, g_log + g_log_len - keep, (size_t)keep);
        g_log_len = keep;
    }
    for (i = 0; i < n && g_log_len + 1 < LOG_CAP; i++)
        g_log[g_log_len++] = s[i];
    if (g_log_len + 1 < LOG_CAP) g_log[g_log_len++] = '\n';
    g_log[g_log_len] = '\0';
}

static const char HELP[] =
    "QEAFRESCUE command channel (VQEAF-SYNC/1)\n"
    "=========================================\n"
    "Write one or more verbs per line into QEAF.CMD, flush the volume (do\n"
    "not eject), then read RESPONSE.TXT by RAW SECTOR - the mounted Windows\n"
    "view is stale. Every answer starts with 'seq=N', and N grows with\n"
    "every batch, so a host can tell a fresh answer from the previous\n"
    "one; end the batch with 'echo <nonce>' as a second, content-level\n"
    "check.\n\n"
    "  help                 show this text\n"
    "  status               OS, USB mode, cable, LUN sectors, boot stage\n"
    "  wifi [net|dmesg|sdio|mod|fw|up|off|state|modem|host|log|find NAME|ls DIR|link PATH|cat FILE|on|pd NODE PORT PD|listen NODE PORT PATH|discover [SERVICE]]\n"
    "                       real WCN3990 state: netdevs, mounts, firmware,\n"
    "                       remoteproc; `wifi up` starts the modem (it\n"
    "                       resets the SoC without a service layer),\n"
    "                       `wifi off` releases it again\n"
    "                       `wifi modem` dumps the msm_subsys census\n"
    "  cam                  native V4L2 camera node census (no vendor HAL)\n"
    "  cam subs             list /dev/v4l-subdevN and kernel names\n"
    "  cam session          create a real cam-req-mgr session on /dev/video0\n"
    "  cam caps             CAM_QUERY_CAP on real cam-isp/cam-icp/sensor\n"
    "  cam link             CAM_ACQUIRE_DEV on ISP/ICP/sensor, then try link\n"
    "  cam grab [NODE]      capture one frame from /dev/videoN via V4L2;\n"
    "                       only a real frame marks camera READY\n"
    "  sync                 republish DEVINFO.TXT\n"
    "  usb charge|mtp|ptp   choose the USB data mode\n"
    "  rotate 0|90|180|270|auto   screen orientation / auto-rotate\n"
    "  lang vi|en|zh        switch the UI language and persist it\n"
    "  echo TEXT            echoed back verbatim (nonce)\n"
    "  ls                   root entries of this volume\n"
    "  kmsg                 tail of the vqeaf: kernel-log lines (boot trace)\n"
    "  diag                 watchdog / charger / thermal census: whether\n"
    "                       a resetting boot even has a watchdog driver\n"
    "  pstore               read-only: prior-boot kernel console (pstore/\n"
    "                       ramoops), /proc/last_kmsg and bootreason ->\n"
    "                       PSTORE.TXT, to find why the last boot reset\n"
    "  snap                 dump the live scanout to SCREEN.RAW (RGB565)\n"
    "  shot                 save a full-resolution PNG screenshot to\n"
    "                       UserData/Pictures/SHOTnnnn.PNG\n"
    "  rec start [fps] [secs] / rec stop / rec status\n"
    "                       record the live scanout into a QVF clip at\n"
    "                       UserData/Videos/VIDnnnn.QVF (non-blocking)\n"
    "  tap X Y              inject a touch at canvas pixel (X,Y)\n"
    "  hold X Y [ms]        keep the touch down at (X,Y) for ms (default\n"
    "                       1200, 100..10000) so a long-press fires\n"
    "  power                short power-key press: lock the screen, run it\n"
    "                       again to wake to the lock page\n"
    "  unlock               wake the panel and go to Home, without the\n"
    "                       unlock swipe (device checks drive this)\n"
    "  fault ui             fault the UI on purpose and record signal,\n"
    "                       pc/lr, the open page and the last 40 log\n"
    "                       lines in CRASH.TXT\n"
    "  reboot system|recovery|bootloader\n"
    "                       restart the phone; recovery boots p77\n"
    "  flash recovery BOOTIMG.BIN confirm\n"
    "                       walk BOOTIMG.BIN's cluster chain, write it to\n"
    "                       p77, read it back and compare byte for byte\n"
    "                       before answering (fragmented files are fine)\n\n"
    "Only 'recovery' (p77) is writable. p76/boot, userdata and misc are\n"
    "never touched. The word 'confirm' is mandatory. HELP.TXT, SUMMARY.TXT,\n"
    "RESPONSE.TXT, QEAFBOOT.LOG and DEVINFO.TXT are the channel's own files.\n";

static const char SUMMARY[] =
    QEAF_OS_STRING " - rescue volume\n\n"
    "Built in RAM by fat16.c; no partition has to survive for these bytes\n"
    "to exist. Read with raw sectors, not the Windows mounted view.\n\n"
    "--- commands (QEAF.CMD, one per line)\n"
    "  help  status  wifi  sync  usb MODE  echo TEXT  ls  df  del NAME\n"
    "  rotate 0|90|180|270|auto\n"
    "  lang vi|en|zh       switch the UI language\n"
    "  snap                 write SCREEN.RAW (240x520 RGB565) for the host\n"
    "  shot                 PNG screenshot -> UserData/Pictures\n"
    "  rec start|stop|status  QVF screen recording -> UserData/Videos\n"
    "  diag                 watchdog/charger/thermal census\n"
    "  pstore               write PSTORE.TXT (prior-boot reset reason)\n"
    "  flash recovery BOOTIMG.BIN confirm\n\n"
    "--- sync\n"
    "  DEVINFO.TXT carries VQEAF-SYNC/1 machine state for VQEAF Tool.\n";

void rescue_set_partition_path(const char *path) {
    g_partition = path ? path : "/dev/mmcblk0p77";
}

void rescue_set_snapshot_hook(rescue_snapshot_fn fn) {
    g_snapshot = fn;
}

void rescue_set_screenshot_hook(rescue_screenshot_fn fn) {
    g_screenshot = fn;
}

void rescue_set_language_hook(rescue_language_fn fn) {
    g_language = fn;
}

void rescue_set_status_hook(rescue_status_fn fn) {
    g_status = fn;
}

void rescue_set_tap_hook(rescue_tap_fn fn) {
    g_tap = fn;
}

void rescue_set_power_hook(rescue_power_fn fn) {
    g_power = fn;
}

void rescue_set_hold_hook(rescue_hold_fn fn) {
    g_hold = fn;
}

void rescue_set_swipe_hook(rescue_swipe_fn fn) {
    g_swipe = fn;
}

void rescue_set_page_hook(rescue_page_fn fn) {
    g_page = fn;
}

void rescue_set_fault_hook(rescue_fault_fn fn) {
    g_fault = fn;
}

void rescue_set_unlock_hook(rescue_unlock_fn fn) {
    g_unlock = fn;
}

void rescue_note(const char *line) {
    if (line != NULL) note(line);
}

void rescue_flush_log(struct fat16_volume *vol) {
    if (vol == NULL) return;
    put(vol, "QEAFBOOT.LOG", g_log);
}

int rescue_last_result(void) { return g_result; }

void rescue_init(struct fat16_volume *vol) {
    g_last_len = -1;
    g_result = 0;
    g_log_len = 0;
    g_log[0] = '\0';
    if (!vol) return;
    put(vol, "HELP.TXT", HELP);
    put(vol, "SUMMARY.TXT", SUMMARY);
    note("[    0.000] N qeaf: " QEAF_OS_STRING " starting (pid 1)");
    note("[    0.010] N qeaf: rescue channel bound to the RAM LUN");
    put(vol, "QEAFBOOT.LOG", g_log);
    put(vol, "RESPONSE.TXT", "seq=0\nno command has run yet\n");
    umos_logf("rescue: channel files published (QEAF.CMD reader ready)\n");
}

/* ---- one flash ------------------------------------------------------------ */

/* The partition is compared back one probe-sized block at a time: a streamed
 * flash never holds the whole image in one buffer. */
static uint8_t g_flash_probe[64 * 1024];

static int flash_verify_run(size_t offset, const uint8_t *expected, size_t len) {
    while (len > 0) {
        size_t chunk = len < sizeof(g_flash_probe) ? len : sizeof(g_flash_probe);

        if (sysio_block_read_at(g_partition, offset, g_flash_probe, chunk) != 0)
            return -1;
        if (memcmp(g_flash_probe, expected, chunk) != 0) return -2;
        offset += chunk;
        expected += chunk;
        len -= chunk;
    }
    return 0;
}

static void do_flash(struct fat16_volume *vol, char *resp, int cap) {
    struct fat16_file file;
    const uint8_t *chunk = NULL;
    size_t written = 0;
    unsigned runs = 0;
    int n, rc;

    if (fat16_file_open(vol, "BOOTIMG.BIN", &file) < 0) {
        cat(resp, cap, "flash: REFUSED - no BOOTIMG.BIN on the drive\n");
        note("[    -    ] E qeaf: flash refused, BOOTIMG.BIN is missing");
        g_result = -1;
        return;
    }
    if (file.size > P77_SIZE || file.size < 4096) {
        cat(resp, cap, "flash: REFUSED - p77 size check failed\n");
        note("[    -    ] E qeaf: flash refused, image size outside p77");
        g_result = -2;
        return;
    }
    n = fat16_file_next(vol, &file, &chunk);
    /* Eight-byte boot-image format magic (kept as raw bytes: the format
     * token is fixed by the bootloader, not a runtime brand). */
    static const unsigned char boot_magic[8] = {
        0x41, 0x4E, 0x44, 0x52, 0x4F, 0x49, 0x44, 0x21
    };
    if (n < 8 || memcmp(chunk, boot_magic, sizeof(boot_magic)) != 0) {
        cat(resp, cap, "flash: REFUSED - boot image magic missing\n");
        note("[    -    ] E qeaf: flash refused, no boot image magic");
        g_result = -2;
        return;
    }
    note("[    -    ] N qeaf: lun: running flash recovery BOOTIMG.BIN confirm");
    /* The cluster chain decides the write pattern, not the file's placement:
     * each run lands at the offset its bytes occupy in the file, so a
     * BOOTIMG.BIN the host scattered over the volume flashes unchanged. */
    for (;;) {
        rc = sysio_block_write_at(g_partition, written, chunk, (size_t)n);
        if (rc != 0) {
        char line[160];
        line[0] = '\0';
        cat(line, sizeof(line), "flash: FAILED - partition write returned an error (errno ");
        cat_u(line, sizeof(line), (unsigned)(sysio_last_errno() < 0
                                             ? -sysio_last_errno()
                                             : sysio_last_errno()));
        cat(line, sizeof(line), ")");
        cat(resp, cap, line);
        cat(resp, cap, "\n");
        note("[    -    ] E qeaf: flash: partition write failed");
        note(line);
        g_result = -3;
        return;
    }
        written += (size_t)n;
        runs++;
        n = fat16_file_next(vol, &file, &chunk);
        if (n < 0) {
            cat(resp, cap, "flash: FAILED - BOOTIMG.BIN chain broke mid-flash\n");
            note("[    -    ] E qeaf: flash: cluster chain ended early");
            g_result = -3;
            return;
        }
        if (n == 0) break;
    }
    if (written != file.size) {
        cat(resp, cap, "flash: FAILED - short read of BOOTIMG.BIN\n");
        note("[    -    ] E qeaf: flash: short read of BOOTIMG.BIN");
        g_result = -3;
        return;
    }
    /* Second walk over the same chain: read the partition back and compare. */
    if (fat16_file_open(vol, "BOOTIMG.BIN", &file) < 0) {
        cat(resp, cap, "flash: FAILED - BOOTIMG.BIN vanished before verify\n");
        note("[    -    ] E qeaf: flash: BOOTIMG.BIN vanished before verify");
        g_result = -4;
        return;
    }
    written = 0;
    while ((n = fat16_file_next(vol, &file, &chunk)) > 0) {
        if (flash_verify_run(written, chunk, (size_t)n) != 0) {
            cat(resp, cap, "flash: FAILED - readback did not match\n");
            note("[    -    ] E qeaf: flash: readback mismatch");
            g_result = -4;
            return;
        }
        written += (size_t)n;
    }
    if (n < 0 || written != file.size) {
        cat(resp, cap, "flash: FAILED - readback did not match\n");
        note("[    -    ] E qeaf: flash: readback mismatch");
        g_result = -4;
        return;
    }
    cat(resp, cap, "flash: written and verified byte for byte\n");
    if (runs > 1) {
        cat(resp, cap, "flash: BOOTIMG.BIN was fragmented - walked ");
        cat_u(resp, cap, runs);
        cat(resp, cap, " cluster runs\n");
    }
    {
        char line[160];
        line[0] = '\0';
        cat(line, sizeof(line),
            "[    -    ] N qeaf: lun: verified - recovery and BOOTIMG.BIN "
            "agree byte for byte over ");
        cat_u(line, sizeof(line), (unsigned)file.size);
        cat(line, sizeof(line), " bytes in ");
        cat_u(line, sizeof(line), runs);
        cat(line, sizeof(line), " cluster runs");
        note(line);
    }
    g_result = 1;
}

/* ---- reset evidence (read-only) -------------------------------------------
 * The RAM LUN dies on every reset, so the live boot trace can never carry the
 * reason the PREVIOUS boot went down. pstore/ramoops keeps the prior kernel
 * console across a warm reset; /proc/last_kmsg and /proc/bootreason do the
 * same on this vendor kernel. This verb only READS those and republishes them
 * as PSTORE.TXT so the host can raw-read the reset reason. No partition write.
 */
#ifdef __linux__
#define EVID_CAP      (160u * 1024u)
#define EVID_FILE_CAP (128u * 1024u)
static char g_evid[EVID_CAP];
static char g_evid_tmp[EVID_FILE_CAP];
static int g_evid_len;

static void evid_n(const char *s, int n) {
    int i;
    for (i = 0; i < n && g_evid_len + 1 < (int)sizeof(g_evid); i++)
        g_evid[g_evid_len++] = s[i];
    g_evid[g_evid_len] = '\0';
}
static void evid_s(const char *s) { evid_n(s, slen(s)); }
static void evid_u(unsigned v) {
    char t[12]; int k = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k > 0) { char c[2]; c[0] = t[--k]; c[1] = '\0'; evid_s(c); }
}
/* Keep only the last `keep` bytes of a file: the reset reason sits at the END
 * of a console log. A bounded buffer slides forward so a multi-MB ramoops can
 * never overflow it. Returns total bytes seen, or -1 if open failed. */
static int evid_file(const char *path, int keep) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    int total = 0, grand = 0, r;
    if (fd < 0) return -1;
    if (keep > (int)sizeof(g_evid_tmp) - 1) keep = (int)sizeof(g_evid_tmp) - 1;
    for (;;) {
        r = (int)read(fd, g_evid_tmp + total,
                      sizeof(g_evid_tmp) - (size_t)total - 1);
        if (r <= 0) break;
        total += r; grand += r;
        if (total >= (int)sizeof(g_evid_tmp) - 1) {
            memmove(g_evid_tmp, g_evid_tmp + (total - keep), (size_t)keep);
            total = keep;
        }
    }
    close(fd);
    evid_s("=== "); evid_s(path); evid_s(" ("); evid_u((unsigned)grand);
    evid_s(" bytes) ===\n");
    if (total > keep) {
        evid_s("[head dropped, tail kept]\n");
        evid_n(g_evid_tmp + (total - keep), keep);
    } else {
        evid_n(g_evid_tmp, total);
    }
    evid_s("\n");
    return grand;
}
static void dump_reset_evidence(struct fat16_volume *vol, char *resp, int cap) {
    DIR *d;
    struct dirent *de;
    int pstore_files = 0, lk, br;
    g_evid_len = 0; g_evid[0] = '\0';
    evid_s("QEAF reset evidence (read-only): prior-boot kernel console + PON\n");
    d = opendir("/sys/fs/pstore");
    if (d != NULL) {
        while ((de = readdir(d)) != NULL) {
            char path[192];
            int keep;
            if (eq(de->d_name, ".") || eq(de->d_name, "..")) continue;
            path[0] = '\0';
            cat(path, (int)sizeof(path), "/sys/fs/pstore/");
            cat(path, (int)sizeof(path), de->d_name);
            keep = starts(de->d_name, "console") ? 96 * 1024 : 16 * 1024;
            if (evid_file(path, keep) >= 0) pstore_files++;
        }
        closedir(d);
    } else {
        evid_s("/sys/fs/pstore: absent (");
        evid_s(strerror(errno));
        evid_s(")\n");
    }
    lk = evid_file("/proc/last_kmsg", 96 * 1024);
    br = evid_file("/proc/bootreason", 4096);
    fat16_write_file(vol, "PSTORE.TXT", (const uint8_t *)g_evid,
                     (size_t)g_evid_len);
    cat(resp, cap, "pstore: wrote PSTORE.TXT (");
    cat_u(resp, cap, (unsigned)g_evid_len);
    cat(resp, cap, " bytes); pstore_files=");
    cat_u(resp, cap, (unsigned)pstore_files);
    cat(resp, cap, "; last_kmsg=");
    cat(resp, cap, lk >= 0 ? "present" : "absent");
    cat(resp, cap, "; bootreason=");
    cat(resp, cap, br >= 0 ? "present" : "absent");
    cat(resp, cap, "\n");
    note("[    -    ] N qeaf: pstore reset evidence republished");
    g_result = 1;
}

/* ---- device census (read-only) --------------------------------------------
 * A boot that resets without a fault record needs the other half of the
 * story: is there a watchdog driver at all, what does the charger report,
 * how hot is the SoC. This verb only reads sysfs and says what it found.
 */
static int diag_has(const char *name, const char *needle)
{
    int i, j;

    for(i = 0; name[i]; i++) {
        for(j = 0; needle[j]; j++) {
            char c = name[i + j];
            if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if(c != needle[j]) break;
        }
        if(needle[j] == '\0') return 1;
    }
    return 0;
}

static void diag_file(char *resp, int cap, const char *path)
{
    char buf[96];
    ssize_t n;
    int fd, i = 0;

    cat(resp, cap, "diag: ");
    cat(resp, cap, path);
    cat(resp, cap, "=");
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) {
        cat(resp, cap, "absent\n");
        return;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if(n <= 0) {
        cat(resp, cap, "empty\n");
        return;
    }
    while(i < (int)n && buf[i] != '\n' && buf[i] != '\r' &&
          i < (int)sizeof(buf) - 1)
        i++;
    buf[i] = '\0';
    cat(resp, cap, buf);
    cat(resp, cap, "\n");
}

static void diag_dir(char *resp, int cap, const char *dir, const char *needle)
{
    DIR *d;
    struct dirent *de;
    int shown = 0;

    cat(resp, cap, "diag: ");
    cat(resp, cap, dir);
    cat(resp, cap, ": ");
    d = opendir(dir);
    if(d == NULL) {
        cat(resp, cap, "absent (");
        cat(resp, cap, strerror(errno));
        cat(resp, cap, ")\n");
        return;
    }
    while((de = readdir(d)) != NULL) {
        if(de->d_name[0] == '.') continue;
        if(needle != NULL && !diag_has(de->d_name, needle)) continue;
        if(shown != 0) cat(resp, cap, " ");
        cat(resp, cap, de->d_name);
        if(++shown >= 24) {
            cat(resp, cap, " ...");
            break;
        }
    }
    closedir(d);
    cat(resp, cap, shown == 0 ? "(none)\n" : "\n");
}

/* One key=value token out of a single-line file (the kernel cmdline): the
 * bootloader records why the previous boot ended right there. */
static void diag_key(char *resp, int cap, const char *path, const char *key)
{
    char buf[1024];
    ssize_t n;
    int fd, i;
    size_t klen = strlen(key);

    cat(resp, cap, "diag: ");
    cat(resp, cap, path);
    cat(resp, cap, "[");
    cat(resp, cap, key);
    cat(resp, cap, "]=");
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) {
        cat(resp, cap, "absent\n");
        return;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if(n <= 0) {
        cat(resp, cap, "empty\n");
        return;
    }
    buf[n] = '\0';
    for(i = 0; i + (int)klen < (int)n; i++) {
        if(memcmp(buf + i, key, klen) != 0) continue;
        i += (int)klen;
        if(buf[i] == '=') i++;
        while(i < (int)n && buf[i] != ' ' && buf[i] != '\n' && buf[i] != '\r') {
            char one[2];
            one[0] = buf[i++];
            one[1] = '\0';
            cat(resp, cap, one);
        }
        cat(resp, cap, "\n");
        return;
    }
    cat(resp, cap, "(not in cmdline)\n");
}

static void dump_device_diag(char *resp, int cap)
{
    int i;

    cat(resp, cap, "diag: watchdog / power / thermal census\n");
    cat(resp, cap, "diag: /dev/watchdog ");
    cat(resp, cap, access("/dev/watchdog", F_OK) == 0 ? "present\n" : "absent\n");
    diag_dir(resp, cap, "/sys/class/watchdog", NULL);
    diag_dir(resp, cap, "/sys/module", "wdt");
    diag_dir(resp, cap, "/vendor/lib/modules", "wdt");
    diag_dir(resp, cap, "/vendor/lib/modules", "watchdog");
    diag_file(resp, cap, "/sys/class/power_supply/battery/capacity");
    diag_file(resp, cap, "/sys/class/power_supply/battery/status");
    diag_file(resp, cap, "/sys/class/power_supply/battery/temp");
    diag_file(resp, cap, "/sys/class/power_supply/usb/online");
    diag_dir(resp, cap, "/proc/device-tree/chosen", NULL);
    diag_file(resp, cap, "/proc/device-tree/chosen/pureason");
    diag_file(resp, cap, "/proc/device-tree/chosen/bootargs");
    for(i = 0; i < 4; i++) {
        char path[64];
        snprintf(path, sizeof(path),
                 "/sys/class/thermal/thermal_zone%d/temp", i);
        diag_file(resp, cap, path);
    }
    note("[    -    ] N qeaf: device diag answered");
    g_result = 1;
}
#endif

/* ---- verb handling -------------------------------------------------------- */

static void run_verbs(struct fat16_volume *vol, const char *cmd, int len,
                      char *resp, int cap) {
    int i = 0;
    while (i < len) {
        char line[256];
        int n = 0;
        while (i < len && (cmd[i] == '\n' || cmd[i] == '\r')) i++;
        while (i < len && cmd[i] != '\n' && cmd[i] != '\r' && n + 1 < (int)sizeof(line))
            line[n++] = cmd[i++];
        line[n] = '\0';
        while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = '\0';
        if (!n) continue;

        cat(resp, cap, "### ");
        cat(resp, cap, line);
        cat(resp, cap, "\n");
        if (starts(line, "echo ")) {
            cat(resp, cap, line + 5);
            cat(resp, cap, "\n");
        } else if (eq(line, "help")) {
            cat(resp, cap, HELP);
        } else if (starts(line, "rotate ")) {
            int o = display_parse_orientation(line + 7);
            if (o == -2) {
                display_set_auto_rotate(1);
                cat(resp, cap, "rotate: auto-rotate on\n");
            } else if (o >= 0) {
                display_set_auto_rotate(0);
                cat(resp, cap, "rotate: orientation -> ");
                cat(resp, cap, display_orientation_name(
                        display_set_orientation((enum display_orientation)o)));
                cat(resp, cap, "\n");
            } else {
                cat(resp, cap, "rotate: unknown value (0|90|180|270|auto)\n");
            }
        } else if (starts(line, "lang ")) {
            const char *v = line + 5;
            int lang = -1;
            if (eq(v, "vi")) lang = 0;
            else if (eq(v, "en")) lang = 1;
            else if (eq(v, "zh")) lang = 3;
            if (lang < 0) {
                cat(resp, cap, "lang: unknown value (vi|en|zh)\n");
            } else if (g_language == NULL) {
                cat(resp, cap, "lang: unavailable (no language hook)\n");
            } else if (g_language(lang) == 1) {
                /* The platform took the value but the UI is on another thread,
                 * so the desktop rebuild happens on its next frame. */
                cat(resp, cap, "lang: queued ");
                cat(resp, cap, v);
                cat(resp, cap, " (desktop not on this thread yet)\n");
                note("[    -    ] N qeaf: lang queued from QEAF.CMD");
                g_result = 1;
            } else if (g_language(lang) == 0) {
                cat(resp, cap, "lang: applied ");
                cat(resp, cap, v);
                cat(resp, cap, "\n");
                note("[    -    ] N qeaf: lang applied from QEAF.CMD");
                g_result = 1;
            } else {
                cat(resp, cap, "lang: refused (apply failed)\n");
                note("[    -    ] E qeaf: lang apply failed");
                g_result = -5;
            }
        } else if (eq(line, "caps")) {
            VioHwProbe probe;
            if (vio_caps_probe_device(&probe) == 0)
                vio_caps_publish(&probe);
            char cbuf[2048];
            int n = vio_caps_report(cbuf, (int)sizeof(cbuf));
            if (n > 0) cat(resp, cap, cbuf);
            else cat(resp, cap, "caps: registry empty\n");
            note("[    -    ] N qeaf: caps census reported");
            g_result = 1;
        } else if (eq(line, "diag")) {
            dump_device_diag(resp, cap);
        } else if (eq(line, "cam") || starts(line, "cam ")) {
            if (eq(line, "cam subs")) {
                char sbuf[4096];
                int n = vqeaf_camera_subdevs(sbuf, (int)sizeof(sbuf));
                if (n > 0) cat(resp, cap, sbuf);
                else cat(resp, cap, "cam subs: none\n");
                g_result = 1;
            } else if (eq(line, "cam session")) {
                char err[200];
                int32_t h = 0;
                int rc = vqeaf_camera_create_session(&h, err, (int)sizeof(err));
                cat(resp, cap, "cam session: ");
                if (rc == 0) {
                    cat(resp, cap, "created handle=0x");
                    {
                        char hx[16];
                        snprintf(hx, sizeof(hx), "%08x", (unsigned)h);
                        cat(resp, cap, hx);
                    }
                } else {
                    cat(resp, cap, "failed: ");
                    cat(resp, cap, err);
                }
                cat(resp, cap, "\n");
                g_result = 1;
            } else if (eq(line, "cam caps")) {
                char cbuf[8192];
                int n = vqeaf_camera_query_caps(cbuf, (int)sizeof(cbuf));
                if (cbuf[0]) cat(resp, cap, cbuf);
                else cat(resp, cap, "cam caps: no answer\n");
                (void)n;
                g_result = 1;
            } else if (eq(line, "cam link")) {
                char cbuf[8192];
                int n = vqeaf_camera_acquire_link(cbuf, (int)sizeof(cbuf));
                if (cbuf[0]) cat(resp, cap, cbuf);
                else cat(resp, cap, "cam link: no answer\n");
                (void)n;
                g_result = 1;
            } else if (starts(line, "cam grab")) {
                char path[128] = "/dev/video32";
                char err[200];
                VqeafCameraFrame frame;
                int rc;
                if (sscanf(line + 8, "%127s", path) != 1)
                    snprintf(path, sizeof(path), "/dev/video32");
                memset(&frame, 0, sizeof(frame));
                rc = vqeaf_camera_grab(path, &frame, err, (int)sizeof(err));
                cat(resp, cap, "cam grab ");
                cat(resp, cap, path);
                cat(resp, cap, ": ");
                if (rc == 0) {
                    vqeaf_cap_set("camera", VQEAF_CAP_READY, 1u,
                                  (int32_t)frame.bytes_used);
                    cat(resp, cap, "frame ");
                    cat_u(resp, cap, frame.width);
                    cat(resp, cap, "x");
                    cat_u(resp, cap, frame.height);
                    cat(resp, cap, " bytes=");
                    cat_u(resp, cap, frame.bytes_used);
                    {
                        char h[16];
                        snprintf(h, sizeof(h), "%08x", frame.hash);
                        cat(resp, cap, " hash=0x");
                        cat(resp, cap, h);
                    }
                    free(frame.data);
                } else {
                    cat(resp, cap, "failed: ");
                    cat(resp, cap, err);
                }
                cat(resp, cap, "\n");
            } else {
                char cbuf[4096];
                int n = vqeaf_camera_probe(cbuf, (int)sizeof(cbuf));
                if (n > 0) cat(resp, cap, cbuf);
                else cat(resp, cap, "cam: probe failed\n");
            }
            note("[    -    ] N qeaf: native camera census answered");
            g_result = 1;
        } else if (eq(line, "status")) {
            cat(resp, cap, QEAF_OS_STRING "; device violet; usb_mode=");
            cat(resp, cap, usb_gadget_mode_name(usb_gadget_mode()));
            cat(resp, cap, "; usb_cable=");
            cat_u(resp, cap, (unsigned)usb_gadget_cable());
            cat(resp, cap, "; orientation=");
            cat(resp, cap, display_orientation_name(display_orientation()));
            cat(resp, cap, "; auto_rotate=");
            cat_u(resp, cap, (unsigned)display_auto_rotate());
            cat(resp, cap, "; control_lun=QEAFRESCUE");
            if (g_status != NULL) g_status(resp, cap);
            cat(resp, cap, "\n");
        } else if (starts(line, "wifi ") || eq(line, "wifi")) {
            int sub = 0; /* 0 = summary, 1 = net, 2 = dmesg, 3 = sdio, 4 = mod, 5 = fw */
            if (eq(line, "wifi off")) {
                vio_wifi_modem_stop();
                cat(resp, cap, "wifi: modem released; bring-up stopped\n");
                note("[    -    ] N qeaf: modem released on request");
                g_result = 1;
                continue;
            }
            if (starts(line, "wifi ")) {
                const char *subname = line + 5;
                if (eq(subname, "net")) sub = 1;
                else if (eq(subname, "dmesg")) sub = 2;
                else if (eq(subname, "sdio")) sub = 3;
                else if (eq(subname, "mod")) sub = 4;
                else if (eq(subname, "fw")) sub = 5;
                else if (eq(subname, "firmware")) sub = 5;
                else if (eq(subname, "up")) sub = 6;
                else if (eq(subname, "bringup")) sub = 6;
                else if (eq(subname, "state")) sub = 7;
                else if (eq(subname, "modem")) sub = 8;
                else if (eq(subname, "sys")) sub = 8;
                else if (eq(subname, "host")) sub = 9;
                else if (eq(subname, "log")) sub = 10;
                else if (starts(subname, "find ")) sub = 11;
                else if (starts(subname, "ls ")) sub = 12;
                else if (starts(subname, "link ")) sub = 13;
                else if (starts(subname, "cat ")) sub = 14;
                else if (eq(subname, "on")) sub = 15;
                else if (starts(subname, "pd ")) sub = 16;
                else if (starts(subname, "listen ")) sub = 17;
                else if (eq(subname, "svc")) sub = 18;
                else if (eq(subname, "vendor")) sub = 19;
                else if (starts(subname, "rproc ")) sub = 20;
                else if (eq(subname, "sup")) sub = 21;
                else if (eq(subname, "gnss")) sub = 22;
                else if (eq(subname, "qlookup")) sub = 23;
                else if (eq(subname, "native")) sub = 24;
                else if (eq(subname, "diag")) sub = 25;
                else if (eq(subname, "discover") || starts(subname, "discover ")) sub = 26;
                else { cat(resp, cap, "wifi: unknown sub (net|dmesg|sdio|mod|fw|up|state|modem|host|log|find NAME|ls DIR|link PATH|cat FILE|on|pd NODE PORT PD|listen NODE PORT PATH|svc|vendor|rproc N)\n"); }
            }
#ifdef __linux__
            if (sub == 19) {
                int rc = vio_host_vendor_wifi();
                cat(resp, cap, "wifi vendor: ");
                cat(resp, cap, rc == 0 ? "started (see wifi log)"
                                       : "unavailable");
                cat(resp, cap, "\n");
                g_result = 1;
            }
            if (sub == 20) {
                char path[96];
                int n, fd;
                if(sscanf(line + 10, "%d", &n) != 1 || n < 0 || n > 7) {
                    cat(resp, cap, "wifi rproc: usage: wifi rproc N\n");
                } else {
                    snprintf(path, sizeof(path),
                             "/sys/class/remoteproc/remoteproc%d/state", n);
                    fd = open(path, O_WRONLY | O_CLOEXEC);
                    cat(resp, cap, "wifi rproc: ");
                    if(fd >= 0 && write(fd, "start", 5) == 5) {
                        cat(resp, cap, "start sent\n");
                    } else {
                        cat(resp, cap, "failed (errno ");
                        cat_u(resp, cap, (unsigned)errno);
                        cat(resp, cap, ")\n");
                    }
                    if(fd >= 0) close(fd);
                }
                g_result = 1;
            }
            if (sub == 21) {
                static const char *const args[] = { "-Dnl80211", "-i", "wlan0",
                    "-c", "/vendor/etc/wifi/wpa_supplicant.conf", NULL };
                int rc = vio_host_run_bionic(
                    "/vendor/bin/hw/wpa_supplicant", "wpa_supplicant", args);
                cat(resp, cap, "wifi sup: ");
                cat(resp, cap, rc == 0 ? "started (see wifi log)" : "refused");
                cat(resp, cap, "\n");
                g_result = 1;
            }
            if (sub == 22) {
                static const char *const args[] = { NULL };
                int rc = vio_host_run_bionic(
                    "/vendor/bin/hw/android.hardware.gnss@2.1-service-qti",
                    "gnss-hal", args);
                cat(resp, cap, "wifi gnss: ");
                cat(resp, cap, rc == 0 ? "started (see wifi log)" : "refused");
                cat(resp, cap, "\n");
                g_result = 1;
            }
            if (sub == 25) {
                char diag[4096];
                vio_wifi_diag(diag, (int)sizeof(diag));
                cat(resp, cap, diag);
                /* HOMOLOG.TXT is owned by the boot-trace publisher and is
                 * rewritten every poll; use a dedicated 8.3 diary file. */
                put(vol, "WIFIDIAG.TXT", diag);
                note("[    -    ] N qeaf: wifi diag written to WIFIDIAG.TXT");
                g_result = 1;
            }
            if (sub == 26) {
                /* Real IO: native QRTR client over AF_QIPCRTR. Read-only with
                 * respect to partitions; only NEW_LOOKUP is sent. */
                uint32_t svc = 0x45;
                char rep[1024];
                if (starts(line, "wifi discover "))
                    (void)sscanf(line + 14, "%x", &svc);
                (void)vio_qrtr_discover_live(svc, 1, 1, 1500,
                                             rep, (int)sizeof(rep));
                cat(resp, cap, rep);
                /* 8.3 name: WIFIDISCOVER is too long for the rescue FAT. */
                put(vol, "QRLIVE.TXT", (const uint8_t *)rep);
                note("[    -    ] N qeaf: live QRTR discovery evidence written");
                g_result = 1;
            }
            if (sub == 23) {
                vio_host_qrtr_lookup();
                cat(resp, cap, "wifi qlookup: launched (see wifi log)\n");
                g_result = 1;
            }
            if (sub == 24) {
                int rc = vio_host_wlfw_native();
                cat(resp, cap, "wifi native: ");
                cat(resp, cap, rc == 0 ? "qwlfgw launched" : "unavailable");
                cat(resp, cap, "\n");
                g_result = 1;
            }
            if (sub == 18) {
                char r[6000];
                vio_qrtr_ns_report(r, (int)sizeof(r));
                cat(resp, cap, r);
                g_result = 1;
            }
            if (sub == 17) {
                char node[32], port[32], path[96];
                if(sscanf(line + 12, "%31s %31s %95s", node, port, path) != 3) {
                    cat(resp, cap, "wifi listen: usage: wifi listen NODE PORT PATH\n");
                } else if(vio_host_pd_listen(node, port, path) == 0) {
                    cat(resp, cap, "wifi listen: requested (see wifi log)\n");
                } else {
                    cat(resp, cap, "wifi listen: tool missing\n");
                }
                g_result = 1;
            }
            if (sub == 16) {
                char node[32], port[32], pd[96];
                int rc;
                if(sscanf(line + 8, "%31s %31s %95s", node, port, pd) != 3) {
                    cat(resp, cap, "wifi pd: usage: wifi pd NODE PORT PD-NAME\n");
                } else {
                    rc = vio_host_pd_restart(node, port, pd);
                    cat(resp, cap, "wifi pd: ");
                    cat(resp, cap, rc == 0 ? "requested" : "refused");
                    cat(resp, cap, " (see wifi log)\n");
                }
                g_result = 1;
            }
            if (sub == 15) {
                int rc = vio_host_wlan_on();
                cat(resp, cap, "wifi on: ");
                cat(resp, cap, rc == 0 ? "accepted" : "refused");
                cat(resp, cap, " (errno ");
                cat_u(resp, cap, (unsigned)(rc == 0 ? 0 : -rc));
                cat(resp, cap, ")\n");
                g_result = rc == 0 ? 1 : 1;
            }
            if (sub == 14) {
                const char *path = line + 9; /* after "wifi cat " */
                int fd = open(path, O_RDONLY | O_CLOEXEC);
                char buf[2048];
                ssize_t n = fd >= 0 ? read(fd, buf, sizeof(buf) - 1) : -1;
                cat(resp, cap, path);
                cat(resp, cap, ": ");
                if(n > 0) { buf[n] = '\0'; cat(resp, cap, buf); }
                else { cat(resp, cap, "unreadable (errno ");
                       cat_u(resp, cap, (unsigned)errno); cat(resp, cap, ")"); }
                if(fd >= 0) close(fd);
                cat(resp, cap, "\n");
                g_result = 1;
            }
            if (sub == 13) {
                const char *path = line + 10; /* after "wifi link " */
                char target[256];
                ssize_t n = readlink(path, target, sizeof(target) - 1);
                cat(resp, cap, path);
                cat(resp, cap, " -> ");
                if(n > 0) { target[n] = '\0'; cat(resp, cap, target); }
                else { cat(resp, cap, "not a link (errno "); cat_u(resp, cap, (unsigned)errno);
                       cat(resp, cap, ")"); }
                cat(resp, cap, "\n");
                g_result = 1;
            }
            if (sub == 12) {
                const char *path = line + 8; /* after "wifi ls " */
                DIR *d = opendir(path);
                struct dirent *e;
                int n = 0;
                cat(resp, cap, path);
                cat(resp, cap, ": ");
                while(d && (e = readdir(d)) != NULL) {
                    if(e->d_name[0] == '.') continue;
                    if(n++ >= 200) break;
                    cat(resp, cap, e->d_name);
                    cat(resp, cap, " ");
                }
                if(!d || n == 0) cat(resp, cap, "(absent or empty)");
                if(d) closedir(d);
                cat(resp, cap, "\n");
                g_result = 1;
            }
            if (sub == 11) {
                const char *needle = line + 10; /* after "wifi find " */
                char found[256];
                cat(resp, cap, "wifi find ");
                cat(resp, cap, needle);
                cat(resp, cap, ": ");
                if(vio_host_find_file(needle, found, (int)sizeof(found)) == 0)
                    cat(resp, cap, found);
                else
                    cat(resp, cap, "not found");
                cat(resp, cap, "\n");
                g_result = 1;
            }
            if (sub == 10) {
                char logs[7200];
                vio_host_services_logs(logs, (int)sizeof(logs));
                cat(resp, cap, logs);
                note("[    -    ] N qeaf: wifi host logs answered");
                g_result = 1;
            }
            if (sub == 9) {
                /* Only the host-service census: small enough that it can
                 * never be pushed out of RESPONSE.TXT by a long dmesg tail. */
                char svc[512];
                vio_host_services_report(svc, (int)sizeof(svc));
                cat(resp, cap, svc);
                note("[    -    ] N qeaf: wifi host services answered");
                g_result = 1;
            }
            if (sub == 5) {
                /* Firmware probe: list the firmware directories the driver asks for. */
                const char *dirs[] = {"/vendor/firmware", "/system/vendor/firmware",
                                      "/etc/firmware", "/system/etc/firmware",
                                      "/lib/firmware", "/usr/share/firmware"};
                for (unsigned di = 0; di < sizeof(dirs)/sizeof(dirs[0]); di++) {
                    DIR *d = opendir(dirs[di]);
                    cat(resp, cap, "wifi fw: ");
                    cat(resp, cap, dirs[di]);
                    cat(resp, cap, ": ");
                    if (d == NULL) cat(resp, cap, "absent\n");
                    else {
                        struct dirent *e; int any = 0, hit = 0;
                        while ((e = readdir(d)) != NULL) {
                            if (e->d_name[0] == '.') continue;
                            if (strstr(e->d_name, "WCN") != NULL || strstr(e->d_name, "wcn") != NULL ||
                                strstr(e->d_name, "wlan") != NULL || strstr(e->d_name, "qca") != NULL ||
                                strstr(e->d_name, "firmware") != NULL) hit++;
                            if (any < 40) { if (any++) cat(resp, cap, ", "); cat(resp, cap, e->d_name); }
                        }
                        closedir(d);
                        if (any == 0) cat(resp, cap, "(empty)");
                        else { cat(resp, cap, " ("); cat_u(resp, cap, (unsigned)hit); cat(resp, cap, " wlan-related)"); }
                        cat(resp, cap, "\n");
                    }
                }
                note("[    -    ] N qeaf: wifi fw probe answered");
                /* The packaging step writes this report into the initramfs.
                 * Print it verbatim so the host sees which WCN3990 blobs the
                 * image actually carries, not just which directories exist. */
                {
                    int fd = open("/etc/qeaf/radio-firmware-status.txt",
                                  O_RDONLY | O_CLOEXEC);
                    cat(resp, cap, "wifi fw report: ");
                    if (fd < 0) {
                        cat(resp, cap, "absent\n");
                    } else {
                        char rep[1024];
                        ssize_t r = read(fd, rep, sizeof(rep) - 1);
                        close(fd);
                        if (r <= 0) {
                            cat(resp, cap, "empty\n");
                        } else {
                            rep[r] = '\0';
                            cat(resp, cap, "\n");
                            cat(resp, cap, rep);
                            if (rep[r - 1] != '\n') cat(resp, cap, "\n");
                        }
                    }
                }
            } else if (sub == 6) {
                /* Force a fresh mount/param pass. Idempotent: already-mounted
                 * partitions return success, so this is safe to repeat.
                 * Starting the modem is insecure without the vendor service
                 * layer, so it only happens on this explicit request. */
                vio_wifi_modem_start();
                vio_wifi_bringup();
                vio_wifi_poll();
                cat(resp, cap, "wifi up: ");
                cat(resp, cap, vio_wifi_state_text());
                cat(resp, cap, "\n");
                {
                    char rep[3072];
                    vio_wifi_report(rep, (int)sizeof(rep));
                    cat(resp, cap, rep);
                }
                note("[    -    ] N qeaf: wifi bring-up requested");
            } else if (sub == 7) {
                char rep[3072];
                vio_wifi_report(rep, (int)sizeof(rep));
                cat(resp, cap, rep);
                note("[    -    ] N qeaf: wifi state answered");
            } else if (sub == 8) {
                char rep[4096];
                vio_wifi_sysdump(rep, (int)sizeof(rep));
                cat(resp, cap, rep);
                note("[    -    ] N qeaf: wifi modem census answered");
            } else if (sub == 1) {
                DIR *d = opendir("/sys/class/net");
                cat(resp, cap, "wifi net: ");
                if (d == NULL) cat(resp, cap, "cannot open\n");
                else {
                    struct dirent *e;
                    char pp[96], buf[128];
                    int cnt = 0;
                    while ((e = readdir(d)) != NULL) {
                        if (e->d_name[0] == '.') continue;
                        if (cnt++) cat(resp, cap, " | ");
                        cat(resp, cap, e->d_name);
                        snprintf(pp, sizeof(pp), "/sys/class/net/%s/operstate", e->d_name);
                        {
                            int f = open(pp, O_RDONLY | O_CLOEXEC);
                            if (f >= 0) { ssize_t n = read(f, buf, sizeof(buf)-1); close(f);
                                if (n > 0) { buf[n] = 0; while (n > 0 && (buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0; cat(resp, cap, " operstate="); cat(resp, cap, buf); } }
                        }
                        snprintf(pp, sizeof(pp), "/sys/class/net/%s/address", e->d_name);
                        {
                            int f = open(pp, O_RDONLY | O_CLOEXEC);
                            if (f >= 0) { ssize_t n = read(f, buf, sizeof(buf)-1); close(f);
                                if (n > 0) { buf[n] = 0; while (n > 0 && (buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0; cat(resp, cap, " addr="); cat(resp, cap, buf); } }
                        }
                        if (cnt > 16) break;
                    }
                    closedir(d);
                    cat(resp, cap, "\n");
                }
                note("[    -    ] N qeaf: wifi net probe answered");
            } else if (sub == 2) {
                int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                cat(resp, cap, "wifi dmesg: ");
                if (fd < 0) cat(resp, cap, "kmsg unavailable\n");
                else {
                    char buf[4096]; char tail[7168]; int tl = 0, found = 0; tail[0] = 0;
                    for (;;) {
                        ssize_t r = read(fd, buf, sizeof(buf)-1);
                        if (r <= 0) break;
                        buf[r] = 0;
                        char *msg = strchr(buf, ';'); msg = msg ? msg + 1 : buf;
                        const char *keys[] = {"wlan", "WCN", "cfg80211", "mac80211", "wifi", "firmware", "BSS", "associat", "SDIO", "icnss", "remoteproc", "subsys", "qrtr", "QMI", "qmi", "glink", "locator", "servreg", "ipa", "msa"};
                        int hit = 0;
                        for (unsigned k = 0; k < sizeof(keys)/sizeof(keys[0]); k++) if (strstr(msg, keys[k]) != NULL) hit = 1;
                        if (!hit) continue;
                        found++;
                        int len = slen(msg);
                        while (len > 0 && (msg[len-1]=='\n'||msg[len-1]=='\r')) len--;
                        if (tl + len + 2 >= (int)sizeof(tail)) { int keep = (int)sizeof(tail)/2; memmove(tail, tail+tl-keep, (size_t)keep); tl = keep; }
                        if (tl > 0) tail[tl++] = ' ';
                        int copy = len;
                        if (tl + copy >= (int)sizeof(tail)) copy = (int)sizeof(tail)-tl-1;
                        if (copy > 0) { memcpy(tail+tl, msg, (size_t)copy); tl += copy; tail[tl] = 0; }
                    }
                    close(fd);
                    if (!found || tl == 0) cat(resp, cap, "no wlan lines\n");
                    else { cat_u(resp, cap, (unsigned)found); cat(resp, cap, " lines: "); cat(resp, cap, tail); cat(resp, cap, "\n"); }
                }
                note("[    -    ] N qeaf: wifi dmesg probe answered");
            } else if (sub == 3) {
                const char *dirs[] = {"/sys/bus/mmc/devices", "/sys/bus/mmc/drivers", "/sys/bus/mmc_link"};
                for (unsigned di = 0; di < 3; di++) {
                    DIR *d = opendir(dirs[di]);
                    cat(resp, cap, "wifi sdio: ");
                    cat(resp, cap, dirs[di]);
                    cat(resp, cap, ": ");
                    if (d == NULL) cat(resp, cap, "absent\n");
                    else {
                        struct dirent *e; int any = 0;
                        while ((e = readdir(d)) != NULL) { if (e->d_name[0]=='.') continue; if (any++) cat(resp, cap, ", "); cat(resp, cap, e->d_name); if (any > 20) break; }
                        closedir(d);
                        if (!any) cat(resp, cap, "(empty)");
                        cat(resp, cap, "\n");
                    }
                }
                note("[    -    ] N qeaf: wifi sdio probe answered");
            } else if (sub == 4) {
                DIR *d = opendir("/sys/module");
                cat(resp, cap, "wifi mod: /sys/module: ");
                if (d == NULL) cat(resp, cap, "absent\n");
                else {
                    struct dirent *e; int any = 0;
                    while ((e = readdir(d)) != NULL) {
                        if (e->d_name[0]=='.') continue;
                        if (strstr(e->d_name, "wlan") == NULL && strstr(e->d_name, "WCN") == NULL &&
                            strstr(e->d_name, "cfg80211") == NULL && strstr(e->d_name, "mac80211") == NULL &&
                            strstr(e->d_name, "bluetooth") == NULL) continue;
                        if (any++) cat(resp, cap, ", ");
                        cat(resp, cap, e->d_name);
                    }
                    closedir(d);
                    if (!any) cat(resp, cap, "(no wlan/cfg/mac80211 module)");
                    cat(resp, cap, "\n");
                }
                DIR *lm = opendir("/lib/modules");
                cat(resp, cap, "wifi mod: /lib/modules: ");
                if (lm == NULL) cat(resp, cap, "absent\n");
                else { struct dirent *e; int any = 0; while ((e = readdir(lm)) != NULL) { if (e->d_name[0]=='.') continue; if (any++) cat(resp, cap, ", "); cat(resp, cap, e->d_name); } closedir(lm); if (!any) cat(resp, cap, "(empty)"); cat(resp, cap, "\n"); }
                note("[    -    ] N qeaf: wifi mod probe answered");
            } else {
            /* Driver probe, firmware untouched: list netdevs and filter dmesg
             * for wlan/WCN/cfg80211 lines. No lines = no driver yet. */
            DIR *d = opendir("/sys/class/net");
            cat(resp, cap, "wifi: /sys/class/net: ");
            if (d == NULL) {
                cat(resp, cap, "cannot open (no netdev?)\n");
            } else {
                struct dirent *e;
                int any = 0;
                while ((e = readdir(d)) != NULL) {
                    if (e->d_name[0] == '.') continue;
                    if (any) cat(resp, cap, ", ");
                    cat(resp, cap, e->d_name);
                    any = 1;
                }
                closedir(d);
                if (!any) cat(resp, cap, "(empty)");
                cat(resp, cap, "\n");
                char addr_path[80];
                cat(resp, cap, "wifi: wlan0 present: ");
                snprintf(addr_path, sizeof(addr_path), "/sys/class/net/wlan0");
                {
                    DIR *w = opendir(addr_path);
                    cat(resp, cap, w ? "yes\n" : "no\n");
                    if (w) closedir(w);
                }
            }
            {
                int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd < 0) {
                    cat(resp, cap, "wifi: kmsg unavailable\n");
                } else {
                    char buf[4096];
                    char tail[4096];
                    int tl = 0, found = 0;
                    tail[0] = '\0';
                    for (;;) {
                        ssize_t r = read(fd, buf, sizeof(buf) - 1);
                        if (r <= 0) break;
                        buf[r] = '\0';
                        char *msg = strchr(buf, ';');
                        msg = msg ? msg + 1 : buf;
                        char *low = msg;
                        int hit = 0;
                        const char *keys[] = {"wlan", "WCN", "cfg80211", "mac80211", "wifi", "WIFI", "icnss", "remoteproc", "subsys", "modem", "pd-mapper", "tqftp", "hostsvc", "WLFW", "qmi", "QMI", "firmware", "rmtfs", "RMTFS", "storage", "IPA", "ipa"};
                        for (unsigned k = 0; k < sizeof(keys)/sizeof(keys[0]); k++)
                            if (strstr(msg, keys[k]) != NULL) hit = 1;
                        (void)low;
                        if (!hit) continue;
                        found++;
                        /* strip the kmsg line-number prefix, take the text after ';' */
                        int len = slen(msg);
                        while (len > 0 && (msg[len-1] == '\n' || msg[len-1] == '\r')) len--;
                        if (tl + len + 2 >= (int)sizeof(tail)) {
                            int keep = (int)sizeof(tail) / 2;
                            memmove(tail, tail + tl - keep, (size_t)keep);
                            tl = keep;
                        }
                        if (tl > 0 && tl + 2 < (int)sizeof(tail)) tail[tl++] = ' ';
                        int copy = len;
                        if (tl + copy >= (int)sizeof(tail)) copy = (int)sizeof(tail) - tl - 1;
                        if (copy > 0) {
                            memcpy(tail + tl, msg, (size_t)copy);
                            tl += copy;
                            tail[tl] = '\0';
                        }
                    }
                    close(fd);
                    if (!found || tl == 0) {
                        cat(resp, cap, "wifi: dmesg: no wlan/WCN/wifi lines (driver absent?)\n");
                    } else {
                        cat(resp, cap, "wifi: dmesg (");
                        cat_u(resp, cap, (unsigned)found);
                        cat(resp, cap, " lines): ");
                        cat(resp, cap, tail);
                        cat(resp, cap, "\n");
                    }
                    note("[    -    ] N qeaf: wifi probe answered");
                }
            }
            {
                char rep[3072];
                vio_wifi_report(rep, (int)sizeof(rep));
                cat(resp, cap, rep);
            }
            } /* sub == 0 */
#else
            cat(resp, cap, "wifi: available on the device only\n");
#endif
        } else if (eq(line, "sync")) {
            usb_gadget_set_cable(usb_gadget_cable());   /* republishes DEVINFO */
            cat(resp, cap, "sync: DEVINFO.TXT republished\n");
        } else if (starts(line, "usb ")) {
            const char *m = line + 4;
            enum usb_mode mode;
            if (eq(m, "charge")) mode = USB_MODE_CHARGE;
            else if (eq(m, "mtp")) mode = USB_MODE_MTP;
            else if (eq(m, "ptp")) mode = USB_MODE_PTP;
            else { cat(resp, cap, "usb: unknown mode (charge|mtp|ptp)\n"); continue; }
            if (usb_gadget_set_mode(mode) == 0)
                cat(resp, cap, "usb: mode set\n");
            else
                cat(resp, cap, "usb: refused (gadget not started?)\n");
        } else if (eq(line, "ls")) {
            cat(resp, cap,
                "HELP.TXT SUMMARY.TXT RESPONSE.TXT QEAFBOOT.LOG DEVINFO.TXT "
                "QEAF.CMD BOOTIMG.BIN SCREEN.RAW HOMOLOG.TXT VQEAF UserData Install\n");
        } else if (eq(line, "df")) {
            cat(resp, cap, "rescue: free ");
            cat_u(resp, cap, fat16_free_bytes(vol));
            cat(resp, cap, " bytes (no per-file cluster cap)\n");
        } else if (starts(line, "del ") || starts(line, "rm ")) {
            const char *name = starts(line, "del ") ? line + 4 : line + 3;
            int freed = fat16_delete_file(vol, name);
            if (freed >= 0) {
                cat(resp, cap, "rescue: deleted ");
                cat(resp, cap, name);
                cat(resp, cap, " (");
                cat_u(resp, cap, (unsigned)freed);
                cat(resp, cap, " cluster(s) reclaimed)\n");
            } else {
                cat(resp, cap, "rescue: delete refused (file missing?)\n");
            }
        } else if (eq(line, "flash recovery BOOTIMG.BIN confirm")) {
            do_flash(vol, resp, cap);
        } else if (starts(line, "flash")) {
            cat(resp, cap,
                "flash: REFUSED - only 'flash recovery BOOTIMG.BIN confirm' "
                "is accepted\n");
            note("[    -    ] E qeaf: flash refused, unsafe verb");
        } else if (eq(line, "kmsg")) {
#ifdef __linux__
            int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                cat(resp, cap, "kmsg: cannot open /dev/kmsg\n");
            } else {
                char buf[4096];
                char tail[7168];
                int tl = 0;
                tail[0] = '\0';
                for (;;) {
                    ssize_t r = read(fd, buf, sizeof(buf) - 1);
                    char *msg, *h;
                    int len;
                    if (r <= 0) break;
                    buf[r] = '\0';
                    msg = strchr(buf, ';');
                    msg = msg ? msg + 1 : buf;
                    h = strstr(msg, "vqeaf:");
                    if (!h) continue;
                    len = slen(h);
                    while (len > 0 && (h[len - 1] == '\n' || h[len - 1] == '\r'))
                        len--;
                    if (tl + len + 2 >= (int)sizeof(tail)) {
                        int keep = (int)sizeof(tail) / 2;
                        memmove(tail, tail + tl - keep, (size_t)keep);
                        tl = keep;
                    }
                    memcpy(tail + tl, h, (size_t)len);
                    tl += len;
                    tail[tl++] = '\n';
                    tail[tl] = '\0';
                }
                close(fd);
                if (tl == 0) cat(resp, cap, "kmsg: no vqeaf: lines yet\n");
                else cat(resp, cap, tail);
            }
#else
            cat(resp, cap, "kmsg: available on the device only\n");
#endif
        } else if (eq(line, "pstore")) {
#ifdef __linux__
            dump_reset_evidence(vol, resp, cap);
#else
            cat(resp, cap, "pstore: available on the device only\n");
#endif
        } else if (eq(line, "snap")) {
            if (g_snapshot == NULL) {
                cat(resp, cap, "snap: unavailable (no scanout hook)\n");
            } else {
                int n = g_snapshot(vol);
                if (n > 0) {
                    cat(resp, cap, "snap: wrote SCREEN.RAW (");
                    cat_u(resp, cap, (unsigned)n);
                    cat(resp, cap, " bytes, RGB565 ");
                    cat_u(resp, cap, (unsigned)RESCUE_SCREEN_W);
                    cat(resp, cap, "x");
                    cat_u(resp, cap, (unsigned)RESCUE_SCREEN_H);
                    cat(resp, cap, ")\n");
                    note("[    -    ] N qeaf: snap wrote SCREEN.RAW");
                } else {
                    cat(resp, cap, "snap: failed (");
                    cat_u(resp, cap, (unsigned)(-n));
                    cat(resp, cap, ")\n");
                    note("[    -    ] E qeaf: snap failed");
                }
            }
        } else if (starts(line, "rec ")) {
            char sub[16];
            int fps = 8, secs = 10, rc;
            if(sscanf(line + 4, "%15s %d %d", sub, &fps, &secs) < 1) {
                cat(resp, cap, "rec: usage: rec start [fps] [secs] | rec stop | rec status\n");
            } else if(strcmp(sub, "start") == 0) {
                rc = vio_rec_start(fps, secs);
                cat(resp, cap, rc == 0 ? "rec: started (see rec status)\n" :
                                         "rec: start refused\n");
            } else if(strcmp(sub, "stop") == 0) {
                rc = vio_rec_stop();
                cat(resp, cap, rc == 0 ? "rec: stop requested\n" :
                                         "rec: not recording\n");
            } else {
                cat(resp, cap, "rec status: ");
                cat(resp, cap, vio_rec_status());
                cat(resp, cap, "\n");
            }
            g_result = 1;
        } else if (eq(line, "shot")) {
            if (g_screenshot == NULL) {
                cat(resp, cap, "shot: unavailable (no screenshot hook)\n");
            } else {
                char name[64] = "";
                int n = g_screenshot(vol, name, sizeof(name));
                if (n > 0) {
                    cat(resp, cap, "shot: wrote ");
                    cat(resp, cap, name);
                    cat(resp, cap, " (");
                    cat_u(resp, cap, (unsigned)n);
                    cat(resp, cap, " bytes PNG)\n");
                    note("[    -    ] N qeaf: screenshot written");
                } else {
                    cat(resp, cap, "shot: failed (");
                    cat_u(resp, cap, (unsigned)(-n));
                    cat(resp, cap, ")\n");
                }
            }
        } else if (starts(line, "term ")) {
            vqeaf_terminal_run(line + 5);
            cat(resp, cap, "term: executed against live framework state\n");
            g_result = 1;
        } else if (starts(line, "defaults ")) {
            int role = -1;
            if(sscanf(line + 9, "%d", &role) != 1 ||
               role < 0 || role >= VQEAF_DEFAULT_COUNT) {
                cat(resp, cap, "defaults: usage: defaults ROLE (0=browser 1=music 2=gallery 3=files 4=home)\n");
            } else {
                int rc = vqeaf_defaults_open(role);
                cat(resp, cap, "defaults: ");
                cat(resp, cap, vqeaf_default_role_name(role));
                cat(resp, cap, " -> ");
                cat(resp, cap, vqeaf_default_selector(role));
                cat(resp, cap, rc > 0 ? " (opened)\n" : " (no real handler)\n");
            }
            g_result = 1;
        } else if (starts(line, "tap ")) {
            int x = -1, y = -1;
            if (sscanf(line + 4, "%d %d", &x, &y) != 2 ||
                x < 0 || y < 0 || x >= 4096 || y >= 4096) {
                cat(resp, cap, "tap: usage tap <x> <y> (canvas pixels)\n");
            } else if (g_tap == NULL) {
                cat(resp, cap, "tap: unavailable (no touch hook)\n");
            } else {
                int rc = g_tap(x, y);
                if (rc == 1) {
                    cat(resp, cap, "tap: queued for the UI thread\n");
                    note("[    -    ] N qeaf: tap queued from QEAF.CMD");
                    g_result = 1;
                } else if (rc == 0) {
                    cat(resp, cap, "tap: applied\n");
                    note("[    -    ] N qeaf: tap applied from QEAF.CMD");
                    g_result = 1;
                } else {
                    cat(resp, cap, "tap: refused (outside the canvas or UI down)\n");
                    note("[    -    ] E qeaf: tap refused");
                    g_result = -6;
                }
            }
        } else if (starts(line, "hold ")) {
            int x = -1, y = -1, ms = 1200;
            int n = sscanf(line + 5, "%d %d %d", &x, &y, &ms);
            if ((n != 2 && n != 3) || x < 0 || y < 0 ||
                x >= 4096 || y >= 4096 ||
                ms < 100 || ms > 10000) {
                cat(resp, cap, "hold: usage hold <x> <y> [ms 100..10000]\n");
            } else if (g_hold == NULL) {
                cat(resp, cap, "hold: unavailable (no touch hook)\n");
            } else {
                int rc = g_hold(x, y, ms);
                if (rc == 1) {
                    cat(resp, cap, "hold: queued for the UI thread\n");
                    note("[    -    ] N qeaf: hold queued from QEAF.CMD");
                    g_result = 1;
                } else if (rc == 0) {
                    cat(resp, cap, "hold: applied\n");
                    note("[    -    ] N qeaf: hold applied from QEAF.CMD");
                    g_result = 1;
                } else {
                    cat(resp, cap, "hold: refused (outside the canvas, UI down, or a contact is pending)\n");
                    note("[    -    ] E qeaf: hold refused");
                    g_result = -8;
                }
            }
        } else if (eq(line, "power")) {
            if (g_power == NULL) {
                cat(resp, cap, "power: unavailable (no key hook)\n");
            } else {
                int rc = g_power();
                if (rc == 0) {
                    cat(resp, cap, "power: applied\n");
                    note("[    -    ] N qeaf: power press applied from QEAF.CMD");
                    g_result = 1;
                } else if (rc == 1) {
                    cat(resp, cap, "power: queued for the UI thread\n");
                    note("[    -    ] N qeaf: power press queued from QEAF.CMD");
                    g_result = 1;
                } else {
                    cat(resp, cap, "power: refused (UI down)\n");
                    note("[    -    ] E qeaf: power press refused");
                    g_result = -8;
                }
            }
        } else if (starts(line, "page ")) {
            char name[32];
            int n = 0;
            const char *p = line + 5;
            while (*p == ' ') p++;
            while (*p && *p != '\r' && *p != '\n' &&
                   n + 1 < (int)sizeof(name)) {
                name[n++] = *p++;
            }
            name[n] = '\0';
            if (g_page == NULL) {
                cat(resp, cap, "page: unavailable (no UI hook)\n");
            } else if (name[0] == '\0') {
                cat(resp, cap, "page: usage page files|apps|home|settings\n");
            } else {
                int rc = g_page(name);
                if (rc == 0) cat(resp, cap, "page: applied\n");
                else if (rc == 1) cat(resp, cap, "page: queued for the UI thread\n");
                else cat(resp, cap, "page: unknown page or UI down\n");
            }
        } else if (starts(line, "swipe ")) {
            int x0 = -1, y0 = -1, x1 = -1, y1 = -1, steps = 8;
            int n = sscanf(line + 6, "%d %d %d %d %d", &x0, &y0, &x1, &y1, &steps);
            if ((n != 4 && n != 5) || x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 ||
                x0 >= 4096 || y0 >= 4096 || x1 >= 4096 || y1 >= 4096 ||
                steps < 2 || steps > 64) {
                cat(resp, cap, "swipe: usage swipe <x0> <y0> <x1> <y1> [steps 2..64]\n");
            } else if (g_swipe == NULL) {
                cat(resp, cap, "swipe: unavailable (no touch hook)\n");
            } else {
                int rc = g_swipe(x0, y0, x1, y1, steps);
                if (rc == 1) {
                    cat(resp, cap, "swipe: queued for the UI thread\n");
                    note("[    -    ] N qeaf: swipe queued from QEAF.CMD");
                    g_result = 1;
                } else if (rc == 0) {
                    cat(resp, cap, "swipe: applied\n");
                    note("[    -    ] N qeaf: swipe applied from QEAF.CMD");
                    g_result = 1;
                } else {
                    cat(resp, cap, "swipe: refused (outside the canvas, UI down, or a drag is pending)\n");
                    note("[    -    ] E qeaf: swipe refused");
                    g_result = -7;
                }
            }
        } else if (eq(line, "apps")) {
            int count = vqeaf_qeapp_count();
            cat(resp, cap, "apps: ");
            cat_u(resp, cap, (unsigned)count);
            cat(resp, cap, " installed\n");
            for (int i = 0; i < count; i++) {
                const vqeaf_qeapp_t *app = vqeaf_qeapp_at(i);
                if (!app) continue;
                cat(resp, cap, " - ");
                cat(resp, cap, app->name);
                cat(resp, cap, app->native ? " (native) " : " ");
                cat(resp, cap, app->package_file);
                if (app->data_size > 0) {
                    cat(resp, cap, " data ");
                    cat_u(resp, cap, (unsigned)app->data_size);
                    cat(resp, cap, "B");
                }
                cat(resp, cap, "\n");
            }
        } else if (starts(line, "appdata")) {
            int index = 0;
            int used = 7;
            int digits = 0;
            while (line[used] == ' ') used++;
            while (line[used] >= '0' && line[used] <= '9' && digits < 4) {
                index = index * 10 + (line[used] - '0');
                used++;
                digits++;
            }
            if (digits == 0) {
                cat(resp, cap, "appdata: usage appdata <slot> [text]\n");
            } else if (line[used] == '\0' || line[used] == '\r' ||
                       line[used] == '\n') {
                const uint8_t *data = NULL;
                size_t size = 0;
                if (vqeaf_qeapp_data_read(index, &data, &size) == 0) {
                    size_t copy = size < 255 ? size : 255;
                    char text[256];
                    memcpy(text, data, copy);
                    text[copy] = '\0';
                    cat(resp, cap, "appdata: slot ");
                    cat_u(resp, cap, (unsigned)index);
                    cat(resp, cap, " ");
                    cat_u(resp, cap, (unsigned)size);
                    cat(resp, cap, " bytes: ");
                    cat(resp, cap, text);
                    cat(resp, cap, "\n");
                } else {
                    cat(resp, cap, "appdata: slot ");
                    cat_u(resp, cap, (unsigned)index);
                    cat(resp, cap, " has no data\n");
                }
            } else {
                char text[512];
                int len = 0;
                while (line[used] == ' ') used++;
                while (line[used] && line[used] != '\r' && line[used] != '\n' &&
                       len + 1 < (int)sizeof(text)) {
                    text[len] = line[used];
                    len++;
                    used++;
                }
                text[len] = '\0';
                vqeaf_qeapp_request_data(index, text);
                cat(resp, cap, "appdata: queued for slot ");
                cat_u(resp, cap, (unsigned)index);
                cat(resp, cap, "\n");
            }
        } else if (starts(line, "uninstall")) {
            int index = 0;
            int used = 9;
            int digits = 0;
            while (line[used] == ' ') used++;
            while (line[used] >= '0' && line[used] <= '9' && digits < 4) {
                index = index * 10 + (line[used] - '0');
                used++;
                digits++;
            }
            if (digits == 0) {
                cat(resp, cap, "uninstall: usage uninstall <slot>\n");
            } else {
                vqeaf_qeapp_request_uninstall(index);
                cat(resp, cap, "uninstall: queued for slot ");
                cat_u(resp, cap, (unsigned)index);
                cat(resp, cap, "\n");
            }
        } else if (starts(line, "store")) {
            uint32_t seq = 0, count = 0, bytes = 0;
            int slot = 0;
            if (appstore_info(&seq, &count, &bytes, &slot) != 0) {
                cat(resp, cap, "store: no valid slot\n");
            } else {
                cat(resp, cap, "store: slot ");
                cat(resp, cap, slot == 0 ? "A" : "B");
                cat(resp, cap, " seq ");
                cat_u(resp, cap, seq);
                cat(resp, cap, " apps ");
                cat_u(resp, cap, count);
                cat(resp, cap, " bytes ");
                cat_u(resp, cap, bytes);
                cat(resp, cap, "\n");
            }
        } else if (starts(line, "run ")) {
            char name[64];
            int len = 0;
            while (line[4 + len] && line[4 + len] != '\r' &&
                   line[4 + len] != '\n' && len + 1 < (int)sizeof(name)) {
                name[len] = line[4 + len];
                len++;
            }
            name[len] = '\0';
            if (name[0] == '\0') {
                cat(resp, cap, "run: usage run <name|id>\n");
            } else {
                vqeaf_qeapp_request_launch(name);
                cat(resp, cap, "run: queued ");
                cat(resp, cap, name);
                cat(resp, cap, "\n");
            }
        } else if (starts(line, "install ")) {
            char name[96];
            char path[100];
            int len = 0;
            while (line[8 + len] && line[8 + len] != '\r' &&
                   line[8 + len] != '\n' && len + 1 < (int)sizeof(name)) {
                name[len] = line[8 + len];
                len++;
            }
            name[len] = '\0';
            if (name[0] == '\0') {
                cat(resp, cap, "install: usage install <file>\n");
            } else {
                snprintf(path, sizeof(path), "/%s", name);
                vqeaf_qeapp_request_install(path);
                cat(resp, cap, "install: queued ");
                cat(resp, cap, path);
                cat(resp, cap, " for the UI thread\n");
            }
        } else if (starts(line, "qeapp ")) {
            static uint8_t package[512u * 1024u];
            char name[96];
            char err[256];
            char rep[2048];
            int n, len = 0;
            while (line[6 + len] && line[6 + len] != '\r' &&
                   line[6 + len] != '\n' && len + 1 < (int)sizeof(name)) {
                name[len] = line[6 + len];
                len++;
            }
            name[len] = '\0';
            n = name[0] ? fat16_read_file(vol, name, package, sizeof(package)) : -1;
            if (n <= 0) {
                cat(resp, cap, "qeapp: cannot read ");
                cat(resp, cap, name[0] ? name : "<name>");
                cat(resp, cap, " from QEAFRESCUE\n");
            } else {
                int rc = vqeaf_qeapp_device_launch(package, (size_t)n, 0u, rep,
                                                   sizeof(rep), err, sizeof(err));
                if (rc == 0) {
                    cat(resp, cap, "qeapp: ran ");
                    cat(resp, cap, name);
                    cat(resp, cap, " on violet (arm64) in a guarded child\n");
                    cat(resp, cap, rep);
                    note("[    -    ] N qeaf: qeapp executed on device");
                    g_result = 1;
                } else {
                    cat(resp, cap, "qeapp: failed: ");
                    cat(resp, cap, err);
                    cat(resp, cap, "\n");
                    note("[    -    ] E qeaf: qeapp device run failed");
                    g_result = -8;
                }
            }
        } else if (eq(line, "unlock")) {
            if (g_unlock == NULL) {
                cat(resp, cap, "unlock: unavailable (no UI hook)\n");
            } else {
                int rc = g_unlock();
                if (rc == 0) cat(resp, cap, "unlock: Home\n");
                else if (rc == 1)
                    cat(resp, cap, "unlock: queued, Home next frame\n");
                else cat(resp, cap, "unlock: UI is down\n");
            }
        } else if (eq(line, "fault ui")) {
            if (g_fault == NULL) {
                cat(resp, cap, "fault: unavailable (no UI hook)\n");
            } else {
                int rc = g_fault();
                if (rc == 0) {
                    cat(resp, cap, "fault: UI faulted\n");
                } else if (rc == 1) {
                    cat(resp, cap, "fault: queued; the record lands in"
                                   " CRASH.TXT\n");
                } else {
                    cat(resp, cap, "fault: UI is down\n");
                }
            }
        } else if (eq(line, "reboot") || starts(line, "reboot ")) {
#ifdef __linux__
            const char *arg = eq(line, "reboot") ? "system" : line + 7;
            if (eq(arg, "system")) {
                cat(resp, cap, "reboot: restarting to system\n");
                note("[    -    ] N qeaf: reboot -> system");
                put(vol, "RESPONSE.TXT", resp);
                put(vol, "QEAFBOOT.LOG", g_log);
                reboot(RB_AUTOBOOT);
            } else if (eq(arg, "recovery") || eq(arg, "bootloader")) {
                cat(resp, cap, "reboot: restarting to ");
                cat(resp, cap, arg);
                cat(resp, cap, "\n");
                note("[    -    ] N qeaf: reboot -> restart2");
                put(vol, "RESPONSE.TXT", resp);
                put(vol, "QEAFBOOT.LOG", g_log);
                syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                        LINUX_REBOOT_CMD_RESTART2, (void *)arg);
            } else {
                cat(resp, cap,
                    "reboot: unknown target (system|recovery|bootloader)\n");
                continue;
            }
            cat(resp, cap, "reboot: syscall returned (restart refused)\n");
#else
            cat(resp, cap, "reboot: available on the device only\n");
#endif
        } else {
            cat(resp, cap, "unknown verb\n");
        }
    }
}

void rescue_poll(struct fat16_volume *vol) {
    uint8_t cmd[CMD_CAP];
    char resp[RESP_CAP];
    int n;

    if (!vol) return;
    n = fat16_read_file(vol, "QEAF.CMD", cmd, sizeof(cmd) - 1);
    if (n <= 0) return;
    cmd[n] = '\0';
    if (n == g_last_len && memcmp(cmd, g_last, (size_t)n) == 0) return;
    if (n >= CMD_CAP) return;
    memcpy(g_last, cmd, (size_t)n);
    g_last_len = n;

    resp[0] = '\0';
    g_seq++;
    cat(resp, sizeof(resp), "seq=");
    cat_u(resp, sizeof(resp), (unsigned)g_seq);
    cat(resp, sizeof(resp), "\n");
    run_verbs(vol, (const char *)cmd, n, resp, sizeof(resp));
    put(vol, "RESPONSE.TXT", resp);
    put(vol, "QEAFBOOT.LOG", g_log);
    umos_logf("rescue: answered %d bytes of QEAF.CMD\n", n);
}
