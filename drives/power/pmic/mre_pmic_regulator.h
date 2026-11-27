/* SPDX-License-Identifier: MIT */
/* mre_pmic_regulator.h - one SPMI regulator, described rather than hardcoded.
 *
 * The descriptor is the whole hardware description: which slave, which
 * register, which bits enable it and how the voltage selector is encoded. That
 * keeps every per-rail number in the platform table that read the PMIC
 * documentation, out of this driver.
 *
 * Writes are doubly gated: the descriptor must be marked evidence_verified
 * (someone read it out of the PMIC spec) and the bus must be verified
 * (mre_spmi_bus_t.verified). Every write is read back and rolled back on a
 * mismatch, so a failed set never leaves a rail in a state nobody recorded.
 */
#ifndef MRE_PMIC_REGULATOR_H
#define MRE_PMIC_REGULATOR_H

#include <stdint.h>

#include "mre_spmi.h"

typedef struct mre_pmic_regulator_desc {
    const char *name;
    uint8_t sid;
    uint16_t enable_address;
    uint8_t enable_mask;
    uint8_t enable_value;
    uint16_t voltage_address;
    uint8_t voltage_mask;
    uint8_t voltage_shift;
    uint32_t step_uv;
    uint32_t min_uv;
    uint32_t selector_max;
    uint8_t evidence_verified;
} mre_pmic_regulator_desc_t;

typedef struct mre_pmic_regulator {
    mre_spmi_bus_t *bus;
    mre_pmic_regulator_desc_t desc;
} mre_pmic_regulator_t;

/* Validates the descriptor (non-zero masks, non-zero step, shift inside the
 * mask, selector_max inside a byte) and copies it. No bus access. */
mre_status_t mre_pmic_regulator_init(mre_pmic_regulator_t *regulator,
                                     mre_spmi_bus_t *bus,
                                     const mre_pmic_regulator_desc_t *desc);

/* Reads the enable register and reports the bit pattern, not the voltage. */
mre_status_t mre_pmic_regulator_get_enabled(mre_pmic_regulator_t *regulator,
                                            uint32_t *enabled);

mre_status_t mre_pmic_regulator_set_enabled(mre_pmic_regulator_t *regulator,
                                            uint32_t enabled);

/* Selector = (uv - min_uv) / step_uv; a request that is not exactly on the
 * step grid is refused rather than rounded. *actual_uv (optional) reports the
 * voltage actually programmed. */
mre_status_t mre_pmic_regulator_set_voltage(mre_pmic_regulator_t *regulator,
                                            uint32_t uv, uint32_t *actual_uv);

#endif
