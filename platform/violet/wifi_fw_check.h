/* SPDX-License-Identifier: MIT */
/* platform/violet/wifi_fw_check.h - validate the WCN3990 firmware files the
 * driver is about to hand to icnss, from the real initramfs paths.
 *
 * The rescue verb `wifi fw` prints this report next to the packaging report,
 * so the phone answers with what it actually carries, not what was built.
 */
#ifndef VIO_WIFI_FW_CHECK_H
#define VIO_WIFI_FW_CHECK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Appends multi-line `fw check:`/`fw set:` lines to a NUL-terminated buffer.
 * Returns 0 only when all three blobs are present and structurally valid. */
int vio_wifi_fw_report(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* VIO_WIFI_FW_CHECK_H */