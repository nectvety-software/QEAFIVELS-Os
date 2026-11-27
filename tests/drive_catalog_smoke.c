/* SPDX-License-Identifier: MIT */
/* tests/drive_catalog_smoke.c - the drives/ set in the QEAFIVELS driver core.
 *
 * The catalog is only trustworthy if a probe without verified platform
 * resources lands in DEFERRED (not FAILED, not READY), a verified one runs the
 * drive's own read-only transaction, and the public census agrees with the
 * core. Every assertion here is about one of those three.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "drive_catalog.h"
#include "mre_panel_nt36672a.h"
#include "qeaf_drives_api.h"
#include "vqeaf/driver.h"

static uint32_t regs[0x2400];

/* Device records live for the whole run: a deferred instance keeps its device
 * pointer, and the retry pass re-probes it. */
static VqeafDriveDevice deferred_dev, pending_dev, ready_dev, bad_parent_dev;
static VqeafDriveDevice panel_dev, retry_dev, bad_dev, bad_compat_dev;

static mre_status_t fake_read(void *context, uint32_t offset, uint32_t *value)
{
    (void)context;
    if (offset >= 0x2400u) return MRE_E_INVALID;
    *value = regs[offset];
    return MRE_OK;
}

static mre_status_t fake_write(void *context, uint32_t offset, uint32_t value)
{
    (void)context;
    if (offset >= 0x2400u) return MRE_E_INVALID;
    if (offset != 0x700u) regs[offset] = value;
    return MRE_OK;
}

static void fake_delay(void *context, uint32_t microseconds)
{
    (void)context;
    (void)microseconds;
}

static uint64_t fake_time(void *context)
{
    static uint64_t now;
    (void)context;
    return ++now;
}

static const mre_reg_io_ops_t fake_ops = {
    fake_read, fake_write, fake_delay, fake_time
};

static mre_reg_io_t fake_window(uint32_t size)
{
    mre_reg_io_t io;
    io.ops = &fake_ops;
    io.context = NULL;
    io.size = size;
    return io;
}

static mre_status_t parent_rate(void *context, mre_dispcc_parent_id_t parent,
                                uint64_t *rate_hz)
{
    (void)context;
    if (parent == MRE_DISPCC_PARENT_GPLL0) {
        *rate_hz = 600000000u;
        return MRE_OK;
    }
    return MRE_E_UNSUPPORTED;
}

/* ------------------------------------------------------------------- cases */

static void test_registration(void)
{
    uint32_t i;

    assert(vqeaf_drive_catalog_register_all() == 0);
    assert(vqeaf_drive_catalog_register_all() == 0);   /* idempotent */
    assert(vqeaf_drive_catalog_count() == 6u);
    assert(vqeaf_driver_count() == 6u);
    assert(vqeaf_driver_instance_count() == 0u);

    for (i = 0; i < vqeaf_drive_catalog_count(); i++) {
        const VqeafDriver *d = vqeaf_drive_catalog_at(i);
        assert(d != NULL);
        assert(d->abi == VQEAF_DRIVER_ABI);
        assert(d->size == sizeof(VqeafDriver));
        assert(d->probe != NULL);
        assert(d->match != NULL);
        assert(vqeaf_driver_find(d->name, &(uint32_t){0u}) == -2);
    }
    assert(vqeaf_drive_catalog_at(vqeaf_drive_catalog_count()) == NULL);
}

