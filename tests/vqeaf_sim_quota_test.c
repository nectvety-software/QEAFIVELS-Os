/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_sim_quota_test.c - deterministic integration gate for the Phase 5
 * broker/quota wiring inside the real host simulator (framework/simulator/
 * qeaf_sim.c). Signed .qeapp v2 fixtures are built by scripts/test_qeapp_broker.py
 * and passed as argv[1]. Exercises path-scope containment, traversal/absolute/
 * drive/backslash denial, the dynamic-memory quota, permission-escalation denial
 * with audit, and the per-call CPU timeout (finite slow fixture + tiny budget).
 *
 * vqeaf_qeapp.c references the shell file reader; the simulator only uses
 * inspect_native, so the reader is stubbed here exactly as in the other drivers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qeaf_sim.h"

static const char *g_dir;
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

static uint8_t *load_pkg(const char *name, size_t *size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    FILE *file = fopen(path, "rb");
    if(file == NULL) { printf("FAIL cannot open %s\n", path); g_failures++; return NULL; }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if(length <= 0) { fclose(file); return NULL; }
    uint8_t *buffer = malloc((size_t)length);
    if(buffer == NULL) { fclose(file); return NULL; }
    if(fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        fclose(file); free(buffer); return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return buffer;
}

static qeaf_sim_t *open_quota(const char *name, const qeaf_sim_file_t *files,
                              size_t file_count, const char *scope, size_t mem,
                              uint32_t call_ms, char *error, size_t cap)
{
    size_t size = 0;
    uint8_t *pkg = load_pkg(name, &size);
    if(pkg == NULL) return NULL;
    qeaf_sim_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.package = pkg;
    cfg.package_size = size;
    cfg.files = files;
    cfg.file_count = file_count;
    cfg.scope = scope;
    cfg.memory_budget = mem;
    cfg.call_budget_ms = call_ms;
    qeaf_sim_t *sim = qeaf_sim_new(&cfg, error, cap);
    free(pkg);
    return sim;
}

static int log_has(const qeaf_sim_t *sim, const char *needle)
{
    for(size_t i = 0; i < qeaf_sim_log_count(sim); i++)
        if(strstr(qeaf_sim_log_at(sim, i), needle) != NULL) return 1;
    return 0;
}

static int audit_has(const qeaf_sim_t *sim, const char *needle)
{
    for(size_t i = 0; i < qeaf_sim_audit_count(sim); i++)
        if(strstr(qeaf_sim_audit_at(sim, i), needle) != NULL) return 1;
    return 0;
}

static const uint8_t CONTENT[] = "native storage OK"; /* 18 bytes incl NUL */

static void seed_file(qeaf_sim_file_t *f)
{
    f->path = "data/hello.txt";
    f->data = CONTENT;
    f->size = sizeof(CONTENT);
}

static void test_in_scope_read(void)
{
    printf("== in-scope read still works ==\n");
    char error[256];
    qeaf_sim_file_t files[1];
    seed_file(&files[0]);
    qeaf_sim_t *sim = open_quota("probe.qeapp", files, 1, "data/", 0, 0, error, sizeof(error));
    CHECK(sim != NULL, "probe loads with scope data/");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK(qeaf_sim_run(sim, error, sizeof(error)) == 0, "in-scope run returns 0");
    CHECK(log_has(sim, "native storage OK"), "in-scope file content logged");
    CHECK(qeaf_sim_deny_count(sim) == 0, "no denials for in-scope read");
    CHECK(qeaf_sim_memory_used(sim) == 0, "memory returned after free");
    CHECK(qeaf_sim_outstanding_allocs(sim) == 0, "no leak");
    qeaf_sim_free(sim);
}

static void test_out_of_scope(void)
{
    printf("== out-of-scope read denied + audited ==\n");
    char error[256];
    qeaf_sim_file_t files[1];
    seed_file(&files[0]);
    qeaf_sim_t *sim = open_quota("probe.qeapp", files, 1, "other/", 0, 0, error, sizeof(error));
    CHECK(sim != NULL, "probe loads with scope other/");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK(qeaf_sim_run(sim, error, sizeof(error)) == 0, "denied read does not crash run");
    CHECK(log_has(sim, "read_file denied"), "module saw read denial");
    CHECK(qeaf_sim_deny_count(sim) >= 1, "broker counted the denial");
    CHECK(audit_has(sim, "out-of-scope"), "out-of-scope audited");
    CHECK(qeaf_sim_outstanding_allocs(sim) == 0, "no allocation on denial");
    CHECK(qeaf_sim_memory_used(sim) == 0, "no memory charged on denial");
    qeaf_sim_free(sim);
}

static void test_unsafe_paths(void)
{
    printf("== traversal / absolute / drive / backslash denied ==\n");
    char error[256];
    qeaf_sim_t *sim = open_quota("badpath.qeapp", NULL, 0, NULL, 0, 0, error, sizeof(error));
    CHECK(sim != NULL, "badpath loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK((qeaf_sim_permissions(sim) & QEAF_PERM_STORAGE_READ) != 0, "storage.read granted");
    CHECK(qeaf_sim_run(sim, error, sizeof(error)) == 0, "badpath run returns 0");
    CHECK(!log_has(sim, "UNEXPECTED success"), "no unsafe path succeeded");
    CHECK(qeaf_sim_deny_count(sim) == 4, "all four unsafe paths denied");
    CHECK(audit_has(sim, "traversal"), "traversal audited");
    CHECK(audit_has(sim, "absolute"), "absolute audited");
    CHECK(audit_has(sim, "drive"), "drive audited");
    CHECK(audit_has(sim, "backslash"), "backslash audited");
    qeaf_sim_free(sim);
}

static void test_memory_quota(void)
{
    printf("== memory quota denial + audited ==\n");
    char error[256];
    qeaf_sim_file_t files[1];
    seed_file(&files[0]); /* 18 bytes */
    qeaf_sim_t *sim = open_quota("probe.qeapp", files, 1, NULL, 4, 0, error, sizeof(error));
    CHECK(sim != NULL, "probe loads with 4-byte budget");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK(qeaf_sim_run(sim, error, sizeof(error)) == 0, "quota denial does not crash run");
    CHECK(log_has(sim, "read_file denied"), "module saw quota denial");
    CHECK(qeaf_sim_deny_count(sim) >= 1, "broker counted quota denial");
    CHECK(audit_has(sim, "memory quota"), "memory quota audited");
    CHECK(qeaf_sim_outstanding_allocs(sim) == 0, "no allocation over quota");
    CHECK(qeaf_sim_memory_used(sim) == 0, "no memory charged over quota");
    qeaf_sim_free(sim);
}

static void test_escalation(void)
{
    printf("== permission escalation denied + audited ==\n");
    char error[256];
    qeaf_sim_t *sim = open_quota("escalate.qeapp", NULL, 0, NULL, 0, 0, error, sizeof(error));
    CHECK(sim != NULL, "escalate loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK(qeaf_sim_run(sim, error, sizeof(error)) == 0, "escalate run returns 0");
    CHECK(log_has(sim, "write denied"), "undeclared write denied");
    CHECK(log_has(sim, "media denied"), "undeclared media denied");
    CHECK(log_has(sim, "unknown denied"), "unknown permission denied");
    CHECK(!log_has(sim, "GRANTED"), "nothing beyond ceiling granted");
    CHECK(qeaf_sim_deny_count(sim) == 3, "three escalation denials counted");
    CHECK(audit_has(sim, "undeclared"), "undeclared request audited");
    CHECK(audit_has(sim, "unknown-permission"), "unknown request audited");
    CHECK((qeaf_sim_permissions(sim) & QEAF_PERM_STORAGE_WRITE_APP) == 0,
          "granted mask still excludes write");
    qeaf_sim_free(sim);
}

static void test_permission_denial(void)
{
    printf("== permission-bit denial (probe-noperm) ==\n");
    char error[256];
    qeaf_sim_file_t files[1];
    seed_file(&files[0]);
    qeaf_sim_t *sim = open_quota("probe-noperm.qeapp", files, 1, NULL, 0, 0, error, sizeof(error));
    CHECK(sim != NULL, "probe-noperm loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK((qeaf_sim_permissions(sim) & QEAF_PERM_STORAGE_READ) == 0, "storage.read NOT granted");
    CHECK(qeaf_sim_run(sim, error, sizeof(error)) == 0, "denied read does not crash run");
    CHECK(log_has(sim, "read_file denied"), "read denied without permission");
    CHECK(qeaf_sim_outstanding_allocs(sim) == 0, "no allocation on denial");
    qeaf_sim_free(sim);
}

static void test_call_timeout(void)
{
    printf("== per-call CPU timeout (finite slow fixture) ==\n");
    char error[256];
    qeaf_sim_t *sim = open_quota("slow.qeapp", NULL, 0, NULL, 0, 5, error, sizeof(error));
    CHECK(sim != NULL, "slow loads with 5 ms budget");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    int rc = qeaf_sim_run(sim, error, sizeof(error));
    CHECK(rc != 0, "overrunning call fails the run");
    CHECK(strstr(error, "per-call CPU budget") != NULL, "error reports per-call budget");
    CHECK(qeaf_sim_call_timeout(sim) == 1, "call_timeout flag set");
    CHECK(qeaf_sim_max_call_ms(sim) > 5, "measured call exceeded 5 ms");
    printf("  max call: %u ms\n", qeaf_sim_max_call_ms(sim));
    qeaf_sim_free(sim);
}

static void test_call_budget_ok(void)
{
    printf("== normal call within budget (positive control) ==\n");
    char error[256];
    qeaf_sim_t *sim = open_quota("hello.qeapp", NULL, 0, NULL, 0, 1000, error, sizeof(error));
    CHECK(sim != NULL, "hello loads with 1000 ms budget");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK(qeaf_sim_run(sim, error, sizeof(error)) == 0, "hello run returns 0");
    CHECK(qeaf_sim_call_timeout(sim) == 0, "no timeout for fast calls");
    CHECK(qeaf_sim_max_call_ms(sim) <= 1000, "max call within budget");
    qeaf_sim_free(sim);
}

int main(int argc, char **argv)
{
    /* MinGW's buffered stdout flush at CRT teardown segfaulted this gate after
     * every check had already passed when output was captured through a pipe.
     * Unbuffered output makes the result deterministic on the host toolchain. */
    setvbuf(stdout, NULL, _IONBF, 0);
    if(argc < 2) {
        printf("usage: %s fixtures-dir\n", argv[0]);
        return 2;
    }
    g_dir = argv[1];
    test_in_scope_read();
    test_out_of_scope();
    test_unsafe_paths();
    test_memory_quota();
    test_escalation();
    test_permission_denial();
    test_call_timeout();
    test_call_budget_ok();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    printf(g_failures ? "SIM QUOTA TEST FAILURES\n" : "SIM QUOTA TEST ALL OK\n");
    return g_failures ? 1 : 0;
}