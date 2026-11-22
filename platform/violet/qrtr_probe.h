/* SPDX-License-Identifier: MIT */
#ifndef VIO_QRTR_PROBE_H
#define VIO_QRTR_PROBE_H
#include <stdint.h>
#include <stddef.h>

/* Runs the native QRTR discovery client over a real AF_QIPCRTR socket.
 * Read-only with respect to storage: sends NEW_LOOKUP, reports only servers
 * that were actually received. Returns 0 when found, negative errno otherwise.
 * `out` receives a multi-line measured report suitable for RESPONSE.TXT. */
int vio_qrtr_discover_live(uint32_t service, uint32_t instance,
                           uint32_t version, uint32_t timeout_ms,
                           char *out, size_t cap);

#endif
