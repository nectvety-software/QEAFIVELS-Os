/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include "vqeaf/wifi_advanced.h"

const char *vqeaf_wifi_backend_name(vqeaf_wifi_backend_t backend)
{
    switch(backend) {
    case VQEAF_WIFI_BACKEND_AUTO: return "auto";
    case VQEAF_WIFI_BACKEND_ICNSS_SNOC: return "icnss-snoc";
    case VQEAF_WIFI_BACKEND_CNSS2_PCIE: return "cnss2-pcie";
    default: return "unknown";
    }
}

int vqeaf_wifi_backend_validate(vqeaf_wifi_transport_t transport,
                                vqeaf_wifi_backend_t backend,
                                int pcie_enumerated,
                                int cnss2_driver_registered)
{
    if(backend < VQEAF_WIFI_BACKEND_AUTO ||
       backend > VQEAF_WIFI_BACKEND_CNSS2_PCIE)
        return -EINVAL;
    if(backend == VQEAF_WIFI_BACKEND_AUTO)
        return transport == VQEAF_WIFI_TRANSPORT_UNKNOWN ? -ENODEV : 0;
    if(backend == VQEAF_WIFI_BACKEND_ICNSS_SNOC)
        return transport == VQEAF_WIFI_TRANSPORT_ICNSS_SNOC ? 0 : -ENODEV;
    /* CNSS2 is a PCIe transport. It must never be forced onto integrated
     * Q6/SNOC WCN3990 hardware, even if the setting is manually edited. */
    if(transport != VQEAF_WIFI_TRANSPORT_PCIE || !pcie_enumerated ||
       !cnss2_driver_registered)
        return -EOPNOTSUPP;
    return 0;
}

int vqeaf_wifi_firmware_package_validate(
    const vqeaf_wifi_firmware_package_t *p,
    uint32_t expected_board_id, uint32_t expected_chip_id,
    int replacing_calibration)
{
    if(!p || p->abi != VQEAF_WIFI_ADVANCED_ABI || p->size < sizeof(*p) ||
       expected_board_id == 0u || expected_chip_id == 0u)
        return -EINVAL;
    if(!p->sha256_verified || !p->signature_verified ||
       !p->board_id_verified || !p->chip_id_verified)
        return -EACCES;
    if(p->board_id != expected_board_id || p->chip_id != expected_chip_id)
        return -EXDEV;
    if(p->firmware_version == 0u) return -EINVAL;
    if(replacing_calibration &&
       (!p->calibration_backup_verified || !p->factory_mac_verified))
        return -ENODATA;
    return 0;
}
