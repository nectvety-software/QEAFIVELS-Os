#ifndef MRE_FIRMWARE_H
#define MRE_FIRMWARE_H

#include "mre_types.h"

typedef mre_status_t (*mre_firmware_verify_fn)(
    const char *path, const char *expected_sha256,
    uint32_t minimum_size, uint32_t maximum_size, void *context);

typedef struct mre_firmware_policy {
    const char *component;
    const char *version;
    const char *expected_sha256;
    const char *license_id;
    uint32_t minimum_size;
    uint32_t maximum_size;
    int redistribution_allowed;
} mre_firmware_policy_t;

mre_status_t mre_firmware_validate_policy(
    const mre_firmware_policy_t *policy,
    const char *requested_version,
    const char *path,
    mre_firmware_verify_fn verify,
    void *context);

#endif
