/* SPDX-License-Identifier: MIT */
/* drives/core/drive_catalog.c - drives/ registered as QEAFIVELS core drivers.
 *
 * One descriptor per drive, one probe adapter each. The adapters are thin on
 * purpose: they check the device record the platform handed over, then call
 * the drive's own entry point and translate its status. No register offsets,
 * no clock numbers and no rail values live here - the drives own those, and
 * this file must not become a second copy of them.
 */
#include <string.h>

#include "drive_catalog.h"

#include "mre_panel_nt36672a.h"

/* A drive probe reports "the platform has not given me its resources yet" as
 * MRE_E_PERMISSION; in the core that is DEFERRED, not FAILED, because nothing
 * about the hardware was proven broken. Everything else keeps its sign. */
static int probe_status(mre_status_t st)
{
    if (st == MRE_OK) return 0;
    if (st == MRE_E_PERMISSION) return VQEAF_DRIVER_DEFER;
    return (int)st;
}

static const VqeafDriveDevice *checked(void *device)
{
    VqeafDriveDevice *d = (VqeafDriveDevice *)device;

    if (d == NULL || d->abi != VQEAF_DRIVE_CATALOG_ABI ||
        d->size != sizeof(*d) || d->compatible == NULL)
        return NULL;
    return d;
}

static int probe_pmic_arb(void *device)
{
    const VqeafDriveDevice *d = checked(device);

    if (d == NULL) return MRE_E_INVALID;
    if (strcmp(d->compatible, "qcom,spmi-pmic-arb") != 0) return MRE_E_INVALID;
    if (d->resources_verified == 0u || d->platform == NULL)
        return VQEAF_DRIVER_DEFER;
    return probe_status(mre_pmic_arb_probe((mre_pmic_arb_t *)d->platform));
}

static int probe_geni_i2c(void *device)
{
    const VqeafDriveDevice *d = checked(device);

    if (d == NULL) return MRE_E_INVALID;
    if (strcmp(d->compatible, "qcom,geni-i2c") != 0) return MRE_E_INVALID;
    if (d->resources_verified == 0u || d->platform == NULL)
        return VQEAF_DRIVER_DEFER;
    return probe_status(mre_geni_i2c_attach((mre_geni_i2c_bus_t *)d->platform));
}

static int probe_pmic_regulator(void *device)
{
    const VqeafDriveDevice *d = checked(device);
    uint32_t enabled = 0u;

    if (d == NULL) return MRE_E_INVALID;
    if (strcmp(d->compatible, "qcom,spmi-pmic-regulator") != 0)
        return MRE_E_INVALID;
    if (d->resources_verified == 0u || d->platform == NULL)
        return VQEAF_DRIVER_DEFER;
    return probe_status(
        mre_pmic_regulator_get_enabled((mre_pmic_regulator_t *)d->platform,
                                       &enabled));
}

static int probe_dispcc(void *device)
{
    const VqeafDriveDevice *d = checked(device);
    mre_dispcc_clock_snapshot_t snapshot;
    mre_status_t st;

    if (d == NULL) return MRE_E_INVALID;
    if (strcmp(d->compatible, "qcom,sm6150-dispcc") != 0) return MRE_E_INVALID;
    if (d->resources_verified == 0u || d->platform == NULL)
        return VQEAF_DRIVER_DEFER;
    st = mre_sm6150_dispcc_read_clock(
        (mre_sm6150_dispcc_t *)d->platform, MRE_DISPCC_MDSS_AHB_SRC, &snapshot);
    /* No parent-rate provider means the platform has not finished wiring the
     * clock tree, so the instance is deferred rather than failed. */
    if (st == MRE_E_NOT_FOUND) return VQEAF_DRIVER_DEFER;
    return probe_status(st);
}

static int probe_touch(void *device)
{
    const VqeafDriveDevice *d = checked(device);
    mre_touch_device_t *touch;
    mre_touch_ops_t ops;
    mre_touch_transform_t transform;

    if (d == NULL) return MRE_E_INVALID;
    if (strcmp(d->compatible, "mre,touchscreen") != 0) return MRE_E_INVALID;
    if (d->resources_verified == 0u || d->platform == NULL)
        return VQEAF_DRIVER_DEFER;
    touch = (mre_touch_device_t *)d->platform;
    /* mre_touch_init() clears the device, so take the platform's copies
     * first; passing pointers back into the object being cleared would feed
     * it zeros. */
    ops = touch->ops;
    transform = touch->transform;
    return probe_status(mre_touch_init(touch, &ops, &transform));
}

