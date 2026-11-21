/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_runtime_guard_test.c - Phase 4 guarded-runtime gate.
 *
 * argv[1] = runner executable (qeaf-sim), argv[2] = fixtures directory.
 * Proves the supervisor contains a crashing module and a hanging module, that
 * a clean run returns a 720x1560 XRGB8888 canvas, that an over-bound arena is
 * rejected end-to-end, and that the runtime is still healthy afterwards.
 * The fact that this process keeps running after the crash/hang cases IS the
 * crash-isolation evidence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf_qeapp_runtime.h"

static const char *g_runner;
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

static void path(char *buf, size_t cap, const char *name)
{
    snprintf(buf, cap, "%s/%s", g_dir, name);
}

static size_t lit_pixels(const lv_image_dsc_t *dsc)
{
    const uint32_t *px = (const uint32_t *)dsc->data;
    size_t n = (size_t)dsc->header.w * dsc->header.h;
    size_t lit = 0;
    for(size_t i = 0; i < n; i++)
        if(px[i] != 0u) lit++;
    return lit;
}

static void test_clean_run_and_canvas(void)
{
    printf("== clean guarded run + canvas ==\n");
    char pkg[512], error[256];
    path(pkg, sizeof(pkg), "hello.qeapp");
    lv_image_dsc_t canvas;
    vqeaf_native_report_t report;
    int rc = vqeaf_qeapp_native_launch(g_runner, pkg, &canvas, &report,
                                       error, sizeof(error));
    CHECK(rc == 0, "clean run returns 0");
    if(rc != 0) printf("  reason: %s\n", error);
    CHECK(report.exit_code == 0 && !report.crashed && !report.timed_out,
          "child exited cleanly");
    CHECK(strstr(report.lifecycle, "create start pause resume stop destroy") != NULL,
          "lifecycle reported");
    CHECK(report.framebuffer_hash != 0u, "framebuffer hash reported");
    CHECK(report.outstanding_allocs == 0, "no leaked allocations reported");
    CHECK(canvas.header.w == 720 && canvas.header.h == 1560, "canvas 720x1560");
    CHECK(canvas.header.cf == LV_COLOR_FORMAT_XRGB8888, "canvas XRGB8888");
    CHECK(canvas.header.stride == 720 * 4, "canvas stride 2880");
    CHECK(canvas.data_size == 720u * 1560u * 4u, "canvas data_size");
    CHECK(lit_pixels(&canvas) > 0, "canvas has rasterized text");
}

static void test_crash_contained(void)
{
    printf("== crash contained ==\n");
    char pkg[512], error[256];
    path(pkg, sizeof(pkg), "crash.qeapp");
    vqeaf_native_report_t report;
    int rc = vqeaf_qeapp_native_launch(g_runner, pkg, NULL, &report,
                                       error, sizeof(error));
    CHECK(rc != 0, "crashing run returns nonzero");
    CHECK(report.crashed == 1, "child crash detected");
    CHECK(strstr(error, "crashed") != NULL, "error reports crash");
    CHECK(strstr(error, "shell survived") != NULL, "error states shell survived");
    printf("  reason: %s\n", error);
    /* still alive here == isolation proven */
    CHECK(1, "supervisor process survived the child crash");
}

static void test_timeout_contained(void)
{
    printf("== hang timed out ==\n");
    char pkg[512], error[256];
    path(pkg, sizeof(pkg), "hang.qeapp");
    vqeaf_native_report_t report;
    int rc = vqeaf_qeapp_native_launch(g_runner, pkg, NULL, &report,
                                       error, sizeof(error));
    CHECK(rc != 0, "hanging run returns nonzero");
    CHECK(report.timed_out == 1, "child timeout detected");
    CHECK(strstr(error, "killed") != NULL, "error reports kill");
    printf("  reason: %s\n", error);
    CHECK(1, "supervisor process survived the child hang");
}

static void test_arena_bound(void)
{
    printf("== bounded arena ==\n");
    char pkg[512], error[256];
    path(pkg, sizeof(pkg), "bigarena.qeapp");
    vqeaf_native_report_t report;
    int rc = vqeaf_qeapp_native_launch(g_runner, pkg, NULL, &report,
                                       error, sizeof(error));
    CHECK(rc != 0, "over-bound arena rejected end-to-end");
    CHECK(!report.crashed && !report.timed_out, "rejection is not a crash/timeout");
    CHECK(report.exit_code != 0, "runner exited nonzero on bound");
    printf("  reason: %s\n", error);
}

static void test_recovery(void)
{
    printf("== recovery after crash/hang ==\n");
    char pkg[512], error[256];
    path(pkg, sizeof(pkg), "hello.qeapp");
    lv_image_dsc_t canvas;
    vqeaf_native_report_t report;
    int rc = vqeaf_qeapp_native_launch(g_runner, pkg, &canvas, &report,
                                       error, sizeof(error));
    CHECK(rc == 0, "runtime healthy again after crash/hang");
    CHECK(report.outstanding_allocs == 0, "no resource leak across runs");
    CHECK(lit_pixels(&canvas) > 0, "canvas still renders after recovery");
}

int main(int argc, char **argv)
{
    if(argc < 3) {
        printf("usage: %s runner.exe fixtures-dir\n", argv[0]);
        return 2;
    }
    g_runner = argv[1];
    g_dir = argv[2];
    test_clean_run_and_canvas();
    test_crash_contained();
    test_timeout_contained();
    test_arena_bound();
    test_recovery();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    printf(g_failures ? "GUARD TEST FAILURES\n" : "GUARD TEST ALL OK\n");
    return g_failures ? 1 : 0;
}