static void test_deferred_until_verified(void)
{
    VqeafDriveDevice *device = &deferred_dev;
    VqeafDriverInstance instance;
    uint32_t id = 0u;

    memset(device, 0, sizeof(*device));
    device->abi = VQEAF_DRIVE_CATALOG_ABI;
    device->size = sizeof(*device);
    device->compatible = "qcom,sm6150-dispcc";

    assert(vqeaf_drive_catalog_bind("qcom,sm6150-dispcc", NULL, &id) ==
           MRE_E_INVALID);
    assert(vqeaf_drive_catalog_bind("qcom,missing", device, &id) == -2);

    /* No platform: deferred, and the state must say so, not "failed". */
    assert(vqeaf_drive_catalog_bind("qcom,sm6150-dispcc", device, &id) ==
           VQEAF_DRIVER_DEFER);
    assert(vqeaf_driver_get(id, &instance) == 0);
    assert(instance.state == VQEAF_DRV_DEFERRED);
    assert(instance.last_error == VQEAF_DRIVER_DEFER);
    assert(instance.attempts == 1u);
    assert(strcmp(vqeaf_driver_state_name(instance.state), "deferred") == 0);

    /* Binding the same device twice is refused, not multiplied. */
    assert(vqeaf_drive_catalog_bind("qcom,sm6150-dispcc", device, &id) == -4);

    /* A bad record is an invalid argument, still not a hardware failure.
     * A fresh record is used: the deferred one above is already bound to this
     * driver, and a second bind of the same record is a duplicate (-4). */
    bad_dev = *device;
    bad_dev.abi = 99u;
    assert(vqeaf_drive_catalog_bind("qcom,sm6150-dispcc", &bad_dev, &id) ==
           -1);
    bad_compat_dev = *device;
    bad_compat_dev.compatible = "qcom,geni-i2c";
    assert(vqeaf_drive_catalog_bind("qcom,sm6150-dispcc", &bad_compat_dev,
                                    &id) == -1);
}

static void test_verified_probe(void)
{
    static mre_sm6150_dispcc_t dispcc;
    mre_sm6150_dispcc_policy_t policy = {1u, 1u, 0u};
    mre_reg_io_t window = fake_window(0x2200u);
    VqeafDriverInstance instance;
    uint32_t id = 0u;

    memset(regs, 0, sizeof(regs));
    assert(mre_sm6150_dispcc_init(&dispcc, &window, parent_rate, NULL,
                                  &policy) == MRE_OK);

    memset(&pending_dev, 0, sizeof(pending_dev));
    pending_dev.abi = VQEAF_DRIVE_CATALOG_ABI;
    pending_dev.size = sizeof(pending_dev);
    pending_dev.compatible = "qcom,sm6150-dispcc";
    pending_dev.platform = &dispcc;

    /* Resources claimed but not verified: still deferred. */
    assert(vqeaf_drive_catalog_bind("qcom,sm6150-dispcc", &pending_dev, &id) ==
           VQEAF_DRIVER_DEFER);

    /* With a verified window the probe performs the real clock read:
     * AHB_SRC from GPLL0, no divider -> 1.2 GHz. */
    regs[0x2174u] = 4u << 8;
    ready_dev = pending_dev;
    ready_dev.resources_verified = 1u;
    assert(vqeaf_drive_catalog_bind("qcom,sm6150-dispcc", &ready_dev, &id) == 0);
    assert(vqeaf_driver_get(id, &instance) == 0);
    assert(instance.state == VQEAF_DRV_READY);
    assert(instance.last_error == 0);

    /* An unknown parent select is a real failure, not a deferral. */
    regs[0x2174u] = 6u << 8;
    bad_parent_dev = ready_dev;
    assert(vqeaf_drive_catalog_bind("qcom,sm6150-dispcc", &bad_parent_dev,
                                    &id) == MRE_E_UNSUPPORTED);
    assert(vqeaf_driver_get(id, &instance) == 0);
    assert(instance.state == VQEAF_DRV_FAILED);
    assert(instance.last_error == MRE_E_UNSUPPORTED);
}

