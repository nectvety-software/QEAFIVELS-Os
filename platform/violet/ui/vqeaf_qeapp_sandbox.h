/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_qeapp_sandbox.h - process sandbox for native QEAPP.
 *
 * A .qeapp module is owner-signed but it is still code from outside the shell,
 * so the runner only enters module code inside a child that gave up every
 * privilege the shell has. apply() is fail-closed: 0 means the identity
 * change, no_new_privs, the descriptor sweep and the seccomp syscall filter are
 * all in force; any layer the kernel refuses keeps the app blocked.
 */
#ifndef VQEAF_QEAPP_SANDBOX_H
#define VQEAF_QEAPP_SANDBOX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dedicated unprivileged identity for a launched QEAPP. Picked above the
 * Third-party app band (10000..19999) so it can never collide with a stock app id
 * on the SM6150 kernel. */
#define VQEAF_QEAPP_SANDBOX_UID 20000u
#define VQEAF_QEAPP_SANDBOX_GID 20000u

/* Applies the sandbox to the calling process, in the order the kernel requires:
 * no_new_privs -> unprivileged uid/gid -> descriptor sweep and rlimits ->
 * seccomp allowlist last. Returns 0 only on full success, -1 with a reason in
 * `error`. Must be called after fork() and before any module code runs. */
int vqeaf_qeapp_sandbox_apply(char *error, size_t cap);

/* Identity the runner reports and the host test asserts. */
unsigned vqeaf_qeapp_sandbox_uid(void);
unsigned vqeaf_qeapp_sandbox_gid(void);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_QEAPP_SANDBOX_H */
