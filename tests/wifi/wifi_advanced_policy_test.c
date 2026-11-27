#include <assert.h>
#include <errno.h>
#include <string.h>
#include "vqeaf/wifi_advanced.h"

int main(void)
{
    vqeaf_wifi_firmware_package_t p;
    memset(&p, 0, sizeof(p));
    assert(vqeaf_wifi_backend_validate(VQEAF_WIFI_TRANSPORT_ICNSS_SNOC,
           VQEAF_WIFI_BACKEND_AUTO, 0, 0) == 0);
    assert(vqeaf_wifi_backend_validate(VQEAF_WIFI_TRANSPORT_ICNSS_SNOC,
           VQEAF_WIFI_BACKEND_ICNSS_SNOC, 0, 0) == 0);
    assert(vqeaf_wifi_backend_validate(VQEAF_WIFI_TRANSPORT_ICNSS_SNOC,
           VQEAF_WIFI_BACKEND_CNSS2_PCIE, 0, 0) == -EOPNOTSUPP);
    assert(vqeaf_wifi_backend_validate(VQEAF_WIFI_TRANSPORT_PCIE,
           VQEAF_WIFI_BACKEND_CNSS2_PCIE, 0, 0) == -EOPNOTSUPP);
    assert(vqeaf_wifi_backend_validate(VQEAF_WIFI_TRANSPORT_PCIE,
           VQEAF_WIFI_BACKEND_CNSS2_PCIE, 1, 1) == 0);
    assert(strcmp(vqeaf_wifi_backend_name(VQEAF_WIFI_BACKEND_CNSS2_PCIE),
                  "cnss2-pcie") == 0);

    p.abi = VQEAF_WIFI_ADVANCED_ABI; p.size = sizeof(p);
    p.board_id = 0x6150; p.chip_id = 3990; p.firmware_version = 1;
    p.sha256_verified = p.signature_verified = 1;
    p.board_id_verified = p.chip_id_verified = 1;
    assert(vqeaf_wifi_firmware_package_validate(&p, 0x6150, 3990, 0) == 0);
    assert(vqeaf_wifi_firmware_package_validate(&p, 0x6151, 3990, 0) == -EXDEV);
    assert(vqeaf_wifi_firmware_package_validate(&p, 0x6150, 3991, 0) == -EXDEV);
    assert(vqeaf_wifi_firmware_package_validate(&p, 0x6150, 3990, 1) == -ENODATA);
    p.calibration_backup_verified = p.factory_mac_verified = 1;
    assert(vqeaf_wifi_firmware_package_validate(&p, 0x6150, 3990, 1) == 0);
    p.signature_verified = 0;
    assert(vqeaf_wifi_firmware_package_validate(&p, 0x6150, 3990, 0) == -EACCES);
    return 0;
}