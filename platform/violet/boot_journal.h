/* SPDX-License-Identifier: MIT */
/* platform/violet/boot_journal.h - the boot/crash record that survives a reset.
 *
 * The rescue volume is RAM: when PID 1 faults, the kernel panics ("Attempted
 * to kill init!") and every byte of CRASH.TXT disappears before a host can read
 * it. That is why a phone that dies after a while leaves nothing to diagnose.
 * p77 does survive, so the record is written into the unused tail of that
 * partition and published again as LASTBOOT.TXT at the next boot.
 */
#ifndef VIO_BOOT_JOURNAL_H
#define VIO_BOOT_JOURNAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* p77 is 64 MiB and the boot image is about 20 MiB, so the last 16 KiB never
 * overlaps it: `flash recovery` writes image bytes only, from offset 0. */
#define VIO_JOURNAL_BYTES  (16u * 1024u)
#define VIO_JOURNAL_HEADER 512u
#define VIO_JOURNAL_TEXT   (VIO_JOURNAL_BYTES - VIO_JOURNAL_HEADER)
#define VIO_JOURNAL_OFFSET (64u * 1024u * 1024u - VIO_JOURNAL_BYTES)
#define VIO_JOURNAL_DEVICE  "/dev/mmcblk0p77"
#define VIO_JOURNAL_DEVICE2 "/dev/block/mmcblk0p77"

/* Reads the slot once, counts the boot and rewrites the header. Read-only
 * before the first stage marker; safe to call before anything else. */
void vio_journal_init(void);

/* How many times this partition has seen a boot start. */
unsigned vio_journal_boots(void);

/* The record the previous boot left behind, or "" when there was none. */
const char *vio_journal_previous(void);

/* Stage marker. Writes only when the stage really changes. */
void vio_journal_stage(const char *stage);

/* Signal-path record: header, then the record text. open/lseek/write/fsync
 * only, no formatting, so a fault handler can call it. */
void vio_journal_crash(const char *text);

/* ---- live trace ----------------------------------------------------------
 * A SoC reset (watchdog, PMIC, modem-triggered SSR) kills PID 1 outright: no
 * signal runs, CRASH.TXT dies with RAM, and the next boot can only report
 * "ended without a caught signal". While armed, the newest bytes of the OS log
 * are rewritten into the same p77 slot once a second, so the next boot's
 * LASTBOOT.TXT ends at the last heartbeat before the reset instead of saying
 * nothing. Armed only around `wifi up` (modem bring-up), where the reset is
 * reproducible, so an idle phone does not write flash.
 */
void vio_journal_trace_arm(int on);
void vio_journal_trace_raw(const char *line);
void vio_journal_trace(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/* One line recorded and flushed immediately, armed or not. */
void vio_journal_note(const char *text);

/* The bootloader/kernel's own account of why the previous boot ended:
 * `androidboot.bootreason` from /proc/cmdline, `chosen/pureason`, and the raw
 * IMEM restart_reason word. Writes one line; values are never invented, an
 * unreadable source is reported as absent. */
void vio_reset_reason(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* VIO_BOOT_JOURNAL_H */
