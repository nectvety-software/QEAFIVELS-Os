/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_broker_test.c - deterministic unit gate for the Phase 5 permission
 * broker + quotas (framework/security/qeaf_broker.c). No modules, no simulator:
 * every decision is checked directly against the broker, including the audit
 * trail and deny counter. Linked through the shared simulator build, so the
 * shell file reader referenced by vqeaf_qeapp.c is stubbed exactly as elsewhere.
 */
#include <stdio.h>
#include <string.h>

#include "qeaf_broker.h"

static int g_failures;
static int g_checks;

int vqeaf_ui_read_file(const char *name, uint8_t **out, size_t *size)
{
    (void)name; (void)out; (void)size;
    return -1;
}

#define CHECK(cond, msg) do { \
    g_checks++; \
    if(cond) { printf("ok   %s\n", msg); } \
    else { printf("FAIL %s\n", msg); g_failures++; } \
} while(0)

static int audit_has(const qeaf_broker_t *b, const char *needle)
{
    for(size_t i = 0; i < qeaf_broker_audit_count(b); i++)
        if(strstr(qeaf_broker_audit_at(b, i), needle) != NULL) return 1;
    return 0;
}

static void test_init_defaults(void)
{
    printf("== init + defaults ==\n");
    qeaf_broker_t b;
    qeaf_broker_init(&b, QEAF_PERM_UI_DISPLAY | QEAF_PERM_STORAGE_READ, NULL, 0, 0);
    CHECK(qeaf_broker_granted(&b) == (QEAF_PERM_UI_DISPLAY | QEAF_PERM_STORAGE_READ),
          "granted starts at the declared ceiling");
    CHECK(b.memory_budget == QEAF_QUOTA_MEMORY_BYTES, "default memory budget 1 MiB");
    CHECK(b.call_budget_ms == QEAF_QUOTA_CALL_MS, "default per-call budget 1000 ms");
    CHECK(qeaf_broker_memory_used(&b) == 0, "no memory used initially");
    CHECK(qeaf_broker_deny_count(&b) == 0, "no denials initially");
    CHECK(qeaf_broker_audit_count(&b) == 0, "audit empty initially");
    CHECK(b.scope[0] == '\0', "NULL scope is safety-only");

    qeaf_broker_init(&b, QEAF_PERM_UI_DISPLAY, "data/", 256, 50);
    CHECK(b.memory_budget == 256, "explicit memory budget honored");
    CHECK(b.call_budget_ms == 50, "explicit per-call budget honored");
    CHECK(strcmp(b.scope, "data/") == 0, "explicit scope stored");
}

static void test_check_request_revoke(void)
{
    printf("== check / request / revoke ==\n");
    qeaf_broker_t b;
    qeaf_broker_init(&b, QEAF_PERM_UI_DISPLAY | QEAF_PERM_STORAGE_READ, NULL, 0, 0);
    CHECK(qeaf_broker_check(&b, QEAF_PERM_UI_DISPLAY) == 1, "check granted permission");
    CHECK(qeaf_broker_check(&b, QEAF_PERM_STORAGE_WRITE_APP) == 0, "check undeclared permission fails");
    CHECK(qeaf_broker_check(&b, 0) == 0, "check zero permission fails");

    CHECK(qeaf_broker_request(&b, QEAF_PERM_STORAGE_WRITE_APP) == 0,
          "request beyond ceiling denied");
    CHECK(qeaf_broker_check(&b, QEAF_PERM_STORAGE_WRITE_APP) == 0,
          "denied request did not grant");
    CHECK(audit_has(&b, "undeclared"), "undeclared request audited");

    CHECK(qeaf_broker_request(&b, 0) == 0, "request unknown permission denied");
    CHECK(audit_has(&b, "unknown-permission"), "unknown request audited");

    qeaf_broker_revoke(&b, QEAF_PERM_STORAGE_READ);
    CHECK(qeaf_broker_check(&b, QEAF_PERM_STORAGE_READ) == 0, "revoke removes grant");
    CHECK(audit_has(&b, "revoke"), "revoke audited");
    CHECK(qeaf_broker_request(&b, QEAF_PERM_STORAGE_READ) == 1,
          "re-request within ceiling re-grants");
    CHECK(qeaf_broker_check(&b, QEAF_PERM_STORAGE_READ) == 1, "re-grant active");
    CHECK(audit_has(&b, "grant"), "grant audited");
    CHECK(qeaf_broker_deny_count(&b) == 2, "two denials counted (undeclared + unknown)");
}

static void test_least_privilege_ceiling(void)
{
    printf("== least-privilege ceiling ==\n");
    qeaf_broker_t b;
    qeaf_broker_init(&b, QEAF_PERM_UI_DISPLAY, NULL, 0, 0);
    /* Every permission not declared must be refused, even combined. */
    CHECK(qeaf_broker_request(&b, QEAF_PERM_STORAGE_READ) == 0, "storage.read refused");
    CHECK(qeaf_broker_request(&b, QEAF_PERM_STORAGE_WRITE_APP) == 0, "storage.write refused");
    CHECK(qeaf_broker_request(&b, QEAF_PERM_INPUT_RECEIVE) == 0, "input.receive refused");
    CHECK(qeaf_broker_request(&b, QEAF_PERM_MEDIA_READ) == 0, "media.read refused");
    CHECK(qeaf_broker_request(&b, QEAF_PERM_UI_DISPLAY | QEAF_PERM_MEDIA_READ) == 0,
          "mixed grant exceeding ceiling refused");
    CHECK(qeaf_broker_granted(&b) == QEAF_PERM_UI_DISPLAY, "granted never exceeds ceiling");
    CHECK(qeaf_broker_deny_count(&b) == 5, "all five escalation attempts denied");
}

