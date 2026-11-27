/* SPDX-License-Identifier: MIT */
/* platform/violet/boot_journal.c - boot/crash record in the tail of p77.
 *
 * Layout of the 16 KiB slot:
 *   0      .. 511    header, ASCII, padded with spaces:
 *                    "QEAFJRNL1\nboots=N\nstage=NAME\nuptime_ms=N\n"
 *   512    .. 16383  the record text, padded with NULs
 *
 * The header is rewritten on every stage change (one 512-byte flash write) and
 * the text only when a fault is recorded, so normal boots cost almost nothing.
 * Everything here must stay usable from a signal handler: no stdio, no malloc,
 * no locks - only open/lseek/write/fsync on a static buffer.
 */
#define _GNU_SOURCE

#include "boot_journal.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "violet.h"

static const char journal_magic[] = "QEAFJRNL1\n";

static char journal_header[VIO_JOURNAL_HEADER];
static char journal_text[VIO_JOURNAL_TEXT];
static char journal_stage[32] = "start";
static unsigned journal_boots;
static int journal_read_done;
static int journal_disabled;

/* Live trace state. The ring holds the same bytes the crash path would keep,
 * but it is rebuilt from the OS log every second, so a reset that skips the
 * fault handler still leaves the last second of evidence in p77. */
static char trace_text[VIO_JOURNAL_TEXT];
static size_t trace_len;
static pthread_mutex_t trace_mutex = PTHREAD_MUTEX_INITIALIZER;
/* One flush at a time: the flusher thread, the arming path and note() can all
 * reach it, and the snapshot buffer is static. */
static pthread_mutex_t flush_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t trace_once = PTHREAD_ONCE_INIT;
static volatile sig_atomic_t trace_armed;
static volatile sig_atomic_t trace_dirty;
static volatile sig_atomic_t trace_crashed;
static int journal_live;   /* header field: this record is a live trace */

static int journal_open(int writable)
{
    int fd;

#ifdef VQEAF_RAM_ONLY_DIAGNOSTICS
    /* Diagnostic RAM boots must never write the p77 boot journal. */
    if(writable) {
        errno = EROFS;
        return -1;
    }
#endif
    if(journal_disabled) return -1;
    fd = open(VIO_JOURNAL_DEVICE, writable ? (O_RDWR | O_CLOEXEC)
                                           : (O_RDONLY | O_CLOEXEC));
    if(fd < 0)
        fd = open(VIO_JOURNAL_DEVICE2, writable ? (O_RDWR | O_CLOEXEC)
                                                : (O_RDONLY | O_CLOEXEC));
    if(fd < 0 && writable) {
        journal_disabled = 1;
        vio_log("journal: p77 is not writable (%s); the record stays in RAM",
                strerror(errno));
    }
    return fd;
}

/* A block device can return short writes and EINTR; the whole span is written
 * before fsync so a reader never sees a half header. */
static int journal_write(int fd, unsigned long offset, const char *data,
                         size_t len)
{
    size_t done = 0;

    if(lseek(fd, (off_t)offset, SEEK_SET) < 0) return -1;
    while(done < len) {
        ssize_t n = write(fd, data + done, len - done);
        if(n < 0) {
            if(errno == EINTR) continue;
            return -1;
        }
        done += (size_t)n;
    }
    return fsync(fd) == 0 ? 0 : -1;
}

/* Always written at full width: a shorter header must not leave the tail of
 * the previous one readable. */
static void journal_header_build(void)
{
    char text[256];
    int used;

    memset(journal_header, ' ', sizeof(journal_header));
    used = snprintf(text, sizeof(text),
                    "%s" "boots=%u\nstage=%s\nuptime_ms=%u\nlive=%d\n",
                    journal_magic, journal_boots, journal_stage,
                    (unsigned)vio_monotonic_ms(), journal_live ? 1 : 0);
    if(used > 0) {
        if(used > (int)sizeof(journal_header)) used = (int)sizeof(journal_header);
        memcpy(journal_header, text, (size_t)used);
    }
}

static void journal_commit(void)
{
    int fd;

    journal_header_build();
    fd = journal_open(1);
    if(fd < 0) return;
    (void)journal_write(fd, VIO_JOURNAL_OFFSET, journal_header,
                        sizeof(journal_header));
    close(fd);
}

