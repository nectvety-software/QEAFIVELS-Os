/* SPDX-License-Identifier: MIT */
/* tests/test_mt689x_caps.c - host check of the MT6893 capability census.
 *
 * The publish/report half of platform/mt689x/mt689x_caps.c is pure, so this
 * test drives it with synthetic probes: an empty board must not report READY,
 * and a fully populated one must mark every supported row READY while keeping
 * NFC/camera/baseband UNSUPPORTED. Build/run: scripts/test_mt689x_caps.py.
 */
#include <stdio.h>
#include <string.h>

#include "mt689x_caps.h"

static int failures;

static void expect_state(const char *name, VqeafCapabilityState want)
{
    VqeafCapability cap;
    if (vqeaf_cap_get(name, &cap) != 0) {
        printf("FAIL %s: not published\n", name);
        failures++;
        return;
    }
    if (cap.state != want) {
        printf("FAIL %s: state %d, want %d\n", name, (int)cap.state,
               (int)want);
        failures++;
    }
}

int main(void)
{
    MtkHwProbe empty;
    MtkHwProbe full;
    char report[2048];

    memset(&empty, 0, sizeof(empty));
    mtk_caps_publish(&empty);
    expect_state("storage.ufs", VQEAF_CAP_UNSUPPORTED);
    expect_state("display.drm", VQEAF_CAP_UNSUPPORTED);
    expect_state("display.refresh_high", VQEAF_CAP_UNSUPPORTED);
    expect_state("wifi", VQEAF_CAP_BLOCKED_FIRMWARE);
    expect_state("bluetooth", VQEAF_CAP_BLOCKED_FIRMWARE);
    expect_state("gpu", VQEAF_CAP_UNSUPPORTED);
    expect_state("audio", VQEAF_CAP_UNSUPPORTED);
    expect_state("kvm", VQEAF_CAP_UNSUPPORTED);

    memset(&full, 0, sizeof(full));
    full.ufs_present = 1;
    full.usb_udc_present = 1;
    full.usb_host_present = 1;
    full.drm_card_present = 1;
    full.drm_render_present = 1;
    full.panel_max_refresh = 120;
    full.touch_present = 1;
    full.wlan_iface_present = 1;
    full.wlan_fw_present = 1;
    full.bt_hci_present = 1;
    full.bt_fw_present = 1;
    full.sound_cards = 1;
    full.accel_present = 1;
    full.kvm_present = 1;
    mtk_caps_publish(&full);

    const char *ready[] = {
        "storage.ufs", "usb.gadget", "usb.host", "display.drm",
        "display.refresh_high", "input.touch", "wifi", "bluetooth",
        "audio", "gpu", "sensor.accel", "kvm",
    };
    for (unsigned i = 0; i < sizeof(ready) / sizeof(ready[0]); i++)
        expect_state(ready[i], VQEAF_CAP_READY);
    expect_state("nfc", VQEAF_CAP_UNSUPPORTED);
    expect_state("camera", VQEAF_CAP_UNSUPPORTED);
    expect_state("baseband", VQEAF_CAP_UNSUPPORTED);

    int n = mtk_caps_report(report, (int)sizeof(report));
    if (n <= 0 || strstr(report, "MT6891/MT6893") == NULL) {
        printf("FAIL report header: %s\n", report);
        failures++;
    }
    if (strstr(report, "display.refresh_high") == NULL) {
        printf("FAIL report missing refresh row\n");
        failures++;
    }

    if (failures == 0)
        printf("test_mt689x_caps: OK (empty board honest, full board READY)\n");
    return failures ? 1 : 0;
}
