#ifndef VQEAF_SECURITY_H
#define VQEAF_SECURITY_H
#include <stdint.h>
/* Per-task credentials for the independent kernel (F4 security model).
 *
 * Every task carries a uid, a gid and a capability mask. Kernel tasks run as
 * the system identity with VQEAF_CAP_ALL. EL0 processes get only what their
 * launcher grants; .qen apps launched by AMS get a per-app uid and an app
 * capability bundle, never VQEAF_CAP_SYSTEM or VQEAF_CAP_FS_ADMIN.
 *
 * Enforcement point is the EL0 SVC dispatcher (kernel/arch/aarch64/irq.c):
 * vqeaf_sec_syscall_cap() names the capability a syscall number requires and
 * vqeaf_sec_has() decides. VFS access is a second check inside vfs.c, against
 * the vnode's owner/mode, so a capability alone cannot cross an app boundary.
 *
 * Threat model: a hostile or buggy EL0 process, not a compromised EL1. The
 * kernel is the only code that runs privileged; user pointers are validated
 * before every copy and a denial returns -EACCES instead of touching state. */

#define VQEAF_UID_SYSTEM 0u
#define VQEAF_GID_SYSTEM 0u
#define VQEAF_GID_APPS 10000u
#define VQEAF_UID_FIRST_APP 10001
/* Kept as a literal so it can be stringized into a VFS path; a static assert
 * in security.c keeps it equal to VQEAF_UID_FIRST_APP + 1. */
#define VQEAF_UID_SECOND_APP 10002
#define VQEAF_UID_APP_SLOTS 8u

#define VQEAF_CAP_CONSOLE    (1ull << 0)  /* sys_putchar / sys_print            */
#define VQEAF_CAP_IPC_SEND   (1ull << 1)  /* sys_send                           */
#define VQEAF_CAP_IPC_RECV   (1ull << 2)  /* sys_recv                           */
#define VQEAF_CAP_SURFACE    (1ull << 3)  /* sys_submit_surface / sys_frame_done*/
#define VQEAF_CAP_INPUT      (1ull << 4)  /* sys_poll_input                     */
#define VQEAF_CAP_WINDOW     (1ull << 5)  /* create / present / set_z / close   */
#define VQEAF_CAP_APP_LAUNCH (1ull << 6)  /* sys_launch                         */
#define VQEAF_CAP_FS_READ    (1ull << 7)  /* sys_open(read) / sys_read          */
#define VQEAF_CAP_FS_WRITE   (1ull << 8)  /* sys_open(write) / sys_write        */
#define VQEAF_CAP_FS_ADMIN   (1ull << 9)  /* bypass vnode mode checks           */
#define VQEAF_CAP_SYSTEM     (1ull << 10) /* reserved for kernel-owned services */
#define VQEAF_CAP_ALL        ((1ull << 11) - 1ull)

typedef struct {
    uint32_t uid;
    uint32_t gid;
    uint64_t caps;
} VqeafCred;

/* System identity used by kernel tasks and the kernel-internal boot self-tests.
 * Ordinary apps must never be created with this credential. */
#define VQEAF_CRED_SYSTEM_INIT { VQEAF_UID_SYSTEM, VQEAF_GID_SYSTEM, VQEAF_CAP_ALL }

/* What an ordinary .qen app may do: draw and read input. */
#define VQEAF_CAPS_APP_UI (VQEAF_CAP_CONSOLE | VQEAF_CAP_SURFACE | \
                           VQEAF_CAP_WINDOW | VQEAF_CAP_INPUT)
/* Headless app: console only (no display, no input, no file system). */
#define VQEAF_CAPS_APP_HEADLESS (VQEAF_CAP_CONSOLE)

/* Capability mask a syscall number requires; 0 means "no capability". */
uint64_t vqeaf_sec_syscall_cap(uint64_t nr);
/* Capabilities that only the kernel itself may hold. A .qen app launched by
 * AMS is rejected at task creation if it tries to carry either bit. */
#define VQEAF_CAP_PRIVILEGED (VQEAF_CAP_SYSTEM | VQEAF_CAP_FS_ADMIN)

/* Non-zero when every bit in caps is present in cred. */
int vqeaf_sec_has(const VqeafCred *cred, uint64_t caps);
/* Syscall policy: non-zero when cred may issue syscall nr. */
int vqeaf_sec_allows(const VqeafCred *cred, uint64_t nr);
/* Exact credential equality (uid + gid + caps). Bound VFS handles use this so
 * a handle can never outlive the identity that opened it. */
int vqeaf_sec_cred_eq(const VqeafCred *a, const VqeafCred *b);
/* Non-zero only for an ordinary app credential: never the system uid and
 * never a kernel-only capability. The boot-time system identity is created
 * through the explicitly named system path, not through this validator. */
int vqeaf_sec_user_cred_ok(const VqeafCred *cred);
#endif
