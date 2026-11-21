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

#ifdef __cplusplus
}
#endif

#endif /* VIO_BOOT_JOURNAL_H */