static void test_path_safety(void)
{
    printf("== path safety (no scope) ==\n");
    qeaf_broker_t b;
    qeaf_broker_init(&b, QEAF_PERM_STORAGE_READ, NULL, 0, 0);
    CHECK(qeaf_broker_check_path(&b, "data/hello.txt", 0) == 1, "safe relative path allowed");
    CHECK(qeaf_broker_check_path(&b, "", 0) == 0, "empty path denied");
    CHECK(qeaf_broker_check_path(&b, NULL, 0) == 0, "NULL path denied");
    CHECK(qeaf_broker_check_path(&b, "/etc/passwd", 0) == 0, "absolute POSIX path denied");
    CHECK(qeaf_broker_check_path(&b, "\\windows\\x", 0) == 0, "absolute backslash path denied");
    CHECK(qeaf_broker_check_path(&b, "C:/windows/x", 0) == 0, "drive-qualified path denied");
    CHECK(qeaf_broker_check_path(&b, "data\\hello.txt", 0) == 0, "backslash separator denied");
    CHECK(qeaf_broker_check_path(&b, "../secret", 0) == 0, "leading traversal denied");
    CHECK(qeaf_broker_check_path(&b, "data/../secret", 0) == 0, "mid traversal denied");
    CHECK(qeaf_broker_check_path(&b, "data/..", 0) == 0, "trailing traversal denied");
    CHECK(qeaf_broker_check_path(&b, "data/..hidden", 0) == 1, "dotdot-prefixed name is not traversal");
    CHECK(audit_has(&b, "traversal"), "traversal audited");
    CHECK(audit_has(&b, "absolute"), "absolute audited");
    CHECK(audit_has(&b, "drive"), "drive audited");
    CHECK(audit_has(&b, "backslash"), "backslash audited");
}

static void test_path_scope(void)
{
    printf("== path scope containment ==\n");
    qeaf_broker_t b;
    qeaf_broker_init(&b, QEAF_PERM_STORAGE_READ, "data/", 0, 0);
    CHECK(qeaf_broker_check_path(&b, "data/hello.txt", 0) == 1, "in-scope path allowed");
    CHECK(qeaf_broker_check_path(&b, "data/sub/x.bin", 0) == 1, "nested in-scope path allowed");
    CHECK(qeaf_broker_check_path(&b, "other/x.txt", 0) == 0, "out-of-scope path denied");
    CHECK(qeaf_broker_check_path(&b, "database/x", 0) == 0, "prefix-substring escape denied");
    CHECK(qeaf_broker_check_path(&b, "data", 0) == 0, "scope root without slash denied");
    CHECK(audit_has(&b, "out-of-scope"), "out-of-scope audited");
}

static void test_write_gate(void)
{
    printf("== write permission gate ==\n");
    qeaf_broker_t b;
    qeaf_broker_init(&b, QEAF_PERM_STORAGE_READ, NULL, 0, 0);
    CHECK(qeaf_broker_check_path(&b, "data/hello.txt", 1) == 0,
          "write without storage.write.app denied");
    CHECK(audit_has(&b, "write-permission"), "write-permission denial audited");

    qeaf_broker_init(&b, QEAF_PERM_STORAGE_READ | QEAF_PERM_STORAGE_WRITE_APP, NULL, 0, 0);
    CHECK(qeaf_broker_check_path(&b, "data/hello.txt", 1) == 1,
          "write with storage.write.app allowed");
    CHECK(qeaf_broker_check_path(&b, "../escape", 1) == 0,
          "write still subject to traversal guard");
}

static void test_memory_quota(void)
{
    printf("== memory quota ==\n");
    qeaf_broker_t b;
    qeaf_broker_init(&b, QEAF_PERM_STORAGE_READ, NULL, 100, 0);
    CHECK(qeaf_broker_mem_acquire(&b, 60) == 1, "acquire within budget");
    CHECK(qeaf_broker_memory_used(&b) == 60, "used charged");
    CHECK(qeaf_broker_mem_acquire(&b, 50) == 0, "acquire over budget denied");
    CHECK(qeaf_broker_memory_used(&b) == 60, "denied acquire does not charge");
    CHECK(audit_has(&b, "memory quota"), "over-budget audited");
    CHECK(qeaf_broker_mem_acquire(&b, 40) == 1, "acquire exact remainder");
    CHECK(qeaf_broker_memory_used(&b) == 100, "budget fully used");
    CHECK(qeaf_broker_mem_acquire(&b, 1) == 0, "acquire when full denied");
    qeaf_broker_mem_release(&b, 60);
    CHECK(qeaf_broker_memory_used(&b) == 40, "release returns bytes");
    CHECK(qeaf_broker_mem_acquire(&b, 60) == 1, "acquire after release");
    qeaf_broker_mem_release(&b, 1000);
    CHECK(qeaf_broker_memory_used(&b) == 0, "over-release clamps to zero");
    CHECK(qeaf_broker_deny_count(&b) == 2, "two quota denials counted");
}

int main(void)
{
    test_init_defaults();
    test_check_request_revoke();
    test_least_privilege_ceiling();
    test_path_safety();
    test_path_scope();
    test_write_gate();
    test_memory_quota();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    printf(g_failures ? "BROKER TEST FAILURES\n" : "BROKER TEST ALL OK\n");
    return g_failures ? 1 : 0;
}