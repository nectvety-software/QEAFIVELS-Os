/* SPDX-License-Identifier: MIT */
/* platform/violet/qrtr_ns.h - userspace QRTR name service for violet.
 *
 * The stock violet kernel carries QRTR and the QMI helper, but no in-kernel
 * name service: qmi_add_lookup() posts NEW_LOOKUP to the local control port
 * and the vendor image supplies /vendor/bin/qrtr-ns to answer it.
 * Without that daemon icnss never learns that the modem exposes the WLAN
 * firmware service (WLFW), so no wlan netdev is ever created.
 */
#ifndef VIO_QRTR_NS_H
#define VIO_QRTR_NS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Bind QRTR_PORT_CTRL and serve NEW_LOOKUP/NEW_SERVER from a worker thread.
 *
 * Call this before the modem is booted. Claiming the control port makes the
 * kernel flag every existing QRTR socket with ENETRESET, which is exactly how
 * a QMI client learns it must rebind and re-send its lookups; it also resets
 * the per-endpoint HELLO state that the modem boot will re-arm.
 *
 * Idempotent. Returns 0 when the control port is bound. */
int vio_qrtr_ns_start(void);

/* 1 once the control port is bound and the worker is running. */
int vio_qrtr_ns_ready(void);

/* Multi-line truth for the QEAFRESCUE `wifi` verb: node id, bound state,
 * cached lookups/servers and the control-packet census. */
void vio_qrtr_ns_report(char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* VIO_QRTR_NS_H */
