/* SPDX-License-Identifier: MIT */
/* tests/driver_core_smoke.c - host gate for the QEAFIVELS driver core.
 *
 * Covers the parts the upgrade introduced, all of them reachable without a
 * device: descriptor validation, duplicate binding, the deferral path, remove
 * only after a successful probe, match-based selection and the bounded report.
 */
#include <assert.h>
#include <string.h>

#include "../system/core/include/vqeaf/driver.h"

static int probe_calls, remove_calls, defer_calls, claim;

static int probe_ok(void *device)
{
    (void)device;
    probe_calls++;
    return 0;
}

static int probe_fail(void *device)
{
    (void)device;
    probe_calls++;
    return -22;
}

/* Defers twice, then succeeds: the shape of a driver waiting for an IRQ or a
 * firmware blob that a later bring-up step installs. */
static int probe_defer_then_ok(void *device)
{
    (void)device;
    defer_calls++;
    return defer_calls < 3 ? VQEAF_DRIVER_DEFER : 0;
}

static int remove_counted(void *device)
{
    (void)device;
    remove_calls++;
    return 0;
}

static int match_never(void *device)
{
    (void)device;
    return 0;
}

static int match_claim(void *device)
{
    (void)device;
    return claim;
}

static const VqeafDriver DRV_OK = {
    VQEAF_DRIVER_ABI, sizeof(VqeafDriver), "ram-storage", VQEAF_DEV_STORAGE,
    probe_ok, remove_counted, NULL
};
static const VqeafDriver DRV_FAIL = {
    VQEAF_DRIVER_ABI, sizeof(VqeafDriver), "broken-radio", VQEAF_DEV_RADIO,
    probe_fail, remove_counted, NULL
};
static const VqeafDriver DRV_DEFER = {
    VQEAF_DRIVER_ABI, sizeof(VqeafDriver), "late-slimbus", VQEAF_DEV_RADIO,
    probe_defer_then_ok, remove_counted, NULL
};
static const VqeafDriver DRV_DECLINE = {
    VQEAF_DRIVER_ABI, sizeof(VqeafDriver), "declining", VQEAF_DEV_SENSOR,
    probe_ok, remove_counted, match_never
};
static const VqeafDriver DRV_CLAIM = {
    VQEAF_DRIVER_ABI, sizeof(VqeafDriver), "claiming", VQEAF_DEV_SENSOR,
    probe_ok, remove_counted, match_claim
};

