/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_qeapp_runtime.c - execute the real QEAPP install path on host.
 *
 * vqeaf_qeapp.c is compiled with __linux__ forced so the SHA-256/HMAC, ZIP and
 * manifest/icon parsing code runs (the same code the violet image builds).
 * vqeaf_ui_read_file is stubbed to read a real .qeapp from disk. The driver
 * (scripts/test_qeapp_runtime.py) builds a signed package and a tampered one.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <vqeaf/qeapp_package.h>

#include "vqeaf_qeapp.h"

#ifdef QEAPP_REAL_APPSTORE
#include "appstore.h"
#include "channel/fs/fat16.h"
#include "violet.h"
#endif

static const char *g_path;
static int g_failures;

static int read_all(const char *path, uint8_t **out, size_t *size)
{
    FILE *file = fopen(path, "rb");
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

int vqeaf_ui_read_file(const char *name, uint8_t **out, size_t *size)
{
    (void)name;
    return read_all(g_path, out, size);
}

#ifdef QEAPP_REAL_APPSTORE
int fat16_file_size(struct fat16_volume *vol, const char *name)
{
    FILE *file;
    long size;

    (void)vol;
    (void)name;
    file = fopen(g_path, "rb");
    if(file == NULL) return -1;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fclose(file);
    return size > 0 && size <= INT32_MAX ? (int)size : -1;
}

int fat16_read_path(struct fat16_volume *vol, const char *name,
                    uint8_t *out, size_t cap)
{
    FILE *file;
    long size;
    size_t got;

    (void)vol;
    (void)name;
    file = fopen(g_path, "rb");
    if(file == NULL) return -1;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if(size <= 0 || (size_t)size > cap) { fclose(file); return -1; }
    got = fread(out, 1, (size_t)size, file);
    fclose(file);
    return got == (size_t)size ? (int)got : -1;
}

void vio_log(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void umos_logf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
#endif

#define CHECK(cond, msg) do { \
    if(cond) { printf("ok   %s\n", msg); } \
    else { printf("FAIL %s\n", msg); g_failures++; } \
} while(0)

static void run_api_tests(int tampered)
{
    vqeaf_qeapp_package_info_t info;
    vqeaf_qeapp_package_result_t result;
    const char *package_path = g_path;
    int rc;

    CHECK(vqeaf_qeapp_package_api_version() ==
          VQEAF_QEAPP_PACKAGE_API_VERSION, "public API version is 1");
    CHECK(strcmp(vqeaf_qeapp_package_status_name(
                     VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY),
                 "OK_SESSION_ONLY") == 0, "session-only status has a name");
    CHECK(strcmp(vqeaf_qeapp_package_status_name(-1), "UNKNOWN") == 0,
          "unknown status has a name");

    memset(&info, 0, sizeof(info));
    memset(&result, 0, sizeof(result));
    rc = vqeaf_qeapp_package_preview(NULL, &info, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT &&
          result.status == rc, "preview rejects a NULL path");
    rc = vqeaf_qeapp_package_install(NULL, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT,
          "install rejects a NULL path");
    rc = vqeaf_qeapp_package_uninstall(NULL, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT,
          "uninstall rejects a NULL id");

    g_path = "missing.qeapp";
    rc = vqeaf_qeapp_package_preview(g_path, &info, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_IO_ERROR,
          "preview reports an unreadable package");
    rc = vqeaf_qeapp_package_install(g_path, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_IO_ERROR,
          "install reports an unreadable package");
    g_path = package_path;

    if(tampered) {
        rc = vqeaf_qeapp_package_preview(g_path, &info, &result);
        CHECK(rc == VQEAF_QEAPP_PACKAGE_SIGNATURE_ERROR,
              "public preview preserves signature errors");
        rc = vqeaf_qeapp_package_install(g_path, &result);
        CHECK(rc == VQEAF_QEAPP_PACKAGE_SIGNATURE_ERROR,
              "public install preserves signature errors");
        CHECK(vqeaf_qeapp_package_count() == 0,
              "rejected public install changes no registry state");
        return;
    }

    rc = vqeaf_qeapp_package_preview(g_path, &info, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_OK && result.status == rc,
          "public preview verifies a signed package");
    CHECK(info.struct_size == sizeof(info) &&
          info.api_version == VQEAF_QEAPP_PACKAGE_API_VERSION,
          "preview returns versioned info");
    CHECK(strcmp(info.id, "org.vqeaf.hello") == 0 && info.has_icon == 1,
          "public preview returns identity and icon");
    CHECK(vqeaf_qeapp_package_count() == 0,
          "public preview does not mutate the registry");

    rc = vqeaf_qeapp_package_install(g_path, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY &&
          result.status == rc && result.persisted == 0 && result.updated == 0,
          "install reports RAM-only success when p77 persistence fails");
    CHECK(result.api_version == VQEAF_QEAPP_PACKAGE_API_VERSION &&
          strstr(result.message, "persistence failed") != NULL,
          "session-only result explains missing persistence");
    CHECK(vqeaf_qeapp_package_count() == 1, "public install commits one app");
    CHECK(vqeaf_qeapp_package_find("org.vqeaf.hello") == 0,
          "find returns a zero-based installed index");
    CHECK(vqeaf_qeapp_package_find("org.example.absent") == -1,
          "find returns -1 for an absent id");
    {
        uint8_t *retained = NULL;
        size_t retained_size = 0;
        g_path = "missing-after-install.qeapp";
        CHECK(vqeaf_qeapp_package(0, &retained, &retained_size) == 0 &&
              retained != NULL && retained_size > 0,
              "session-only install launches retained verified bytes");
        free(retained);
        g_path = package_path;
    }

    rc = vqeaf_qeapp_package_get(0, &info, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_OK && result.persisted == -1,
          "get reads an installed package");
    CHECK(strcmp(info.id, "org.vqeaf.hello") == 0 &&
          info.granted_permissions == 0,
          "installed identity is stable and grants start empty");
    rc = vqeaf_qeapp_package_get(99, &info, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_NOT_FOUND,
          "get reports an out-of-range index");

    rc = vqeaf_qeapp_package_install(g_path, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY && result.updated == 1,
          "reinstall reports update by stable package id");
    CHECK(vqeaf_qeapp_package_count() == 1,
          "update does not add a duplicate registry entry");

    rc = vqeaf_qeapp_package_uninstall("org.example.absent", &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_NOT_FOUND,
          "uninstall reports an absent package");
    rc = vqeaf_qeapp_package_uninstall("org.vqeaf.hello", &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY &&
          result.persisted == 0, "uninstall reports RAM-only success");
    CHECK(vqeaf_qeapp_package_count() == 0 &&
          vqeaf_qeapp_package_find("org.vqeaf.hello") == -1,
          "uninstall removes the package");
}

static void run_verified_tests(const char *alternate)
{
    vqeaf_qeapp_package_info_t info;
    vqeaf_qeapp_package_result_t result;
    const char *original = g_path;
    int nonzero = 0;
    int rc;

    rc = vqeaf_qeapp_package_preview(original, &info, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_OK, "verified flow preview succeeds");
    for(size_t i = 0; i < sizeof(info.package_hash); i++)
        nonzero |= info.package_hash[i] != 0;
    CHECK(nonzero, "preview returns a nonzero package digest");

    g_path = alternate;
    rc = vqeaf_qeapp_package_install_verified(original, info.package_hash,
                                               &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE,
          "verified install rejects a valid substituted package");
    CHECK(vqeaf_qeapp_package_count() == 0,
          "substituted verified install changes no registry state");

    g_path = original;
    rc = vqeaf_qeapp_package_install_verified(original, info.package_hash,
                                               &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY && result.updated == 0,
          "verified install accepts the exact previewed bytes");
    CHECK(vqeaf_qeapp_package_count() == 1,
          "verified install commits one app");
    rc = vqeaf_qeapp_package_install_verified(original, NULL, &result);
    CHECK(rc == VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT,
          "verified install requires a digest");
}

#ifdef QEAPP_REAL_APPSTORE
static void check_stored_bytes(const char *key, const uint8_t *expected,
                               size_t expected_size)
{
    uint8_t *actual = NULL;
    size_t actual_size = 0;
    int ok = appstore_read(key, &actual, &actual_size) == 0 &&
             actual_size == expected_size &&
             memcmp(actual, expected, expected_size) == 0;

    CHECK(ok, "catalog returns the exact verified package bytes");
    free(actual);
}

static void run_durable_write(const char *good, const char *alternate)
{
    static const uint8_t legacy_data[] = "legacy-data";
    static const uint8_t alternate_data[] = "alternate-data";
    uint8_t *good_bytes = NULL, *alternate_bytes = NULL;
    size_t good_size = 0, alternate_size = 0;
    char names[QEAPP_MAX][APPSTORE_NAME];
    appstore_entry_t entries[2] = { 0 };
    appstore_entry_t duplicate[2] = { 0 };
    int count;

    appstore_set_volume((struct fat16_volume *)(uintptr_t)1, NULL, NULL);
    CHECK(read_all(good, &good_bytes, &good_size) == 0 &&
          read_all(alternate, &alternate_bytes, &alternate_size) == 0,
          "durable fixtures are readable");

    g_path = good;
    CHECK(vqeaf_qeapp_package_install("/Install/Apps/shared.qeapp", NULL) == 0,
          "first same-basename package installs");
    g_path = alternate;
    CHECK(vqeaf_qeapp_package_install("/Install/Apps/shared.qeapp", NULL) == 0,
          "second same-basename package installs");
    CHECK(vqeaf_qeapp_package_count() == 2,
          "same basename with distinct ids creates two records");
    CHECK(vqeaf_qeapp_persisted() == 1,
          "real appstore save reports verified persistence");

    g_path = "missing-after-install.qeapp";
    CHECK(vqeaf_qeapp_data_write(0, "one", 3) == 0 &&
          vqeaf_qeapp_data_write(1, "two", 3) == 0 &&
          vqeaf_qeapp_persisted() == 1,
          "later saves do not reread mutable package paths");
    check_stored_bytes("org.vqeaf.hello", good_bytes, good_size);
    check_stored_bytes("org.vqeaf.alternate", alternate_bytes,
                       alternate_size);
    count = appstore_restore(names, QEAPP_MAX);
    CHECK(count == 2 && strcmp(names[0], "org.vqeaf.hello") == 0 &&
          strcmp(names[1], "org.vqeaf.alternate") == 0,
          "new catalog keys are stable manifest ids");

    snprintf(duplicate[0].name, sizeof(duplicate[0].name), "%s",
             "org.vqeaf.hello");
    snprintf(duplicate[1].name, sizeof(duplicate[1].name), "%s",
             "org.vqeaf.hello");
    duplicate[0].data = good_bytes;
    duplicate[0].data_size = good_size;
    duplicate[1].data = alternate_bytes;
    duplicate[1].data_size = alternate_size;
    CHECK(appstore_save(duplicate, 2) == -1,
          "duplicate catalog keys are rejected before writing");
    count = appstore_restore(names, QEAPP_MAX);
    CHECK(count == 2, "rejected duplicate save preserves the live catalog");

    snprintf(entries[0].name, sizeof(entries[0].name), "%s", "shared.qeapp");
    entries[0].data = good_bytes;
    entries[0].data_size = good_size;
    entries[0].app_data = legacy_data;
    entries[0].app_data_size = sizeof(legacy_data);
    entries[0].granted_permissions = 1u;
    snprintf(entries[1].name, sizeof(entries[1].name), "%s",
             "org.vqeaf.alternate");
    entries[1].data = alternate_bytes;
    entries[1].data_size = alternate_size;
    entries[1].app_data = alternate_data;
    entries[1].app_data_size = sizeof(alternate_data);
    entries[1].granted_permissions = 1u;
    CHECK(appstore_save(entries, 2) == 0,
          "mixed id-keyed and legacy catalog fixture is written");
    free(good_bytes);
    free(alternate_bytes);
}

static void run_durable_restore(const char *good, const char *alternate)
{
    uint8_t *good_bytes = NULL, *alternate_bytes = NULL;
    size_t good_size = 0, alternate_size = 0;
    char names[QEAPP_MAX][APPSTORE_NAME];
    const uint8_t *data;
    size_t data_size;
    int count;
    int legacy_index, alternate_index;

    CHECK(read_all(good, &good_bytes, &good_size) == 0 &&
          read_all(alternate, &alternate_bytes, &alternate_size) == 0,
          "restore fixtures are readable");
    count = appstore_restore(names, QEAPP_MAX);
    CHECK(count == 2 && strcmp(names[0], "shared.qeapp") == 0 &&
          strcmp(names[1], "org.vqeaf.alternate") == 0,
          "fresh process reads the mixed legacy catalog");
    appstore_set_restoring(1);
    for(int i = 0; i < count; i++)
        CHECK(vqeaf_qeapp_install_stored(names[i], NULL, 0) == 0,
              "boot restore revalidates each signed package");
    appstore_set_restoring(0);
    CHECK(vqeaf_qeapp_package_count() == 2,
          "fresh process restores both package identities");
    legacy_index = vqeaf_qeapp_package_find("org.vqeaf.hello");
    alternate_index = vqeaf_qeapp_package_find("org.vqeaf.alternate");
    CHECK(legacy_index >= 0 && alternate_index >= 0,
          "legacy and id-keyed records resolve by manifest id");
    CHECK(vqeaf_qeapp_data_read(legacy_index, &data, &data_size) == 0 &&
          data_size == sizeof("legacy-data") &&
          memcmp(data, "legacy-data", sizeof("legacy-data")) == 0,
          "validated legacy fallback preserves private data");
    CHECK(vqeaf_qeapp_at(legacy_index)->granted_permissions == 1u,
          "validated legacy fallback preserves user grants");
    check_stored_bytes("org.vqeaf.alternate", alternate_bytes,
                       alternate_size);

    {
        uint8_t *package = NULL;
        size_t package_size = 0;
        int ok = vqeaf_qeapp_package(legacy_index, &package,
                                     &package_size) == 0 &&
                 package_size == good_size &&
                 memcmp(package, good_bytes, good_size) == 0;
        CHECK(ok, "native launch lookup resolves a legacy package safely");
        free(package);
    }

    CHECK(vqeaf_qeapp_data_write(legacy_index, "migrated", 8) == 0 &&
          vqeaf_qeapp_persisted() == 1,
          "post-restore save migrates the legacy record");
    count = appstore_restore(names, QEAPP_MAX);
    CHECK(count == 2 && strcmp(names[0], "org.vqeaf.hello") == 0 &&
          strcmp(names[1], "org.vqeaf.alternate") == 0,
          "legacy filename key is replaced by the stable id");
    check_stored_bytes("org.vqeaf.hello", good_bytes, good_size);
    free(good_bytes);
    free(alternate_bytes);
}
#endif

int main(int argc, char **argv)
{
    char error[256];
    int rc;

    if(argc < 3) {
        printf("usage: %s package.qeapp ok|tampered|limit|api-verified|icon-slots "
               "[alternate.qeapp]\n", argv[0]);
        return 2;
    }
    g_path = argv[1];
    error[0] = '\0';

#ifdef QEAPP_REAL_APPSTORE
    if(strcmp(argv[2], "durable-write") == 0 ||
       strcmp(argv[2], "durable-restore") == 0) {
        if(argc != 5) return 2;
        if(strcmp(argv[2], "durable-write") == 0)
            run_durable_write(argv[3], argv[4]);
        else
            run_durable_restore(argv[3], argv[4]);
        printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
        return g_failures ? 1 : 0;
    }
#endif

    if(strcmp(argv[2], "api-verified") == 0) {
        if(argc != 4) return 2;
        run_verified_tests(argv[3]);
        printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
        return g_failures ? 1 : 0;
    }

    if(strcmp(argv[2], "icon-slots") == 0) {
        const size_t edge = (32u * 64u + 32u) * 4u;
        vqeaf_qeapp_preview_t preview;
        const lv_image_dsc_t *first_slot;
        const vqeaf_qeapp_t *app;

        if(argc != 4) return 2;
        CHECK(vqeaf_qeapp_preview("legacy.qeapp", &preview, error,
                                  sizeof(error)) == 0,
              "legacy icon package preview succeeds");
        CHECK(preview.has_icon && preview.icon[edge + 3] == 128 &&
              preview.icon[edge] == 0 && preview.icon[edge + 1] == 0 &&
              preview.icon[edge + 2] == 0,
              "preview keeps exact legacy alpha and black RGB");
        CHECK(vqeaf_qeapp_install("legacy.qeapp", error, sizeof(error)) == 0,
              "legacy icon package installs");
        app = vqeaf_qeapp_at(0);
        CHECK(app != NULL && app->icon[edge + 3] == 128 &&
              app->icon[edge] == 0 && app->icon[edge + 1] == 0 &&
              app->icon[edge + 2] == 0,
              "installed record keeps exact legacy icon bytes");
        CHECK(app != NULL && app->icon_render[edge + 3] == 128 &&
              (app->icon_render[edge] != 0 || app->icon_render[edge + 1] != 0 ||
               app->icon_render[edge + 2] != 0),
              "display buffer removes black from the translucent edge");
        first_slot = vqeaf_qeapp_icon(0);
        CHECK(first_slot != NULL && app != NULL &&
              first_slot->data == app->icon_render &&
              first_slot->data != app->icon,
              "descriptor renders the separate display buffer");

        g_path = argv[3];
        CHECK(vqeaf_qeapp_install("alternate.qeapp", error, sizeof(error)) == 0,
              "second icon package installs");
        CHECK(vqeaf_qeapp_count() == 2 && vqeaf_qeapp_icon(0) !=
              vqeaf_qeapp_icon(1), "each slot has a stable distinct descriptor");
        CHECK(vqeaf_qeapp_uninstall(0) == 0 && vqeaf_qeapp_count() == 1,
              "uninstall shifts the remaining app record");
        app = vqeaf_qeapp_at(0);
        CHECK(app != NULL && strcmp(app->id, "org.vqeaf.alternate") == 0,
              "remaining app keeps its identity after the slot shift");
        CHECK(first_slot != NULL && vqeaf_qeapp_icon(0) == first_slot &&
              app != NULL && first_slot->data == app->icon_render,
              "slot shift refreshes the stable descriptor data pointer");
        printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
        return g_failures ? 1 : 0;
    }

    if(strcmp(argv[2], "api") == 0 ||
       strcmp(argv[2], "api-tampered") == 0) {
        run_api_tests(strcmp(argv[2], "api-tampered") == 0);
        printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
        return g_failures ? 1 : 0;
    }

    if(strcmp(argv[2], "native") == 0 ||
       (strncmp(argv[2], "native-", 7) == 0 &&
        strcmp(argv[2], "native-install") != 0)) {
        int reject = strcmp(argv[2], "native") != 0;
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
        if(reject) {
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
    CHECK(dsc != NULL && app != NULL && dsc->data == app->icon_render &&
          dsc->data != app->icon,
          "icon data points at the separate display buffer");
    CHECK(vqeaf_qeapp_icon(99) == NULL, "out-of-range icon is NULL");

    printf(g_failures ? "%d FAILURES\n" : "ALL OK\n", g_failures);
    return g_failures ? 1 : 0;
}