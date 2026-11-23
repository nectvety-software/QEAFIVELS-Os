#include "mre_driver.h"
#include "mre_device_manager.h"
#include "mre_firmware.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int probe_calls;
typedef struct test_firmware { const char *sha256; uint32_t size; } test_firmware_t;

static mre_status_t test_probe(const mre_device_t *device)
{
    assert(device != NULL);
    ++probe_calls;
    return MRE_OK;
}

static mre_status_t test_verify(const char *path, const char *expected_sha256,
    uint32_t minimum_size, uint32_t maximum_size, void *context)
{
    const test_firmware_t *actual = (const test_firmware_t *)context;
    if (path == NULL || expected_sha256 == NULL || actual == NULL) return MRE_E_INVALID;
    if (actual->size < minimum_size || actual->size > maximum_size) return MRE_E_INVALID;
    return strcmp(expected_sha256, actual->sha256) == 0 ? MRE_OK : MRE_E_CHECKSUM;
}

int main(void)
{
    static const mre_driver_t driver = {"test-uart", "mre,test-uart", test_probe, NULL};
    static const mre_device_t device = {
        "uart0", "mre,test-uart", MRE_BUS_MMIO, 0x10000000u, 0x1000u, 5u, NULL
    };
    static const mre_device_t unknown = {
        "unknown0", "mre,unknown", MRE_BUS_PLATFORM, 0u, 0u, 0u, NULL
    };
    const mre_firmware_policy_t policy = {
        "test-radio", "1.2.3",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "LicenseRef-Test-Redistributable", 1u, 4096u, 1
    };
    mre_firmware_policy_t restricted_policy = policy;
    mre_firmware_policy_t invalid_size_policy = policy;
    const test_firmware_t valid_fw = {
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", 1024u
    };
    const test_firmware_t bad_hash = {"bad", 1024u};
    const test_firmware_t bad_size = {
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", 8192u
    };

    mre_device_reset_for_test();
    assert(mre_device_register(NULL) == MRE_E_INVALID);
    assert(mre_device_register(&device) == MRE_OK);
    assert(mre_device_register(&device) == MRE_E_EXISTS);
    assert(mre_device_count() == 1u);
    assert(mre_device_find("uart0") == &device);
    assert(mre_device_find("missing") == NULL);
    assert(mre_device_find_compatible("mre,test-uart", 0u) == &device);
    assert(mre_device_find_compatible("mre,test-uart", 1u) == NULL);

    mre_driver_reset_for_test();
    assert(mre_driver_register(NULL) == MRE_E_INVALID);
    assert(mre_driver_register(&driver) == MRE_OK);
    assert(mre_driver_count() == 1u);
    assert(mre_driver_register(&driver) == MRE_E_EXISTS);
    assert(mre_driver_bind(&device) == MRE_OK);
    assert(probe_calls == 1);
    assert(mre_driver_bind(&unknown) == MRE_E_NOT_FOUND);
    assert(mre_driver_bind(NULL) == MRE_E_INVALID);

    assert(mre_firmware_validate_policy(&policy, "1.2.3", "radio.bin",
        test_verify, (void *)&valid_fw) == MRE_OK);
    assert(mre_firmware_validate_policy(&policy, "9.9.9", "radio.bin",
        test_verify, (void *)&valid_fw) == MRE_E_VERSION);
    assert(mre_firmware_validate_policy(&policy, "1.2.3", "radio.bin",
        test_verify, (void *)&bad_hash) == MRE_E_CHECKSUM);
    assert(mre_firmware_validate_policy(&policy, "1.2.3", "radio.bin",
        test_verify, (void *)&bad_size) == MRE_E_INVALID);
    restricted_policy.redistribution_allowed = 0;
    assert(mre_firmware_validate_policy(&restricted_policy, "1.2.3", "radio.bin",
        test_verify, (void *)&valid_fw) == MRE_E_PERMISSION);
    invalid_size_policy.minimum_size = 8192u;
    invalid_size_policy.maximum_size = 1024u;
    assert(mre_firmware_validate_policy(&invalid_size_policy, "1.2.3", "radio.bin",
        test_verify, (void *)&valid_fw) == MRE_E_INVALID);

    puts("MRE host core tests: PASS");
    return 0;
}
