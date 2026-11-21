/* SPDX-License-Identifier: MIT */
/* platform/violet/wifi_bringup.h - mount the stock radio partitions and let
 * the kernel's own WCN3990 stack come up.
 *
 * The violet kernel already carries qcacld-3.0 (hdd/cfg80211), icnss and the
 * PIL/remoteproc drivers. What it does not have is a filesystem: this OS is
 * PID 1 in an initramfs, so unless /vendor, the modem (firmware_mnt) and
 * persist partitions are mounted, request_firmware() has nothing to read and
 * no wlan netdev is ever created. This module does exactly that and nothing
 * else - it never pretends a network is up.
 */
#ifndef VIO_WIFI_BRINGUP_H
#define VIO_WIFI_BRINGUP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Idempotent: mounts /vendor (p86, ext4), /vendor/firmware_mnt (p82, vfat)
 * and /mnt/vendor/persist (p74, ext4) read-only, links the modem image into
 * the kernel firmware search path, and publishes the real result. */
int vio_wifi_bringup(void);

/* Boot-time state machine: starts the modem remoteproc and rebinds icnss
 * once, then gives up honestly. Safe to call from the main loop. */
void vio_wifi_poll(void);

/* Start the modem PIL on purpose (rescue `wifi up`). Boot never calls this:
 * a modem without a vendor service layer on top of it resets the SoC about
 * 40 s after it comes online, which is why the default boot leaves it off. */
void vio_wifi_modem_start(void);

/* Release the modem PIL and stop every retry. Used by the rescue verb
 * `wifi off` and by the boot path when the modem must not be started: a
 * modem with no service layer on top of it resets the whole SoC, which is
 * why a boot that started it did not survive past ~40 s. */
void vio_wifi_modem_stop(void);

/* 1 only when a real wlan* netdev exists. */
int vio_wifi_netdev_present(void);

/* Multi-line, human-readable truth for the QEAFRESCUE `wifi` verb. */
void vio_wifi_report(char *out, int cap);

/* Dump the subsystem/modem sysfs the bring-up depends on. Read-only. */
void vio_wifi_sysdump(char *out, int cap);

/* Short one-line state for the status bar / settings page. */
const char *vio_wifi_state_text(void);

#ifdef __cplusplus
}
#endif

#endif /* VIO_WIFI_BRINGUP_H */