void vio_journal_init(void)
{
    char buffer[VIO_JOURNAL_BYTES];
    char head[VIO_JOURNAL_HEADER + 1];
    size_t used;
    int fd;

    if(journal_read_done) return;
    journal_read_done = 1;
    journal_text[0] = '\0';

    fd = journal_open(0);
    if(fd >= 0) {
        ssize_t n = pread(fd, buffer, sizeof(buffer), (off_t)VIO_JOURNAL_OFFSET);
        close(fd);
        if(n == (ssize_t)sizeof(buffer) &&
           memcmp(buffer, journal_magic, sizeof(journal_magic) - 1) == 0) {
            const char *field;

            memcpy(head, buffer, VIO_JOURNAL_HEADER);
            head[VIO_JOURNAL_HEADER] = '\0';
            field = strstr(head, "boots=");
            if(field != NULL) journal_boots = (unsigned)strtoul(field + 6, NULL, 10);
            field = strstr(head, "stage=");
            if(field != NULL) {
                size_t i = 0;
                field += 6;
                while(field[i] != '\0' && field[i] != '\n' &&
                      i + 1 < sizeof(journal_stage)) {
                    journal_stage[i] = field[i];
                    i++;
                }
                journal_stage[i] = '\0';
            }
            used = VIO_JOURNAL_TEXT;
            while(used > 0 && (buffer[VIO_JOURNAL_HEADER + used - 1] == '\0' ||
                               buffer[VIO_JOURNAL_HEADER + used - 1] == ' ' ||
                               buffer[VIO_JOURNAL_HEADER + used - 1] == '\n'))
                used--;
            if(used >= sizeof(journal_text)) used = sizeof(journal_text) - 1;
            memcpy(journal_text, buffer + VIO_JOURNAL_HEADER, used);
            journal_text[used] = '\0';
        }
    }

    journal_boots++;
    /* Re-arm with the count of this boot; a stage change later overwrites it. */
    journal_stage[0] = '\0';
    vio_journal_stage("start");
    if(journal_text[0] != '\0')
        vio_log("journal: previous boot left a record (%u bytes, boots=%u)",
                (unsigned)strlen(journal_text), journal_boots);
    else
        vio_log("journal: slot re-armed, boots=%u", journal_boots);
}

unsigned vio_journal_boots(void) { return journal_boots; }

const char *vio_journal_previous(void) { return journal_text; }

void vio_journal_stage(const char *stage)
{
    if(stage == NULL) return;
    if(strncmp(journal_stage, stage, sizeof(journal_stage) - 1) == 0) return;
    snprintf(journal_stage, sizeof(journal_stage), "%s", stage);
    journal_commit();
}

/* Called from the fault handler. The header is the one already in RAM (built
 * by the last stage change), so nothing is formatted here; the record itself
 * carries sig/pc/lr/stage from crash_build(). */
void vio_journal_crash(const char *text)
{
    static char scratch[VIO_JOURNAL_TEXT];
    size_t len;
    int fd;

    if(text == NULL) return;
    /* Claim the slot before opening it: the trace flusher must never write a
     * live snapshot over the record this handler is about to store. */
    trace_crashed = 1;
    journal_live = 0;
    fd = journal_open(1);
    if(fd < 0) return;

    if(journal_header[0] == '\0') journal_header_build();
    (void)journal_write(fd, VIO_JOURNAL_OFFSET, journal_header,
                        sizeof(journal_header));

    memset(scratch, 0, sizeof(scratch));
    len = strlen(text);
    if(len > sizeof(scratch) - 1) len = sizeof(scratch) - 1;
    memcpy(scratch, text, len);
    (void)journal_write(fd, VIO_JOURNAL_OFFSET + VIO_JOURNAL_HEADER, scratch,
                        sizeof(scratch));
    close(fd);
}

/* ---- live trace ----------------------------------------------------------
 * Measured behaviour this exists for: with the modem ONLINE the SoC resets
 * 14-16 s later, no signal handler runs, the kernel keeps no pstore and the
 * rescue volume is RAM, so every earlier attempt produced "no fault record".
 * p77 survives the reset, so the last second of the OS log is written there
 * before it happens. The header carries live=1 and the uptime of the last
 * heartbeat, which is what proves the record was cut off by a reset rather
 * than written at a clean shutdown.
 */
#define TRACE_FLUSH_MS 1000u
/* The vendor kernel's own ring holds the one thing userspace cannot report:
 * `subsys-restart` / `subsys-pil-tz` lines naming the protection domain that
 * went down. It is RAM-only and dies with the reset, so its tail is appended
 * to every heartbeat. 4 KiB is the newest few hundred kernel lines. */
#define TRACE_KLOG_BYTES 4096u

