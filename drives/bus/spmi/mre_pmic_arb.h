/* SPDX-License-Identifier: MIT */
/* mre_pmic_arb.h - SPMI PMIC arbiter (v5+) contract for SM6150.
 *
 * The arbiter is the only path to the PMIC: an APID table maps
 * (sid, address) to a channel, a command register starts a transaction and the
 * arbiter's status word says whether the PMIC accepted it. The driver refuses
 * to guess: until probe() has walked the APID map, every transfer is
 * MRE_E_INVALID, and until authorize_writes() has confirmation from both the
 * owner and the platform, every write is MRE_E_PERMISSION.
 *
 * Resource facts (window bases/sizes, execution environment, channel) come
 * from the live device tree; this header only declares the shapes.
 */
#ifndef MRE_PMIC_ARB_H
#define MRE_PMIC_ARB_H

#include <stdint.h>

#include "mre_fdt.h"
#include "mre_spmi.h"

/* Arbitrated PPIDs in the SM6150 arbiter configuration. */
#define MRE_PMIC_ARB_MAX_APIDS 512u
/* A PPID is ((sid << 8) | (address >> 8)); sid <= 0x0f, so the table needs
 * 4096 slots regardless of how many APIDs exist. */
#define MRE_PMIC_ARB_PPID_TABLE 4096u
/* Version register floor for the v5 register layout this driver programs. */
#define MRE_PMIC_ARB_VERSION_V5_MIN 0x00050000u

typedef enum mre_pmic_arb_region {
    MRE_PMIC_ARB_CORE = 0,
    MRE_PMIC_ARB_CHANNELS,
    MRE_PMIC_ARB_OBSERVER,
    MRE_PMIC_ARB_CONFIG
} mre_pmic_arb_region_t;

typedef struct mre_pmic_arb_window {
    uint64_t base;
    uint32_t size;
} mre_pmic_arb_window_t;

typedef mre_status_t (*mre_pmic_arb_read32_fn)(void *context,
                                               mre_pmic_arb_region_t region,
                                               uint32_t offset,
                                               uint32_t *value);
typedef mre_status_t (*mre_pmic_arb_write32_fn)(void *context,
                                                mre_pmic_arb_region_t region,
                                                uint32_t offset,
                                                uint32_t value);

typedef struct mre_pmic_arb_io_ops {
    mre_pmic_arb_read32_fn read32;
    mre_pmic_arb_write32_fn write32;
    uint64_t (*time_us)(void *context);
    void (*delay_us)(void *context, uint32_t microseconds);
    void (*lock)(void *context);
    void (*unlock)(void *context);
    /* Optional: posted-write barrier before the command is issued. */
    void (*barrier)(void *context);
} mre_pmic_arb_io_ops_t;

typedef struct mre_pmic_arb {
    const mre_pmic_arb_io_ops_t *ops;
    void *context;
    mre_pmic_arb_window_t windows[4];
    uint8_t execution_environment;
    uint8_t channel;
    uint32_t timeout_us;
    uint32_t max_polls;
    uint8_t resources_verified;
    uint8_t probed;
    uint8_t write_authorized;
    uint8_t faulted;
    uint8_t transaction_active;
    uint32_t version;
    uint16_t ppid_to_apid[MRE_PMIC_ARB_PPID_TABLE];
    uint8_t ppid_valid[MRE_PMIC_ARB_PPID_TABLE];
    uint8_t apid_write_ee[MRE_PMIC_ARB_MAX_APIDS];
} mre_pmic_arb_t;

/* Parses the single qcom,spmi-pmic-arb node: the five reg-names in order,
 * the four windows this driver maps, qcom,ee and qcom,channel. A second node,
 * a renamed region or an out-of-range property is an error, not a default. */
mre_status_t mre_pmic_arb_parse_fdt(const mre_fdt_t *fdt,
                                    mre_pmic_arb_window_t windows[4],
                                    uint8_t *execution_environment,
                                    uint8_t *channel);

/* All four windows must be non-zero and large enough for their layout;
 * resources_verified must be exactly 1 (the platform measured them). */
mre_status_t mre_pmic_arb_init(mre_pmic_arb_t *arb,
                               const mre_pmic_arb_io_ops_t *ops, void *context,
                               const mre_pmic_arb_window_t windows[4],
                               uint8_t execution_environment, uint8_t channel,
                               uint32_t timeout_us, uint32_t max_polls,
                               uint8_t resources_verified);

/* Reads the version and the whole APID map. MRE_E_UNSUPPORTED below
 * MRE_PMIC_ARB_VERSION_V5_MIN, MRE_E_NOT_FOUND when the map is empty. */
mre_status_t mre_pmic_arb_probe(mre_pmic_arb_t *arb);

mre_status_t mre_pmic_arb_bind_spmi(mre_pmic_arb_t *arb,
                                    mre_spmi_bus_t *bus, uint32_t timeout_us);

/* Enables writes only when the caller states the owner was verified AND that
 * writing is explicitly authorized. Anything less is MRE_E_PERMISSION. */
mre_status_t mre_pmic_arb_authorize_writes(mre_pmic_arb_t *arb,
                                           mre_spmi_bus_t *bus,
                                           uint8_t owner_verified,
                                           uint8_t explicit_authorization);

#endif
