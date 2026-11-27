/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_WIFI_ADVANCED_H
#define VQEAF_WIFI_ADVANCED_H
#include <stddef.h>
#include <stdint.h>

#define VQEAF_WIFI_ADVANCED_ABI 1u

typedef enum {
    VQEAF_WIFI_TRANSPORT_UNKNOWN = 0,
    VQEAF_WIFI_TRANSPORT_ICNSS_SNOC = 1,
    VQEAF_WIFI_TRANSPORT_PCIE = 2
} vqeaf_wifi_transport_t;

typedef enum {
    VQEAF_WIFI_BACKEND_AUTO = 0,
    VQEAF_WIFI_BACKEND_ICNSS_SNOC = 1,
    VQEAF_WIFI_BACKEND_CNSS2_PCIE = 2
} vqeaf_wifi_backend_t;

typedef struct {
    uint32_t abi, size;
    uint32_t board_id;
    uint32_t chip_id;
    uint32_t firmware_version;
    uint8_t sha256_verified;
    uint8_t signature_verified;
    uint8_t board_id_verified;
    uint8_t chip_id_verified;
    uint8_t calibration_backup_verified;
    uint8_t factory_mac_verified;
    uint8_t reserved[2];
} vqeaf_wifi_firmware_package_t;

/* Selects a backend only if the hardware transport supports it. */
int vqeaf_wifi_backend_validate(vqeaf_wifi_transport_t transport,
                                vqeaf_wifi_backend_t backend,
                                int pcie_enumerated,
                                int cnss2_driver_registered);
/* Package must be signed, hash checked, and explicitly match this device.
 * Calibration updates additionally require a verified per-device backup and
 * factory MAC. This is a preflight gate; it does not write files or NV. */
int vqeaf_wifi_firmware_package_validate(
    const vqeaf_wifi_firmware_package_t *package,
    uint32_t expected_board_id, uint32_t expected_chip_id,
    int replacing_calibration);
const char *vqeaf_wifi_backend_name(vqeaf_wifi_backend_t backend);

#endif
