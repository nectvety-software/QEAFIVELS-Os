/* SPDX-License-Identifier: MIT */
/* mre_spmi.h - SPMI byte transport contract.
 *
 * One byte at a time, addressed by (slave id, register address), over ops the
 * owner supplies. Reads are always allowed once the bus exists; writes need
 * `verified` set by whatever proved the transport (for the PMIC arbiter that
 * is mre_pmic_arb_authorize_writes, not the bus owner guessing).
 */
#ifndef MRE_SPMI_H
#define MRE_SPMI_H

#include <stdint.h>

#include "mre_types.h"

typedef mre_status_t (*mre_spmi_read8_fn)(void *context, uint8_t sid,
                                          uint16_t address, uint8_t *value,
                                          uint32_t timeout_us);
typedef mre_status_t (*mre_spmi_write8_fn)(void *context, uint8_t sid,
                                           uint16_t address, uint8_t value,
                                           uint32_t timeout_us);

typedef struct mre_spmi_ops {
    mre_spmi_read8_fn read8;
    mre_spmi_write8_fn write8;
} mre_spmi_ops_t;

typedef struct mre_spmi_bus {
    const mre_spmi_ops_t *ops;
    void *context;
    uint32_t timeout_us;
    uint8_t verified;
} mre_spmi_bus_t;

/* Binds ops and clears `verified`; a new bus has to be authorized again. */
mre_status_t mre_spmi_init(mre_spmi_bus_t *bus, const mre_spmi_ops_t *ops,
                           void *context, uint32_t timeout_us);

/* Slave ids above 0x0f do not exist in the PMIC arbiter address space. */
mre_status_t mre_spmi_read8(mre_spmi_bus_t *bus, uint8_t sid,
                            uint16_t address, uint8_t *value);

/* MRE_E_PERMISSION until the transport is verified. */
mre_status_t mre_spmi_write8(mre_spmi_bus_t *bus, uint8_t sid,
                             uint16_t address, uint8_t value);

#endif