static void test_panel_never_ready(void)
{
    const mre_panel_descriptor_t *panel = mre_panel_nt36672a_shenchao();
    VqeafDriverInstance instance;
    uint32_t id = 0u;

    memset(&panel_dev, 0, sizeof(panel_dev));
    panel_dev.abi = VQEAF_DRIVE_CATALOG_ABI;
    panel_dev.size = sizeof(panel_dev);
    panel_dev.compatible = "novatek,nt36672a-shenchao";
    panel_dev.platform = (void *)panel;
    panel_dev.resources_verified = 1u;

    /* No measured panel ID exists, so the init table is never authorized. */
    assert(vqeaf_drive_catalog_bind("novatek,nt36672a-shenchao", &panel_dev,
                                    &id) == VQEAF_DRIVER_DEFER);
    assert(vqeaf_driver_get(id, &instance) == 0);
    assert(instance.state == VQEAF_DRV_DEFERRED);
}

static void test_retry_report_and_census(void)
{
    static mre_sm6150_dispcc_t dispcc;
    mre_sm6150_dispcc_policy_t policy = {1u, 1u, 0u};
    mre_reg_io_t window = fake_window(0x2200u);
    VqeafDriverInstance instance;
    const qeaf_drives_api_v1 *api = qeaf_drives_api();
    qeaf_drive_info_t info;
    char report[1024];
    int written;
    uint32_t id = 0u, i, live;

    memset(regs, 0, sizeof(regs));
    regs[0x2174u] = 4u << 8;
    assert(mre_sm6150_dispcc_init(&dispcc, &window, parent_rate, NULL,
                                  &policy) == MRE_OK);

    memset(&retry_dev, 0, sizeof(retry_dev));
    retry_dev.abi = VQEAF_DRIVE_CATALOG_ABI;
    retry_dev.size = sizeof(retry_dev);
    retry_dev.compatible = "qcom,sm6150-dispcc";
    retry_dev.platform = &dispcc;
    assert(vqeaf_drive_catalog_bind("qcom,sm6150-dispcc", &retry_dev, &id) ==
           VQEAF_DRIVER_DEFER);

    /* The platform finishes wiring: this deferred instance becomes READY.
     * The other deferred records still have no verified resources, so they
     * stay deferred - one ready instance out of the retry pass. */
    retry_dev.resources_verified = 1u;
    assert(vqeaf_drive_catalog_retry() == 1);
    assert(vqeaf_driver_get(id, &instance) == 0);
    assert(instance.state == VQEAF_DRV_READY);
    assert(instance.attempts == 2u);
    assert(vqeaf_drive_catalog_retry() == 0);

    written = vqeaf_drive_catalog_report(report, (int)sizeof(report));
    assert(written > 0);
    assert(strstr(report, "qcom,sm6150-dispcc") != NULL);
    assert(strstr(report, "state=ready") != NULL);
    assert(strstr(report, "qcom,spmi-pmic-arb") != NULL);   /* registered, unbound */
    assert(strstr(report, "state=registered") != NULL);
    assert(vqeaf_drive_catalog_report(NULL, 16) == -1);
    assert(vqeaf_drive_catalog_report(report, 0) == -1);

    assert(api != NULL);
    assert(api->size == sizeof(qeaf_drives_api_v1));
    assert(api->abi_version == VQEAF_DRIVES_API_VERSION);
    live = api->count();
    assert(live >= 1u);
    for (i = 0; i < live; i++) {
        assert(api->info(i, &info) == 0);
        assert(info.name[0] != '\0');
        assert(info.state[0] != '\0');
        assert(info.attempts >= 1u);
    }
    assert(api->info(live, &info) == -1);
    assert(api->info(0u, NULL) == -1);
    assert(api->report(report, (uint32_t)sizeof(report)) == written);

    /* An unbind leaves history in the core but not in the census. */
    assert(vqeaf_driver_unbind(id) == 0);
    assert(api->count() == live - 1u);
    if (live > 1u) assert(api->info(0u, &info) == 0);
    assert(api->info(api->count(), &info) == -1);
}

int main(void)
{
    vqeaf_driver_reset();
    vqeaf_drive_catalog_reset_for_test();
    test_registration();
    test_deferred_until_verified();
    test_verified_probe();
    test_panel_never_ready();
    test_retry_report_and_census();
    puts("drive catalog / SDK API tests: PASS");
    return 0;
}
