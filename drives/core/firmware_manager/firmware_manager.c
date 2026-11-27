#include "mre_firmware.h"
#include <string.h>

mre_status_t mre_firmware_validate_policy(
    const mre_firmware_policy_t *policy,
    const char *requested_version,
    const char *path,
    mre_firmware_verify_fn verify,
    void *context)
{
    if (policy == NULL || requested_version == NULL || path == NULL ||
        policy->component == NULL || policy->component[0] == '\0' ||
        policy->version == NULL || policy->expected_sha256 == NULL ||
        policy->expected_sha256[0] == '\0' || policy->license_id == NULL ||
        policy->license_id[0] == '\0' || policy->minimum_size > policy->maximum_size) {
        return MRE_E_INVALID;
    }
    if (policy->redistribution_allowed != 1) {
        return MRE_E_PERMISSION;
    }
    if (strcmp(policy->version, requested_version) != 0) {
        return MRE_E_VERSION;
    }
    if (verify == NULL) {
        return MRE_E_UNSUPPORTED;
    }
    return verify(path, policy->expected_sha256, policy->minimum_size,
                  policy->maximum_size, context);
}