static int probe_panel(void *device)
{
    const VqeafDriveDevice *d = checked(device);
    mre_status_t st;

    if (d == NULL) return MRE_E_INVALID;
    if (strcmp(d->compatible, "novatek,nt36672a-shenchao") != 0)
        return MRE_E_INVALID;
    if (d->platform == NULL) return VQEAF_DRIVER_DEFER;
    st = mre_panel_descriptor_validate(
        (const mre_panel_descriptor_t *)d->platform);
    if (st != MRE_OK) return (int)st;
    /* The panel itself defines no ID command, so nothing can prove the fitted
     * panel matches this table. Its init sequence stays unauthorized: the
     * honest state is DEFERRED (needs a measured ID), not READY. */
    return VQEAF_DRIVER_DEFER;
}

#define DRIVE_MATCH(fn, name)                                                  \
    static int fn(void *device)                                                \
    {                                                                          \
        const VqeafDriveDevice *d = checked(device);                           \
        if (d == NULL) return 0;                                               \
        return strcmp(d->compatible, name) == 0 ? 1 : 0;                       \
    }

DRIVE_MATCH(match_pmic_arb, "qcom,spmi-pmic-arb")
DRIVE_MATCH(match_geni_i2c, "qcom,geni-i2c")
DRIVE_MATCH(match_pmic_regulator, "qcom,spmi-pmic-regulator")
DRIVE_MATCH(match_dispcc, "qcom,sm6150-dispcc")
DRIVE_MATCH(match_touch, "mre,touchscreen")
DRIVE_MATCH(match_panel, "novatek,nt36672a-shenchao")

#define DRIVE(name, cls, probe, match)                                         \
    {                                                                          \
        VQEAF_DRIVER_ABI, sizeof(VqeafDriver), name, cls, probe, NULL, match    \
    }

static const VqeafDriver catalog[] = {
    DRIVE("qcom,spmi-pmic-arb", VQEAF_DEV_POWER, probe_pmic_arb, match_pmic_arb),
    DRIVE("qcom,geni-i2c", VQEAF_DEV_UNKNOWN, probe_geni_i2c, match_geni_i2c),
    DRIVE("qcom,spmi-pmic-regulator", VQEAF_DEV_POWER, probe_pmic_regulator,
          match_pmic_regulator),
    DRIVE("qcom,sm6150-dispcc", VQEAF_DEV_DISPLAY, probe_dispcc, match_dispcc),
    DRIVE("mre,touchscreen", VQEAF_DEV_INPUT, probe_touch, match_touch),
    DRIVE("novatek,nt36672a-shenchao", VQEAF_DEV_DISPLAY, probe_panel,
          match_panel)
};

#undef DRIVE
#undef DRIVE_MATCH

static int catalog_registered;

uint32_t vqeaf_drive_catalog_count(void)
{
    return (uint32_t)(sizeof(catalog) / sizeof(catalog[0]));
}

const VqeafDriver *vqeaf_drive_catalog_at(uint32_t index)
{
    if (index >= vqeaf_drive_catalog_count()) return NULL;
    return &catalog[index];
}

int vqeaf_drive_catalog_register_all(void)
{
    uint32_t i;
    int rc;

    if (catalog_registered) return 0;
    for (i = 0; i < vqeaf_drive_catalog_count(); ++i) {
        rc = vqeaf_driver_register(&catalog[i]);
        if (rc != 0) return rc;
    }
    catalog_registered = 1;
    return 0;
}

int vqeaf_drive_catalog_bind(const char *name, VqeafDriveDevice *device,
                             uint32_t *instance_id)
{
    if (device == NULL) return MRE_E_INVALID;
    return vqeaf_driver_bind(name, device, instance_id);
}

int vqeaf_drive_catalog_retry(void)
{
    return vqeaf_driver_retry_deferred();
}

int vqeaf_drive_catalog_report(char *out, int cap)
{
    return vqeaf_driver_report(out, cap);
}

void vqeaf_drive_catalog_reset_for_test(void)
{
    catalog_registered = 0;
}
