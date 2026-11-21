/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_sim_test.c - deterministic host-simulator gate.
 *
 * Drives the real simulator core (framework/simulator/qeaf_sim.c) against
 * signed .qeapp v2 fixtures built by scripts/test_qeapp_simulator.py. The
 * fixtures directory is argv[1]. Every assertion is deterministic: lifecycle
 * order, full-run trace, repeatability, storage cleanup, permission denial,
 * leak detection, structural rejections and rasterized framebuffer pixels.
 *
 * vqeaf_qeapp.c references the shell file reader; the simulator only uses
 * inspect_native, so the reader is stubbed here exactly as in the CLI driver.
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

static qeaf_sim_t *open_sim(const char *name, const qeaf_sim_file_t *files,
                            size_t file_count, const qeaf_input_event_t *input,
                            size_t input_count, char *error, size_t cap)
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
    cfg.input = input;
    cfg.input_count = input_count;
    qeaf_sim_t *sim = qeaf_sim_new(&cfg, error, cap);
    free(pkg);
    return sim;
}

static qeaf_sim_t *open_device_sim(const char *name, const char *device_id,
                                   const qeaf_input_event_t *input,
                                   size_t input_count, char *error, size_t cap)
{
    size_t size = 0;
    uint8_t *pkg = load_pkg(name, &size);
    if(pkg == NULL) return NULL;
    qeaf_sim_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.package = pkg;
    cfg.package_size = size;
    cfg.input = input;
    cfg.input_count = input_count;
    cfg.device = qeaf_device_find(device_id);
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

static void test_lifecycle_order(void)
{
    printf("== lifecycle order ==\n");
    char error[256];
    qeaf_sim_t *sim = open_sim("hello.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(sim != NULL, "hello loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK(strcmp(qeaf_sim_state(sim), "loaded") == 0, "initial state loaded");
    CHECK(qeaf_sim_start(sim, error, sizeof(error)) != 0, "start before create rejected");
    CHECK(qeaf_sim_pause(sim, error, sizeof(error)) != 0, "pause before create rejected");
    CHECK(qeaf_sim_destroy_app(sim, error, sizeof(error)) != 0, "destroy before create rejected");
    CHECK(qeaf_sim_create_app(sim, error, sizeof(error)) == 0, "create succeeds");
    CHECK(strcmp(qeaf_sim_state(sim), "created") == 0, "state created");
    CHECK(qeaf_sim_create_app(sim, error, sizeof(error)) != 0, "double create rejected");
    CHECK(qeaf_sim_start(sim, error, sizeof(error)) == 0, "start succeeds");
    CHECK(qeaf_sim_stop(sim, error, sizeof(error)) == 0, "stop succeeds");
    CHECK(qeaf_sim_destroy_app(sim, error, sizeof(error)) == 0, "destroy succeeds");
    CHECK(strcmp(qeaf_sim_state(sim), "destroyed") == 0, "state destroyed");
    CHECK(qeaf_sim_create_app(sim, error, sizeof(error)) != 0, "create after destroy rejected");
    qeaf_sim_free(sim);
}

static void test_full_run_trace(void)
{
    printf("== full run trace ==\n");
    char error[256];
    qeaf_sim_t *sim = open_sim("hello.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(sim != NULL, "hello loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    int rc = qeaf_sim_run(sim, error, sizeof(error));
    CHECK(rc == 0, "full lifecycle run returns 0");
    if(rc != 0) printf("  reason: %s\n", error);
    static const char *want[] = {"create", "start", "pause", "resume", "stop", "destroy"};
    CHECK(qeaf_sim_trace_count(sim) == 6, "trace has 6 events");
    int order_ok = qeaf_sim_trace_count(sim) == 6;
    for(size_t i = 0; order_ok && i < 6; i++)
        order_ok = strcmp(qeaf_sim_trace_at(sim, i), want[i]) == 0;
    CHECK(order_ok, "trace order create start pause resume stop destroy");
    CHECK(strcmp(qeaf_sim_state(sim), "destroyed") == 0, "ends destroyed");
    CHECK(qeaf_sim_outstanding_allocs(sim) == 0, "no outstanding allocations");
    CHECK(qeaf_sim_log_count(sim) == 5, "hello logs 5 lifecycle lines");
    CHECK(qeaf_sim_draw_count(sim) == 1, "hello draws once");
    CHECK(qeaf_sim_framebuffer_hash(sim) != 0u, "framebuffer hash nonzero");
    qeaf_sim_free(sim);
}

static void test_determinism(void)
{
    printf("== determinism ==\n");
    char error[256];
    qeaf_sim_t *a = open_sim("hello.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    qeaf_sim_t *b = open_sim("hello.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(a != NULL && b != NULL, "two sims load");
    if(a == NULL || b == NULL) { qeaf_sim_free(a); qeaf_sim_free(b); return; }
    CHECK(qeaf_sim_run(a, error, sizeof(error)) == 0, "run A ok");
    CHECK(qeaf_sim_run(b, error, sizeof(error)) == 0, "run B ok");
    CHECK(qeaf_sim_framebuffer_hash(a) == qeaf_sim_framebuffer_hash(b),
          "framebuffer hash identical across runs");
    CHECK(qeaf_sim_log_count(a) == qeaf_sim_log_count(b), "log count identical");
    CHECK(qeaf_sim_draw_count(a) == qeaf_sim_draw_count(b), "draw count identical");
    int same = qeaf_sim_log_count(a) == qeaf_sim_log_count(b);
    for(size_t i = 0; same && i < qeaf_sim_log_count(a); i++)
        same = strcmp(qeaf_sim_log_at(a, i), qeaf_sim_log_at(b, i)) == 0;
    CHECK(same, "every log line identical");
    qeaf_sim_free(a);
    qeaf_sim_free(b);
}

static void test_storage_cleanup(void)
{
    printf("== storage read/free + input ==\n");
    char error[256];
    static const uint8_t content[] = "native storage OK";
    qeaf_sim_file_t files[1];
    files[0].path = "data/hello.txt";
    files[0].data = content;
    files[0].size = sizeof(content); /* includes NUL, 18 bytes */
    qeaf_input_event_t input[2];
    memset(input, 0, sizeof(input));
    input[0].type = QEAF_INPUT_TOUCH; input[0].code = 0;
    input[0].x = 100; input[0].y = 200; input[0].pressed = 1;
    input[1].type = QEAF_INPUT_KEY; input[1].code = 28; input[1].pressed = 1;
    qeaf_sim_t *sim = open_sim("probe.qeapp", files, 1, input, 2, error, sizeof(error));
    CHECK(sim != NULL, "probe loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK((qeaf_sim_permissions(sim) & QEAF_PERM_STORAGE_READ) != 0, "storage.read granted");
    CHECK((qeaf_sim_permissions(sim) & QEAF_PERM_INPUT_RECEIVE) != 0, "input.receive granted");
    int rc = qeaf_sim_run(sim, error, sizeof(error));
    CHECK(rc == 0, "probe run returns 0");
    if(rc != 0) printf("  reason: %s\n", error);
    CHECK(qeaf_sim_outstanding_allocs(sim) == 0, "read buffer freed (no leak)");
    CHECK(log_has(sim, "native storage OK"), "file content logged");
    CHECK(qeaf_sim_input_delivered(sim) == 2, "two input events delivered");
    CHECK(log_has(sim, "input event"), "on_input ran");
    qeaf_sim_free(sim);
}

static void test_permission_denial(void)
{
    printf("== permission denial ==\n");
    char error[256];
    static const uint8_t content[] = "native storage OK";
    qeaf_sim_file_t files[1];
    files[0].path = "data/hello.txt";
    files[0].data = content;
    files[0].size = sizeof(content);
    qeaf_sim_t *sim = open_sim("probe-noperm.qeapp", files, 1, NULL, 0, error, sizeof(error));
    CHECK(sim != NULL, "probe-noperm loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK((qeaf_sim_permissions(sim) & QEAF_PERM_STORAGE_READ) == 0, "storage.read NOT granted");
    int rc = qeaf_sim_run(sim, error, sizeof(error));
    CHECK(rc == 0, "denied read does not crash the run");
    CHECK(log_has(sim, "read_file denied"), "read_file denied without permission");
    CHECK(qeaf_sim_outstanding_allocs(sim) == 0, "no allocation on denial");
    qeaf_sim_free(sim);
}

static void test_leak(void)
{
    printf("== leak detection ==\n");
    char error[256];
    static const uint8_t content[] = "native storage OK";
    qeaf_sim_file_t files[1];
    files[0].path = "data/hello.txt";
    files[0].data = content;
    files[0].size = sizeof(content);
    qeaf_sim_t *sim = open_sim("leak.qeapp", files, 1, NULL, 0, error, sizeof(error));
    CHECK(sim != NULL, "leak fixture loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    int rc = qeaf_sim_run(sim, error, sizeof(error));
    CHECK(rc != 0, "leaking app fails the run");
    CHECK(strstr(error, "leaked") != NULL, "error reports leaked allocation");
    CHECK(qeaf_sim_outstanding_allocs(sim) == 1, "one allocation outstanding");
    qeaf_sim_free(sim); /* host reclaims the leaked buffer on free */
}

static void expect_reject(const char *name, const char *needle, const char *label)
{
    char error[256];
    error[0] = '\0';
    qeaf_sim_t *sim = open_sim(name, NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(sim == NULL, label);
    if(sim != NULL) { qeaf_sim_free(sim); return; }
    CHECK(strstr(error, needle) != NULL, label);
    if(strstr(error, needle) == NULL) printf("  reason: %s\n", error);
}

static void test_rejections(void)
{
    printf("== structural rejections ==\n");
    expect_reject("stub.qeapp", "empty code", "empty-code stub rejected");
    expect_reject("badimport.qeapp", "unknown framework import", "unknown import rejected");
    expect_reject("missingexport.qeapp", "missing required exports", "missing export rejected");
    expect_reject("tampered.qeapp", "chu ky", "tampered signature rejected");
    expect_reject("arm64.qeapp", "x86_64 modules only", "arm64 module rejected on host");
}

static void test_framebuffer_pixels(void)
{
    printf("== framebuffer pixels ==\n");
    char error[256];
    qeaf_sim_t *sim = open_sim("hello.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(sim != NULL, "hello loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK(qeaf_sim_run(sim, error, sizeof(error)) == 0, "run ok");
    const uint32_t *fb = qeaf_sim_framebuffer(sim);
    uint32_t w = qeaf_sim_width(sim), h = qeaf_sim_height(sim);
    CHECK(fb != NULL && w == 720 && h == 1560, "framebuffer is 720x1560");
    size_t lit = 0;
    if(fb != NULL)
        for(size_t i = 0; i < (size_t)w * h; i++)
            if(fb[i] != 0u) lit++;
    CHECK(lit > 0, "text rasterized to nonzero pixels");
    CHECK(lit < (size_t)w * h, "framebuffer not uniformly filled");
    printf("  lit pixels: %zu\n", lit);
    qeaf_sim_free(sim);
}

/* Count nonzero pixels in the inclusive row band [y0,y1] across the canvas. */
static size_t band_lit(const qeaf_sim_t *sim, int y0, int y1)
{
    const uint32_t *fb = qeaf_sim_framebuffer(sim);
    uint32_t w = qeaf_sim_width(sim), h = qeaf_sim_height(sim);
    size_t lit = 0;
    if(fb == NULL) return 0;
    for(int y = y0; y <= y1 && y < (int)h; y++)
        for(uint32_t x = 0; x < w; x++)
            if(fb[(uint32_t)y * w + x] != 0u) lit++;
    return lit;
}

static void test_weight_v4(void)
{
    printf("== v4 text weight ==\n");
    char error[256];
    qeaf_sim_t *sim = open_sim("weight.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(sim != NULL, "v4 weight fixture loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    int rc = qeaf_sim_run(sim, error, sizeof(error));
    CHECK(rc == 0, "v4 weight run returns 0");
    if(rc != 0) printf("  reason: %s\n", error);
    CHECK(qeaf_sim_draw_count(sim) == 2, "two weighted draws recorded (invalid weight not drawn)");
    uint32_t w0 = 99u, w1 = 99u;
    qeaf_sim_draw_weight_at(sim, 0, &w0);
    qeaf_sim_draw_weight_at(sim, 1, &w1);
    CHECK(w0 == QEAF_TEXT_WEIGHT_REGULAR, "first draw weight REGULAR");
    CHECK(w1 == QEAF_TEXT_WEIGHT_BOLD, "second draw weight BOLD");
    CHECK(log_has(sim, "invalid weight rejected"), "unknown weight rejected by runtime");
    size_t regular = band_lit(sim, 120, 127);
    size_t bold = band_lit(sim, 400, 407);
    printf("  regular ink: %zu, bold ink: %zu\n", regular, bold);
    CHECK(regular > 0, "regular band rasterized");
    CHECK(bold > regular, "bold band has strictly more ink than regular");

    qeaf_sim_t *again = open_sim("weight.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(again != NULL, "v4 weight fixture reloads");
    if(again != NULL) {
        CHECK(qeaf_sim_run(again, error, sizeof(error)) == 0, "second weighted run ok");
        CHECK(qeaf_sim_framebuffer_hash(sim) == qeaf_sim_framebuffer_hash(again),
              "weighted raster deterministic across runs");
        CHECK(band_lit(again, 400, 407) == bold, "bold ink identical across runs");
        qeaf_sim_free(again);
    }
    qeaf_sim_free(sim);

    printf("== v4 weight permission denial ==\n");
    qeaf_sim_t *np = open_sim("weight-noperm.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(np != NULL, "v4 weight-noperm fixture loads");
    if(np != NULL) {
        CHECK(qeaf_sim_run(np, error, sizeof(error)) == 0, "denied weighted run does not crash");
        CHECK(qeaf_sim_draw_count(np) == 0, "no draws without ui.display");
        qeaf_sim_free(np);
    }
}

static int rect_is(const qeaf_sim_t *sim, size_t split, uint32_t rect,
                   int32_t x, int32_t y, int32_t w, int32_t h)
{
    qeaf_rect_t r;
    if(qeaf_sim_split_rect_at(sim, split, rect, &r) != 0) return 0;
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void test_layout_v5(void)
{
    printf("== v5 layout weight ==\n");
    char error[256];
    qeaf_sim_t *sim = open_sim("layout.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(sim != NULL, "v5 layout fixture loads");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    int rc = qeaf_sim_run(sim, error, sizeof(error));
    CHECK(rc == 0, "v5 layout run returns 0");
    if(rc != 0) printf("  reason: %s\n", error);
    CHECK(qeaf_sim_split_count(sim) == 8, "eight split calls recorded (2 valid + 6 invalid)");
    int rc0 = 99, rc1 = 99; uint32_t n0 = 0, n1 = 0;
    qeaf_sim_split_at(sim, 0, &rc0, &n0);
    qeaf_sim_split_at(sim, 1, &rc1, &n1);
    CHECK(rc0 == 0 && n0 == 3, "horizontal split accepted with n=3");
    CHECK(rc1 == 0 && n1 == 3, "vertical split accepted with n=3");
    /* 1:2:1 over 640px with two 16px gaps -> free 608 -> 152/304/152. */
    CHECK(rect_is(sim, 0, 0, 40, 100, 152, 80), "horizontal rect0 = (40,100,152,80)");
    CHECK(rect_is(sim, 0, 1, 208, 100, 304, 80), "horizontal rect1 = (208,100,304,80)");
    CHECK(rect_is(sim, 0, 2, 528, 100, 152, 80), "horizontal rect2 = (528,100,152,80)");
    /* 1:1:1 over 301px with two 10px gaps -> free 281 -> 94/94/93 (remainder leading). */
    CHECK(rect_is(sim, 1, 0, 40, 300, 200, 94), "vertical rect0 = (40,300,200,94)");
    CHECK(rect_is(sim, 1, 1, 40, 404, 200, 94), "vertical rect1 = (40,404,200,94)");
    CHECK(rect_is(sim, 1, 2, 40, 508, 200, 93), "vertical rect2 = (40,508,200,93)");
    int all_rejected = 1;
    for(size_t i = 2; i < 8; i++) {
        int rci = 0; uint32_t ni = 0;
        qeaf_sim_split_at(sim, i, &rci, &ni);
        if(rci != -1) all_rejected = 0;
    }
    CHECK(all_rejected, "all six invalid split probes rejected");
    CHECK(log_has(sim, "invalid splits rejected"), "runtime logged invalid-split rejection");
    CHECK(log_has(sim, "splits ok"), "runtime logged valid splits");
    CHECK(qeaf_sim_draw_count(sim) == 6, "six labels drawn at computed rect origins");
    CHECK(qeaf_sim_framebuffer_hash(sim) != 0u, "layout rasterized to nonzero pixels");

    qeaf_sim_t *again = open_sim("layout.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(again != NULL, "v5 layout fixture reloads");
    if(again != NULL) {
        CHECK(qeaf_sim_run(again, error, sizeof(error)) == 0, "second layout run ok");
        CHECK(qeaf_sim_framebuffer_hash(sim) == qeaf_sim_framebuffer_hash(again),
              "layout raster deterministic across runs");
        qeaf_sim_free(again);
    }
    qeaf_sim_free(sim);

    printf("== v5 layout weight is permission-free ==\n");
    qeaf_sim_t *np = open_sim("layout-noperm.qeapp", NULL, 0, NULL, 0, error, sizeof(error));
    CHECK(np != NULL, "v5 layout-noperm fixture loads");
    if(np != NULL) {
        CHECK(qeaf_sim_run(np, error, sizeof(error)) == 0, "permission-free layout run ok");
        CHECK(qeaf_sim_split_count(np) == 8, "splits still recorded without ui.display");
        int r0 = 99; uint32_t nn = 0;
        qeaf_sim_split_at(np, 0, &r0, &nn);
        CHECK(r0 == 0, "pure split succeeds without ui.display");
        CHECK(rect_is(np, 0, 1, 208, 100, 304, 80), "split rects identical without permission");
        CHECK(qeaf_sim_draw_count(np) == 0, "label draws denied without ui.display");
        qeaf_sim_free(np);
    }
}

static void test_violet_device_profile(void)
{
    printf("== Redmi Note 7 Pro device VM ==\n");
    const qeaf_device_profile_t *profile = qeaf_device_find("violet");
    CHECK(profile != NULL, "violet profile exists");
    if(profile == NULL) return;
    CHECK(strcmp(profile->name, "Redmi Note 7 Pro") == 0, "profile name");
    CHECK(profile->panel_width == 1080 && profile->panel_height == 2340,
          "physical panel is 1080x2340");
    CHECK(profile->canvas_width == 720 && profile->canvas_height == 1560,
          "app canvas is 720x1560 (1.5x fit)");
    CHECK(qeaf_device_find("redmi-note-7-pro") == profile, "device alias resolves");

    qeaf_input_event_t input[2];
    memset(input, 0, sizeof(input));
    input[0].type = QEAF_INPUT_TOUCH;
    input[0].code = 1;
    input[0].x = 540; /* raw panel centre */
    input[0].y = 1170;
    input[0].pressed = 1;
    input[1].type = QEAF_INPUT_KEY;
    input[1].code = 7;
    input[1].pressed = 1;

    char error[256];
    qeaf_sim_t *sim = open_device_sim("inputprobe.qeapp", "violet",
                                      input, 2, error, sizeof(error));
    CHECK(sim != NULL, "inputprobe loads in device mode");
    if(sim == NULL) { printf("  reason: %s\n", error); return; }
    CHECK(qeaf_sim_device(sim) == profile, "sim reports the violet profile");
    CHECK(qeaf_sim_width(sim) == 720 && qeaf_sim_height(sim) == 1560,
          "sim canvas follows the profile");
    CHECK(qeaf_sim_panel_width(sim) == 1080 && qeaf_sim_panel_height(sim) == 2340,
          "sim panel dimensions follow the profile");
    CHECK(qeaf_sim_run(sim, error, sizeof(error)) == 0, "device VM full run ok");
    CHECK(qeaf_sim_input_delivered(sim) == 2, "touch and key events delivered");
    CHECK(log_has(sim, "input x=360"), "raw panel x=540 maps to canvas x=360");
    CHECK(log_has(sim, "input y=780"), "raw panel y=1170 maps to canvas y=780");
    qeaf_sim_free(sim);
}

int main(int argc, char **argv)
{
    if(argc < 2) {
        printf("usage: %s fixtures-dir\n", argv[0]);
        return 2;
    }
    g_dir = argv[1];
    test_lifecycle_order();
    test_full_run_trace();
    test_determinism();
    test_storage_cleanup();
    test_permission_denial();
    test_leak();
    test_rejections();
    test_framebuffer_pixels();
    test_weight_v4();
    test_layout_v5();
    test_violet_device_profile();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    printf(g_failures ? "SIM TEST FAILURES\n" : "SIM TEST ALL OK\n");
    return g_failures ? 1 : 0;
}