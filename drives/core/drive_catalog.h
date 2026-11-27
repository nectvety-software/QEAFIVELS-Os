/* SPDX-License-Identifier: MIT */
/* drives/core/drive_catalog.h - the drives/ set as QEAFIVELS core drivers.
 *
 * The hardware drives under drives/ each expose their own contract
 * (mre_spmi, mre_pmic_arb, mre_pmic_regulator, mre_geni_i2c,
 * mre_sm6150_dispcc, mre_touch, the nt36672a panel). system/core/driver.c is
 * the OS's driver core: it owns registration, instances and the state machine.
 * This catalog is the one place that joins them, so `drivers`-style reports
 * driven by vqeaf_driver_report() show the MRE drives in the same registry as
 * everything else instead of a second, parallel bookkeeping layer.
 *
 * Semantics, chosen to stay fail-closed:
 *   - register_all() only registers descriptors; nothing touches hardware.
 *   - A device record (VqeafDriveDevice) must state that the platform
 *     verified its resources. Without that the probe returns
 *     VQEAF_DRIVER_DEFER, so the instance is DEFERRED - never FAILED and never
 *     READY. A deferred instance is retried by vqeaf_driver_retry_deferred()
 *     once the platform has the resources.
 *   - With verified resources the probe runs one real, read-only transaction
 *     of that drive (APID map walk, regulator enable read, DISP_CC clock read,
 *     bus attach). Only a zero return from that is READY.
 *   - The panel has no verified ID command (its own source says so), so its
 *     probe always defers: its init table must never be authorized by a guess.
 */
#ifndef VQEAF_DRIVE_CATALOG_H
#define VQEAF_DRIVE_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#include "mre_geni_i2c.h"
#include "mre_pmic_arb.h"
#include "mre_pmic_regulator.h"
#include "mre_sm6150_dispcc.h"
#include "mre_touch.h"
#include "vqeaf/driver.h"

#define VQEAF_DRIVE_CATALOG_ABI 1u

/* One device instance handed to a catalog probe. `platform` points at the
 * drive's own contract object:
 *   qcom,spmi-pmic-arb        -> mre_pmic_arb_t
 *   qcom,geni-i2c             -> mre_geni_i2c_bus_t
 *   qcom,spmi-pmic-regulator  -> mre_pmic_regulator_t
 *   qcom,sm6150-dispcc        -> mre_sm6150_dispcc_t
 *   novatek,nt36672a-shenchao -> const mre_panel_descriptor_t
 *   mre,touchscreen           -> mre_touch_device_t
 */
typedef struct {
    uint32_t abi;
    uint32_t size;
    const char *compatible;
    uint32_t resources_verified; /* 1 only after the platform mapped/proved them */
    void *platform;
} VqeafDriveDevice;

/* Registers every descriptor once. Returns 0, or the first negative error from
 * vqeaf_driver_register() (-1 invalid, -2 duplicate, -3 full). Safe to call
 * twice: the second call returns 0 without registering duplicates. */
int vqeaf_drive_catalog_register_all(void);

/* Number of descriptors in the catalog and read-only access to them. */
uint32_t vqeaf_drive_catalog_count(void);
const VqeafDriver *vqeaf_drive_catalog_at(uint32_t index);

/* Binds one catalog driver to a device record, by driver name
 * (e.g. "qcom,spmi-pmic-arb"). Same return contract as vqeaf_driver_bind(). */
int vqeaf_drive_catalog_bind(const char *name, VqeafDriveDevice *device,
                             uint32_t *instance_id);

/* Re-probes every DEFERRED instance (the core's retry) and reports how many
 * became READY. */
int vqeaf_drive_catalog_retry(void);

/* Bounded multi-line registry, straight from vqeaf_driver_report(). */
int vqeaf_drive_catalog_report(char *out, int cap);

/* Drops the catalog's own "already registered" flag and every instance, so a
 * host test starts from nothing. Does not unregister from the core by itself:
 * call vqeaf_driver_reset() too when the test wants a clean core. */
void vqeaf_drive_catalog_reset_for_test(void);

#endif
