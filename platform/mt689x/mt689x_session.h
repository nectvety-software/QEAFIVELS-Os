/* SPDX-License-Identifier: MIT */
/* platform/mt689x/mt689x_session.h - userspace shell selection.
 *
 * QEAF renders the phone shell itself through DRM/KMS; Denial is an optional
 * Wayland compositor. The platform never assumes Denial is installed: it runs
 * only when the binary is present and the user selected it, and the QEAF shell
 * comes up when it is absent or exits.
 */
#ifndef QEAF_MT689X_SESSION_H
#define QEAF_MT689X_SESSION_H

/* Reads /etc/qeaf/session ("qeaf" or "denial"). Returns 1 for Denial only
 * when the marker says so AND the binary exists. */
int mtk_session_wants_denial(const char *denial_path);

/* Power-on chooser: for the first timeout_ms, VOL+ selects Denial and VOL-
 * selects the QEAF shell. Writes the choice to /etc/qeaf/session and returns
 * 1 when a key was seen, 0 when the boot marker was left unchanged. */
int mtk_session_boot_choose(unsigned timeout_ms);

/* Executes the compositor with the runtime directory prepared and waits for
 * it. Returns 0 when it ran and exited, -1 when it could not start. The
 * caller then falls back to the QEAF shell. */
int mtk_session_run_denial(const char *denial_path);

/* Writes the per-boot session log line used by the rescue `status` verb. */
const char *mtk_session_note(void);

#endif /* QEAF_MT689X_SESSION_H */
