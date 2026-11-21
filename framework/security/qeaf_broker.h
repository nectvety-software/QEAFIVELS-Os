/* SPDX-License-Identifier: MIT */
/* framework/security/qeaf_broker.h - QEAPP permission broker + quotas (Phase 5).
 *
 * Least-privilege permission broker shared by the host simulator and (later)
 * the device runtime. The manifest declares a permission ceiling; the broker
 * grants only within that ceiling, can revoke grants, and audits every
 * decision. It also enforces a per-app storage path scope and a dynamic-memory
 * quota. Portable C11: no OS, libc-nonportable, or hardware dependencies.
 *
 * Threat model: a signed but malicious QEAPP module tries to (a) use a
 * permission it never declared, (b) re-grant a revoked permission, (c) read
 * outside its storage scope via traversal/absolute paths, or (d) exhaust host
 * memory. The broker denies each and records an audit entry; the module never
 * receives a kernel pointer, hardware register, or libc entry point.
 */
#ifndef VQEAF_QEAF_BROKER_H
#define VQEAF_QEAF_BROKER_H

#include <stddef.h>
#include <stdint.h>

#include "qeaf_native_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QEAF_BROKER_MAX_AUDIT   64
#define QEAF_BROKER_AUDIT_LEN   96
#define QEAF_BROKER_SCOPE_MAX   128

/* Default Phase 5 quotas. A caller may override per app via the simulator
 * config; zero means "use the default". */
#define QEAF_QUOTA_MEMORY_BYTES (1u * 1024u * 1024u) /* dynamic read buffers */
#define QEAF_QUOTA_CALL_MS      1000u                /* per native lifecycle call */
#define QEAF_QUOTA_RUN_MS       5000u                /* whole run (supervisor) */

typedef struct {
    uint32_t declared;        /* manifest ceiling; grants can never exceed it */
    uint32_t granted;         /* currently active permission bits */
    char scope[QEAF_BROKER_SCOPE_MAX]; /* storage scope root; "" = safety-only */
    size_t memory_budget;     /* max outstanding dynamic bytes */
    size_t memory_used;       /* currently outstanding dynamic bytes */
    uint32_t call_budget_ms;  /* per-call wall-clock budget */
    uint32_t deny_count;      /* total denied decisions (audit metric) */
    size_t audit_count;       /* entries recorded (capped at MAX_AUDIT) */
    char audit[QEAF_BROKER_MAX_AUDIT][QEAF_BROKER_AUDIT_LEN];
} qeaf_broker_t;

/* Initialise with the manifest ceiling (granted = declared), a storage scope
 * root (NULL/"" = no containment, safety checks still apply), a memory budget
 * and a per-call budget (0 -> defaults). */
void qeaf_broker_init(qeaf_broker_t *b, uint32_t declared, const char *scope,
                      size_t memory_budget, uint32_t call_budget_ms);

/* Permission decisions. check is a silent query; request grants only within
 * the declared ceiling and audits; revoke removes a grant and audits. */
int qeaf_broker_check(const qeaf_broker_t *b, uint32_t perm);
int qeaf_broker_request(qeaf_broker_t *b, uint32_t perm);
void qeaf_broker_revoke(qeaf_broker_t *b, uint32_t perm);

/* Path scope. Returns 1 only when path is a safe relative path inside the
 * scope (no "..", no absolute/drive path, no backslash). want_write additionally
 * requires storage.write.app. Denials are audited. */
int qeaf_broker_check_path(qeaf_broker_t *b, const char *path, int want_write);

/* Dynamic-memory quota. acquire returns 1 and charges bytes when within
 * budget, else 0 (audited). release returns bytes to the budget. */
int qeaf_broker_mem_acquire(qeaf_broker_t *b, size_t bytes);
void qeaf_broker_mem_release(qeaf_broker_t *b, size_t bytes);

/* Inspection. */
size_t qeaf_broker_audit_count(const qeaf_broker_t *b);
const char *qeaf_broker_audit_at(const qeaf_broker_t *b, size_t index);
uint32_t qeaf_broker_deny_count(const qeaf_broker_t *b);
size_t qeaf_broker_memory_used(const qeaf_broker_t *b);
uint32_t qeaf_broker_granted(const qeaf_broker_t *b);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_QEAF_BROKER_H */