int main(void)
{
    static int dev_a, dev_b, dev_c;
    static char names[VQEAF_DRIVER_MAX + 1u][8];
    static VqeafDriver many[VQEAF_DRIVER_MAX + 1u];
    VqeafDriverInstance ins;
    char buf[512], tiny[8];
    uint32_t id = 0, other = 0, i;
    int n;

    /* 1. An incomplete descriptor is refused and nothing is registered. */
    vqeaf_driver_reset();
    assert(vqeaf_driver_count() == 0 && vqeaf_driver_instance_count() == 0);
    assert(vqeaf_driver_register(NULL) == -1);
    {
        VqeafDriver bad = DRV_OK;

        bad.abi = VQEAF_DRIVER_ABI + 1u;
        assert(vqeaf_driver_register(&bad) == -1);
        bad = DRV_OK;
        bad.size = sizeof(VqeafDriver) - 1u;
        assert(vqeaf_driver_register(&bad) == -1);
        bad = DRV_OK;
        bad.name = "";
        assert(vqeaf_driver_register(&bad) == -1);
        bad = DRV_OK;
        bad.name = NULL;
        assert(vqeaf_driver_register(&bad) == -1);
        bad = DRV_OK;
        bad.probe = NULL;
        assert(vqeaf_driver_register(&bad) == -1);
    }
    assert(vqeaf_driver_count() == 0);
    assert(vqeaf_driver_register(&DRV_OK) == 0);
    assert(vqeaf_driver_register(&DRV_OK) == -2);
    assert(vqeaf_driver_count() == 1);

    /* 2. bind READY, refuse a second binding of the same device, unbind once. */
    probe_calls = 0;
    remove_calls = 0;
    assert(vqeaf_driver_bind(NULL, &dev_a, &id) == -1);
    assert(vqeaf_driver_bind("ram-storage", &dev_a, NULL) == -1);
    assert(vqeaf_driver_bind("missing", &dev_a, &id) == -2);
    assert(vqeaf_driver_bind("ram-storage", &dev_a, &id) == 0);
    assert(vqeaf_driver_get(id, &ins) == 0);
    assert(ins.state == VQEAF_DRV_READY && ins.last_error == 0);
    assert(ins.attempts == 1 && probe_calls == 1);
    assert(vqeaf_driver_bind("ram-storage", &dev_a, &other) == -4);
    assert(vqeaf_driver_bind("ram-storage", &dev_b, &other) == 0);
    assert(vqeaf_driver_instance_count() == 2);
    assert(vqeaf_driver_find("ram-storage", &other) == 0 && other == 0);
    assert(vqeaf_driver_find("nope", &other) == -2);
    assert(vqeaf_driver_unbind(id) == 0 && remove_calls == 1);
    assert(vqeaf_driver_unbind(id) == -1);
    assert(vqeaf_driver_get(id, &ins) == 0 && ins.state == VQEAF_DRV_REMOVED);
    assert(vqeaf_driver_find("ram-storage", &other) == 0 && other == 1);

    /* 3. A failing probe stays FAILED and unbind must not fake a remove. */
    remove_calls = 0;
    assert(vqeaf_driver_register(&DRV_FAIL) == 0);
    assert(vqeaf_driver_bind("broken-radio", &dev_c, &id) == -22);
    assert(vqeaf_driver_get(id, &ins) == 0);
    assert(ins.state == VQEAF_DRV_FAILED && ins.last_error == -22);
    assert(vqeaf_driver_unbind(id) == 0);
    assert(remove_calls == 0);
    assert(vqeaf_driver_get(id, &ins) == 0 && ins.state == VQEAF_DRV_REMOVED);

    /* 4. Deferral is neither failure nor success; the retry pass settles it. */
    defer_calls = 0;
    remove_calls = 0;
    assert(vqeaf_driver_register(&DRV_DEFER) == 0);
    assert(vqeaf_driver_bind("late-slimbus", &dev_a, &id) == VQEAF_DRIVER_DEFER);
    assert(vqeaf_driver_get(id, &ins) == 0 && ins.state == VQEAF_DRV_DEFERRED);
    assert(ins.last_error == VQEAF_DRIVER_DEFER && remove_calls == 0);
    assert(vqeaf_driver_retry_deferred() == 0);
    assert(vqeaf_driver_get(id, &ins) == 0);
    assert(ins.state == VQEAF_DRV_DEFERRED && ins.attempts == 2);
    assert(vqeaf_driver_retry_deferred() == 1);
    assert(vqeaf_driver_get(id, &ins) == 0);
    assert(ins.state == VQEAF_DRV_READY && ins.last_error == 0 && ins.attempts == 3);
    assert(vqeaf_driver_retry_deferred() == 0);
    assert(vqeaf_driver_unbind(id) == 0 && remove_calls == 1);

    /* 5. match() decides which registered driver gets the device. */
    vqeaf_driver_reset();
    claim = 0;
    assert(vqeaf_driver_register(&DRV_DECLINE) == 0);
    assert(vqeaf_driver_register(&DRV_CLAIM) == 0);
    assert(vqeaf_driver_bind_match(&dev_a, &id) == -5);
    claim = 1;
    assert(vqeaf_driver_bind_match(&dev_a, &id) == 0);
    assert(vqeaf_driver_get(id, &ins) == 0);
    assert(strcmp(ins.driver->name, "claiming") == 0);
    claim = 0;

    /* 6. The registry is bounded and refuses the extra driver. */
    vqeaf_driver_reset();
    for(i = 0; i < VQEAF_DRIVER_MAX + 1u; i++) {
        names[i][0] = 'd';
        names[i][1] = (char)('0' + (i / 10u));
        names[i][2] = (char)('0' + (i % 10u));
        names[i][3] = '\0';
        many[i].abi = VQEAF_DRIVER_ABI;
        many[i].size = sizeof(VqeafDriver);
        many[i].name = names[i];
        many[i].device_class = VQEAF_DEV_UNKNOWN;
        many[i].probe = probe_ok;
        many[i].remove = NULL;
        many[i].match = NULL;
        assert(vqeaf_driver_register(&many[i]) ==
               (i < VQEAF_DRIVER_MAX ? 0 : -3));
    }
    assert(vqeaf_driver_count() == VQEAF_DRIVER_MAX);

    /* 7. The report is bounded, NUL terminated and states the real state. */
    vqeaf_driver_reset();
    probe_calls = 0;
    remove_calls = 0;
    assert(vqeaf_driver_register(&DRV_OK) == 0);
    assert(vqeaf_driver_bind("ram-storage", &dev_a, &id) == 0);
    n = vqeaf_driver_report(buf, (int)sizeof(buf));
    assert(n > 0 && n < (int)sizeof(buf) && buf[n] == '\0');
    assert(strstr(buf, "registered=1 instances=1") != NULL);
    assert(strstr(buf, "ram-storage state=ready") != NULL);
    assert(strstr(buf, "class=1") != NULL);
    assert(vqeaf_driver_report(NULL, 16) == -1);
    assert(vqeaf_driver_report(buf, 0) == -1);
    n = vqeaf_driver_report(tiny, (int)sizeof(tiny));
    assert(n == (int)sizeof(tiny) - 1 && tiny[n] == '\0');

    vqeaf_driver_reset();
    assert(vqeaf_driver_count() == 0 && vqeaf_driver_instance_count() == 0);
    n = vqeaf_driver_report(buf, (int)sizeof(buf));
    assert(n > 0 && strstr(buf, "registered=0 instances=0") != NULL);
    return 0;
}
