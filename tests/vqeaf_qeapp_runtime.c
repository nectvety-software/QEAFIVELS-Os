/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_qeapp_runtime.c - execute the real QEAPP install path on host.
 *
 * vqeaf_qeapp.c is compiled with __linux__ forced so the SHA-256/HMAC, ZIP and
 * manifest/icon parsing code runs (the same code the violet image builds).
 * vqeaf_ui_read_file is stubbed to read a real .qeapp from disk. The driver
 * (scripts/test_qeapp_runtime.py) builds a signed package and a tampered one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf_qeapp.h"

static const char *g_path;
static int g_failures;

int vqeaf_ui_read_file(const char *name, uint8_t **out, size_t *size)
{
    (void)name;
    FILE *file = fopen(g_path, "rb");
    long length;
    uint8_t *buffer;

    if(file == NULL) return -1;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if(length <= 0) { fclose(file); return -1; }
    buffer = malloc((size_t)length);
    if(buffer == NULL) { fclose(file); return -1; }
    if(fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(buffer);
        return -1;
    }
    fclose(file);
    *out = buffer;
    *size = (size_t)length;
    return 0;
}

#define CHECK(cond, msg) do { \
    if(cond) { printf("ok   %s\n", msg); } \
    else { printf("FAIL %s\n", msg); g_failures++; } \
} while(0)

