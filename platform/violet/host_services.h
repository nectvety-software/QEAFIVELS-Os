/* SPDX-License-Identifier: MIT */
/* platform/violet/host_services.h - QRTR host daemons for real WCN3990 Wi-Fi.
 *
 * The stock kernel hosts the QRTR transport but the name service and the
 * service registry / firmware fetchers live in userspace. Without them the
 * modem has no PD map and no way to read WLAN firmware, so icnss never sees
 * the WLFW service and no wlan0 appears. */
#ifndef VIO_HOST_SERVICES_H
#define VIO_HOST_SERVICES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start each daemon that is actually packaged in /sbin/qeaf. Idempotent;
 * never invents a service when the binary is missing. */
int vio_host_services_start(void);

/* Fail-closed WLAN-PD policy. The marker is a release gate, not a credential. */
int vio_host_wlan_policy_ready(void);
const char *vio_host_wlan_policy_note(void);

/* Called once the modem subsystem is online: copies the per-unit board files
 * from persist, starts wlfwd and drives /dev/wlan. Idempotent. */
void vio_host_services_start_wlan(void);

/* 1 once /dev/wlan accepted "ON". */
int vio_host_wlan_ready(void);

/* Synchronous attempt to load the WLAN driver; 0 on success, -errno otherwise.
 * Exposed to QEAFRESCUE as `wifi on` so timing can be controlled from host. */
int vio_host_wlan_on(void);

/* Fire the vendored pdr-restart tool at servreg: `node port pd-name`.
 * Output goes to /tmp/hostsvc-pdr-restart.log for `wifi log`. */
int vio_host_pd_restart(const char *node, const char *port, const char *name);

/* Ask the servreg notifier to track a PD: pd-listen <node> <port> <path>. */
int vio_host_pd_listen(const char *node, const char *port, const char *name);

/* Try the device's own vendor Wi-Fi daemon (bionic binary). */
int vio_host_vendor_wifi(void);

/* RAM-only test session: stack rw tmpfs over /mnt/vendor/persist and write a
 * clearly-labelled TEST wlan_mac.bin. Never touches p74. Returns a report in
 * `out`. Use only for diagnosis; all changes vanish on reboot. */
int vio_host_persist_ram_setup(char *out, int cap);

/* Diagnostic: relaunch the real ROM cnss-daemon with LD_DEBUG=all and verbose
 * Android logging into /tmp/hostsvc-cnss-trace.log. No mock or bypass. */
int vio_host_cnss_trace(char *out, int cap);

/* Run a real stock Bionic binary through the staged linker64 with the device's
 * real library paths. Output goes to /tmp/hostsvc-<logname>.log for `wifi log`.
 * Returns 0 when the child was spawned, negative on fork failure. This is a
 * diagnostic runner for stock binaries (wpa_supplicant / GNSS HAL), never a
 * stub: the binary and all its libraries come from the violet ROM. */
int vio_host_run_bionic(const char *binary, const char *logname,
                        const char *const *extra_args);

/* Last lines of each daemon's stdout/stderr for QEAFRESCUE `wifi log`. */
/* Run the vendored qrtr-lookup tool for service 0x45; output goes to
 * /tmp/hostsvc-qlookup.log for `wifi log`. */
void vio_host_qrtr_lookup(void);

/* Start the pure-Linux WLFW 0x45 server (build/violet/hosttools/qwlfgw).
 * No Bionic, APEX, binder or chroot is involved. */
int vio_host_wlfw_native(void);

void vio_host_services_logs(char *out, int cap);

/* Search the per-unit storage roots for a named board file. */
int vio_host_find_file(const char *name, char *out, int cap);

/* Multi-line truth for the QEAFRESCUE `wifi` verb: binary present or not,
 * pid, and exit status if it died. */
void vio_host_services_report(char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif
