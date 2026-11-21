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

static int journal_open(int writable)
{
    int fd;

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
                    "%s" "boots=%u\nstage=%s\nuptime_ms=%u\n",
                    journal_magic, journal_boots, journal_stage,
                    (unsigned)vio_monotonic_ms());
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