int main(int argc, char **argv)
{
    char error[256];
    int rc;

    if(argc < 3) {
        printf("usage: %s package.qeapp ok|tampered|limit [expected-output]\n", argv[0]);
        return 2;
    }
    g_path = argv[1];
    error[0] = '\0';

    if(strcmp(argv[2], "native") == 0 || strcmp(argv[2], "native-bad") == 0) {
        uint8_t *buffer = NULL;
        size_t size = 0;
        vqeaf_qeapp_native_info_t info;
        const uint8_t *module = NULL;
        size_t module_size = 0;

        if(vqeaf_ui_read_file(g_path, &buffer, &size) != 0 || buffer == NULL) {
            printf("FAIL cannot read %s\n", g_path);
            return 1;
        }
        rc = vqeaf_qeapp_inspect_native(buffer, size, &info, &module,
                                        &module_size, error, sizeof(error));
        if(strcmp(argv[2], "native-bad") == 0) {
            CHECK(rc != 0, "invalid native package is rejected");
            printf("  reason: %s\n", error);
        } else {
            CHECK(rc == 0, "signed native package is inspected");
            if(rc != 0) printf("  reason: %s\n", error);
            CHECK(rc == 0 && info.format_version == 2, "format_version 2");
            CHECK(rc == 0 && info.abi_version == 5, "abi_version 5");
            CHECK(rc == 0 && strcmp(info.arch, "arm64") == 0, "architecture arm64");
            CHECK(rc == 0 && strcmp(info.entry, "native/arm64/app.qen") == 0,
                  "entry module path");
            CHECK(rc == 0 && module_size == 64 && module != NULL,
                  "module bytes returned");
        }
        free(buffer);
        printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
        return g_failures ? 1 : 0;
    }

    if(strcmp(argv[2], "preview") == 0 ||
       strcmp(argv[2], "preview-tampered") == 0) {
        vqeaf_qeapp_preview_t meta;
        int before = vqeaf_qeapp_count();

        rc = vqeaf_qeapp_preview("hello.qeapp", &meta, error, sizeof(error));
        if(strcmp(argv[2], "preview-tampered") == 0) {
            CHECK(rc != 0, "tampered package preview is rejected");
            CHECK(vqeaf_qeapp_count() == before,
                  "rejected preview installs nothing");
            printf("  reason: %s\n", error);
            printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
            return g_failures ? 1 : 0;
        }
        CHECK(rc == 0, "signed package preview parses");
        if(rc != 0) {
            printf("  reason: %s\n", error);
            printf("%d FAILURES\n", g_failures);
            return 1;
        }
        CHECK(vqeaf_qeapp_count() == before, "preview does not install");
        CHECK(strcmp(meta.id, "org.vqeaf.hello") == 0, "preview id parsed");
        CHECK(strcmp(meta.name, "Hello VQEAF") == 0, "preview name parsed");
        CHECK(strcmp(meta.version, "1.0.0") == 0, "preview version parsed");
        CHECK(strstr(meta.message, "Hello from QEAPP") != NULL,
              "preview print line parsed");
        CHECK(meta.has_icon == 1, "preview icon present");
        CHECK(meta.native == 0, "preview declarative runtime");

        /* The commit path must agree with what the confirmation showed. */
        rc = vqeaf_qeapp_install("hello.qeapp", error, sizeof(error));
        CHECK(rc == 0, "install after preview succeeds");
        CHECK(vqeaf_qeapp_count() == before + 1, "install commits one app");
        const vqeaf_qeapp_t *app = vqeaf_qeapp_at(before);
        CHECK(app != NULL && strcmp(app->id, meta.id) == 0,
              "installed id matches preview");
        CHECK(app != NULL && strcmp(app->name, meta.name) == 0,
              "installed name matches preview");
        CHECK(app != NULL && app->permissions == meta.permissions,
              "installed permissions match preview");
        printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
        return g_failures ? 1 : 0;
    }

    if(strcmp(argv[2], "native-install") == 0) {
        vqeaf_qeapp_preview_t meta;
        int before = vqeaf_qeapp_count();

        rc = vqeaf_qeapp_preview("native.qeapp", &meta, error, sizeof(error));
        CHECK(rc == 0, "signed native package preview parses");
        if(rc != 0) {
            printf("  reason: %s\n", error);
            printf("%d FAILURES\n", g_failures);
            return 1;
        }
        CHECK(meta.native == 1, "preview reports native runtime");
        CHECK(meta.abi_version == 5, "preview reports manifest abi_version 5");
        rc = vqeaf_qeapp_install("native.qeapp", error, sizeof(error));
        CHECK(rc == 0, "native install commits");
        const vqeaf_qeapp_t *app = vqeaf_qeapp_at(before);
        CHECK(app != NULL && app->native == 1, "installed record is native");
        /* The app page prints this field, so it must be the package's ABI and
         * not a build-wide constant. */
        CHECK(app != NULL && app->abi_version == 5,
              "installed record keeps abi_version 5");
        printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
        return g_failures ? 1 : 0;
    }

    rc = vqeaf_qeapp_install("hello.qeapp", error, sizeof(error));

    if(strcmp(argv[2], "tampered") == 0) {
        CHECK(rc != 0, "tampered package is rejected");
        printf("  reason: %s\n", error);
        printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
        return g_failures ? 1 : 0;
    }

    CHECK(rc == 0, "signed QEAPP installs");
    if(rc != 0) {
        printf("  reason: %s\n", error);
        printf("%d FAILURES\n", g_failures);
        return 1;
    }
    CHECK(vqeaf_qeapp_count() == 1, "one app installed");
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(0);
    if(strcmp(argv[2], "limit") == 0) {
        CHECK(app != NULL && strlen(app->message) == 510,
              "long print output is bounded");
        CHECK(app != NULL && strspn(app->message, "A") == 510,
              "full output buffer stops before the next print");
        printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
        return g_failures ? 1 : 0;
    }
    CHECK(app != NULL && strcmp(app->id, "org.vqeaf.hello") == 0, "id parsed");
    CHECK(app != NULL && strcmp(app->name, "Hello VQEAF") == 0, "name parsed");
    CHECK(app != NULL && strcmp(app->version, "1.0.0") == 0, "version parsed");
    CHECK(app != NULL && strstr(app->message, "Hello from QEAPP") != NULL,
          "print line parsed");
    if(argc > 3)
        CHECK(app != NULL && strstr(app->message, argv[3]) != NULL,
              "linked library output parsed");
    CHECK(app != NULL && app->has_icon == 1, "embedded icon present");
    const lv_image_dsc_t *dsc = vqeaf_qeapp_icon(0);
    CHECK(dsc != NULL, "icon descriptor returned");
    CHECK(dsc != NULL && dsc->header.w == 64 && dsc->header.h == 64,
          "icon is 64x64");
    CHECK(dsc != NULL && dsc->header.cf == LV_COLOR_FORMAT_ARGB8888,
          "icon is ARGB8888");
    CHECK(dsc != NULL && dsc->header.stride == 64 * 4, "icon stride 256");
    CHECK(dsc != NULL && app != NULL && dsc->data == app->icon,
          "icon data points at slot buffer");
    CHECK(vqeaf_qeapp_icon(99) == NULL, "out-of-range icon is NULL");

    printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
    return g_failures ? 1 : 0;
}