static void trace_append_locked(const char *line, size_t len)
{
    size_t room = sizeof(trace_text) - 1u - trace_len;

    if(len > sizeof(trace_text) - 1u) {          /* keep the newest half */
        line += len - (sizeof(trace_text) - 1u);
        len = sizeof(trace_text) - 1u;
        room = 0;
    }
    if(len > room) {
        size_t keep = (sizeof(trace_text) - 1u) / 2u;
        if(trace_len > keep)
            memmove(trace_text, trace_text + trace_len - keep, keep);
        else
            keep = trace_len;
        trace_len = keep;
    }
    memcpy(trace_text + trace_len, line, len);
    trace_len += len;
    trace_text[trace_len] = '\0';
}

/* Writes the current ring into the record slot: header first, then the full
 * text area, so a shorter snapshot cannot leave older bytes behind. */
static void trace_flush(void)
{
    static char snapshot[VIO_JOURNAL_TEXT];
    char klog[TRACE_KLOG_BYTES + 1u];
    size_t len, limit, used;
    int fd;

    if(trace_crashed) return;
    pthread_mutex_lock(&flush_mutex);
    pthread_mutex_lock(&trace_mutex);
    len = trace_len;
    limit = sizeof(snapshot) - TRACE_KLOG_BYTES - 64u;
    if(len > limit) {                       /* keep the newest trace bytes */
        memmove(snapshot, trace_text + (len - limit), limit);
        len = limit;
    } else if(len > 0) {
        memcpy(snapshot, trace_text, len);
    }
    pthread_mutex_unlock(&trace_mutex);

    used = len;
    {
        int n = vio_kernel_tail(klog, sizeof(klog));
        if(n > 0) {
            char head[96];
            int h = snprintf(head, sizeof(head),
                             "\n### kernel kmsg tail (%d bytes)\n", n);
            if(h > 0 && used + (size_t)h + (size_t)n < sizeof(snapshot)) {
                memcpy(snapshot + used, head, (size_t)h);
                used += (size_t)h;
                memcpy(snapshot + used, klog, (size_t)n);
                used += (size_t)n;
            }
        }
    }
    memset(snapshot + used, 0, sizeof(snapshot) - used);
    journal_live = 1;
    journal_header_build();
    /*
     * WLAN live trace is RAM-only. /tmp is part of QEAF's RAM rootfs;
     * never route this diagnostic heartbeat through the p77 journal.
     * The UART/kmsg path remains available through vio_log/vio_kernel_tail.
     */
    fd = open("/tmp/qeaf-wifi-trace.log",
              O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if(fd >= 0) {
        (void)journal_write(fd, 0, journal_header, sizeof(journal_header));
        (void)journal_write(fd, sizeof(journal_header), snapshot,
                            sizeof(snapshot));
        close(fd);
        trace_dirty = 0;
    } else {
        vio_log("journal: cannot write RAM Wi-Fi trace: %s", strerror(errno));
    }
    pthread_mutex_unlock(&flush_mutex);
}

static void *trace_flusher(void *arg)
{
    (void)arg;
    for(;;) {
        vio_sleep_ms(TRACE_FLUSH_MS);
        if(trace_crashed) return NULL;
        if(trace_armed && trace_dirty) trace_flush();
    }
}

static void trace_start_thread(void)
{
    pthread_t thread;

    if(pthread_create(&thread, NULL, trace_flusher, NULL) == 0)
        pthread_detach(thread);
    else
        vio_log("journal: no trace thread: %s", strerror(errno));
}

void vio_journal_trace_arm(int on)
{
    if(on) {
        pthread_once(&trace_once, trace_start_thread);
        if(!trace_armed) {
            trace_armed = 1;
            journal_live = 1;
            vio_journal_trace("journal: RAM live trace armed; UART/kmsg "
                              "remains reset evidence");
            trace_dirty = 1;
            trace_flush();
        }
        return;
    }
    if(trace_armed) {
        trace_armed = 0;
        vio_journal_trace("journal: live trace disarmed (modem released)");
        trace_flush();
        /* A clean end is recorded in RAM only; do not commit to p77. */
        journal_live = 0;
    }
}

void vio_journal_trace_raw(const char *line)
{
    char stamped[600];
    int n;

    if(line == NULL) return;
    if(!trace_armed) return;
    n = snprintf(stamped, sizeof(stamped), "t=%ums %s\n",
                 (unsigned)vio_monotonic_ms(), line);
    if(n <= 0) return;
    if((size_t)n >= sizeof(stamped)) n = (int)sizeof(stamped) - 1;
    pthread_mutex_lock(&trace_mutex);
    trace_append_locked(stamped, (size_t)n);
    pthread_mutex_unlock(&trace_mutex);
    trace_dirty = 1;
}

void vio_journal_trace(const char *fmt, ...)
{
    char line[512];
    va_list args;
    int n;

    if(fmt == NULL) return;
    va_start(args, fmt);
    n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if(n <= 0) return;
    if((size_t)n >= sizeof(line)) n = (int)sizeof(line) - 1;
    line[n] = '\0';
    vio_journal_trace_raw(line);
}

void vio_journal_note(const char *text)
{
    char line[512];
    int n;

    if(text == NULL) return;
    n = snprintf(line, sizeof(line), "note %s", text);
    if(n <= 0) return;
    if((size_t)n >= sizeof(line)) n = (int)sizeof(line) - 1;
    pthread_mutex_lock(&trace_mutex);
    trace_append_locked(line, (size_t)n);
    trace_append_locked("\n", 1u);
    pthread_mutex_unlock(&trace_mutex);
    trace_dirty = 1;
    trace_flush();
}

/* ---- why did the previous boot end? --------------------------------------
 * Three real sources, none guessed:
 *   /proc/cmdline androidboot.bootreason - written by the bootloader
 *   chosen/pureason                      - PON reason the bootloader recorded
 *   IMEM restart_reason word             - the value the reset path leaves
 * The IMEM word is printed raw. Its numeric meanings differ between ABL
 * versions, so it is annotated only where the layout is unambiguous.
 */
#define IMEM_RESTART_REASON_PA 0x146aa65cull

static int read_first_line(const char *path, char *out, size_t cap)
{
    int fd, i = 0;
    ssize_t n;
    char buf[192];

    out[0] = '\0';
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return -1;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if(n <= 0) return -1;
    while(i < (int)n && buf[i] != '\n' && buf[i] != '\r' &&
          (size_t)i + 1 < cap) {
        out[i] = buf[i];
        i++;
    }
    out[i] = '\0';
    return i > 0 ? 0 : -1;
}

static int cmdline_value(const char *key, char *out, size_t cap)
{
    char buf[2048];
    size_t klen = strlen(key);
    int fd;
    ssize_t n;
    size_t i;

    out[0] = '\0';
    fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if(fd < 0) return -1;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if(n <= 0) return -1;
    buf[n] = '\0';
    for(i = 0; i + klen < (size_t)n; i++) {
        size_t j = 0;
        if((i > 0) && buf[i - 1] != ' ') continue;
        if(memcmp(buf + i, key, klen) != 0) continue;
        i += klen;
        if(buf[i] == '=') i++;
        while(i < (size_t)n && buf[i] != ' ' && buf[i] != '\n' &&
              buf[i] != '\r' && j + 1 < cap)
            out[j++] = buf[i++];
        out[j] = '\0';
        return j > 0 ? 0 : -1;
    }
    return -1;
}

void vio_reset_reason(char *out, size_t cap)
{
    char value[192];
    uint32_t word = 0;
    int have_word = 0;
    int used;

    if(out == NULL || cap == 0) return;
    out[0] = '\0';
    used = snprintf(out, cap, "reset-reason:");
    if(used < 0 || (size_t)used >= cap) return;

    if(cmdline_value("androidboot.bootreason", value, sizeof(value)) == 0)
        used += snprintf(out + used, cap - (size_t)used,
                         " bootreason=%s", value);
    else
        used += snprintf(out + used, cap - (size_t)used,
                         " bootreason=absent");
    if(used < 0 || (size_t)used >= cap) return;

    if(read_first_line("/proc/device-tree/chosen/pureason", value,
                       sizeof(value)) == 0)
        used += snprintf(out + used, cap - (size_t)used,
                         " pureason=%s", value);
    else
        used += snprintf(out + used, cap - (size_t)used,
                         " pureason=absent");
    if(used < 0 || (size_t)used >= cap) return;

    /* IMEM is not in the kernel's RAM map; reading it needs /dev/mem, which a
     * hardened kernel may refuse. Refused is reported, never filled in. */
    {
        int fd = open("/dev/mem", O_RDONLY | O_CLOEXEC);
        if(fd >= 0) {
            ssize_t n = pread(fd, &word, sizeof(word),
                              (off_t)IMEM_RESTART_REASON_PA);
            close(fd);
            have_word = n == (ssize_t)sizeof(word);
        }
    }
    if(have_word) {
        used += snprintf(out + used, cap - (size_t)used,
                         " imem_restart_reason=0x%08x", (unsigned)word);
        if(word == 0x77665501u)
            used += snprintf(out + used, cap - (size_t)used,
                             " (bootloader: normal boot request)");
        else if(word == 0x77665502u)
            used += snprintf(out + used, cap - (size_t)used,
                             " (bootloader: recovery boot request)");
    } else {
        used += snprintf(out + used, cap - (size_t)used,
                         " imem_restart_reason=unreadable (/dev/mem absent or "
                         "restricted)");
    }
    (void)used;
}
