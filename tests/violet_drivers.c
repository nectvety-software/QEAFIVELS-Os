#include "violet_drivers.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int condition, const char *message)
{
    if(!condition) {
        printf("FAIL: %s\n", message);
        fails++;
    }
}

static const VioDriverStatus *find(const VioDriverStatus *items, size_t count,
                                   const char *name)
{
    for(size_t i = 0; i < count; i++)
        if(strcmp(items[i].name, name) == 0) return &items[i];
    return NULL;
}

static void expect_state(const VioDriverStatus *items, size_t count,
                         const char *name, VqeafCapabilityState want)
{
    const VioDriverStatus *item = find(items, count, name);
    expect(item != NULL, name);
    if(item != NULL && item->state != want) {
        printf("FAIL: %s state=%d want=%d\n", name,
               (int)item->state, (int)want);
        fails++;
    }
}

int main(void)
{
    VioDriverStatus items[VIO_DRIVERS_MAX];
    VioDriverEvidence evidence;
    VioHwProbe probe;
    int count;

    memset(&probe, 0, sizeof(probe));
    vio_drivers_evidence_init(&evidence);
    count = vio_drivers_collect(&probe, &evidence, items, VIO_DRIVERS_MAX);
    expect(count == (int)VIO_DRIVERS_MAX, "registry has the full provider set");
    expect_state(items, (size_t)count, "storage.emmc", VQEAF_CAP_UNSUPPORTED);
    expect_state(items, (size_t)count, "display.drm", VQEAF_CAP_UNSUPPORTED);
    expect_state(items, (size_t)count, "wifi", VQEAF_CAP_BLOCKED_FIRMWARE);
    expect_state(items, (size_t)count, "bluetooth", VQEAF_CAP_BLOCKED_FIRMWARE);
    expect_state(items, (size_t)count, "camera", VQEAF_CAP_UNSUPPORTED);
    expect_state(items, (size_t)count, "nfc", VQEAF_CAP_UNSUPPORTED);
    evidence.camera_frame_ok = 1;
    count = vio_drivers_collect(&probe, &evidence, items, VIO_DRIVERS_MAX);
    expect_state(items, (size_t)count, "camera", VQEAF_CAP_UNSUPPORTED);
    vio_drivers_evidence_init(&evidence);

    probe.mmcblk_present = 1;
    probe.drm_card_present = 1;
    probe.panel_max_refresh = 60;
    probe.touch_present = 1;
    probe.usb_udc_present = 1;
    probe.kgsl_present = 1;
    probe.v4l2_video_nodes = 6;
    probe.v4l2_capture_nodes = 2;
    count = vio_drivers_collect(&probe, &evidence, items, VIO_DRIVERS_MAX);
    expect_state(items, (size_t)count, "storage.emmc", VQEAF_CAP_READY);
    expect_state(items, (size_t)count, "display.drm", VQEAF_CAP_PRESENT);
    expect_state(items, (size_t)count, "display.refresh_high", VQEAF_CAP_UNSUPPORTED);
    expect_state(items, (size_t)count, "input.touch", VQEAF_CAP_PRESENT);
    expect_state(items, (size_t)count, "usb.gadget", VQEAF_CAP_PRESENT);
    expect_state(items, (size_t)count, "gpu", VQEAF_CAP_PRESENT);
    expect_state(items, (size_t)count, "camera", VQEAF_CAP_PRESENT);

    evidence.drm_modeset_ok = 1;
    evidence.touch_open = 1;
    evidence.usb_gadget_bound = 1;
    evidence.power_input_open = 1;
    count = vio_drivers_collect(&probe, &evidence, items, VIO_DRIVERS_MAX);
    expect_state(items, (size_t)count, "display.drm", VQEAF_CAP_READY);
    expect_state(items, (size_t)count, "input.touch", VQEAF_CAP_READY);
    expect_state(items, (size_t)count, "input.power_keys", VQEAF_CAP_READY);
    expect_state(items, (size_t)count, "usb.gadget", VQEAF_CAP_READY);
    expect_state(items, (size_t)count, "camera", VQEAF_CAP_PRESENT);

    evidence.camera_frame_ok = 1;
    count = vio_drivers_collect(&probe, &evidence, items, VIO_DRIVERS_MAX);
    expect_state(items, (size_t)count, "camera", VQEAF_CAP_READY);

    probe.wlan_fw_present = 1;
    probe.bt_fw_present = 1;
    count = vio_drivers_collect(&probe, &evidence, items, VIO_DRIVERS_MAX);
    expect_state(items, (size_t)count, "wifi", VQEAF_CAP_PRESENT);
    expect_state(items, (size_t)count, "bluetooth", VQEAF_CAP_PRESENT);
    {
        const VioDriverStatus *wifi = find(items, (size_t)count, "wifi");
        expect(wifi != NULL && wifi->reason != NULL &&
               strstr(wifi->reason, "firmware present; wlan0 absent") != NULL,
               "Wi-Fi PRESENT state explains missing netdev");
    }
    probe.wlan_iface_present = 1;
    probe.bt_hci_present = 1;
    count = vio_drivers_collect(&probe, &evidence, items, VIO_DRIVERS_MAX);
    expect_state(items, (size_t)count, "wifi", VQEAF_CAP_READY);
    expect_state(items, (size_t)count, "bluetooth", VQEAF_CAP_READY);

    expect(vio_drivers_collect(&probe, &evidence, items, 2) ==
           (int)VIO_DRIVERS_MAX, "small output buffer reports full provider count");
    expect(vio_drivers_collect(&probe, &evidence, items, 0) ==
           (int)VIO_DRIVERS_MAX, "zero output capacity is safe");
    expect(vio_drivers_collect(&probe, &evidence, NULL, VIO_DRIVERS_MAX) == -1,
           "NULL output is rejected");
    expect(vio_drivers_collect(&probe, NULL, items, VIO_DRIVERS_MAX) ==
           (int)VIO_DRIVERS_MAX, "NULL evidence uses the fail-closed default");
    expect(vio_drivers_collect(NULL, &evidence, items, VIO_DRIVERS_MAX) == -1,
           "NULL probe is rejected");
    evidence.abi = 0;
    expect(vio_drivers_collect(&probe, &evidence, items, VIO_DRIVERS_MAX) == -1,
           "wrong evidence ABI is rejected");

    vio_drivers_evidence_init(&evidence);
    probe.wlan_iface_present = 0;
    probe.bt_hci_present = 0;
    probe.wlan_fw_present = 0;
    probe.bt_fw_present = 0;
    count = vio_drivers_collect(&probe, &evidence, items, VIO_DRIVERS_MAX);
    {
        char report[4096];
        int n = vio_drivers_render(items, (size_t)count, report, sizeof(report));
        expect(n > 0, "report is non-empty");
        expect(strstr(report, "userspace ABI 1") != NULL, "report names ABI");
        expect(strstr(report, "DRM/KMS") != NULL, "report names Linux interface");
        expect(strstr(report, "BLOCKED_FIRMWARE") != NULL,
               "report preserves blocked state");
        expect(strstr(report, "WLFW service/netdev unavailable") != NULL,
               "report explains Wi-Fi blocker");
    }
    {
        char tiny[32];
        int n = vio_drivers_render(items, (size_t)count, tiny, sizeof(tiny));
        expect(n >= 0 && n < (int)sizeof(tiny), "small report buffer is bounded");
        expect(tiny[sizeof(tiny) - 1] == '\0', "small report is NUL terminated");
    }

    if(fails) {
        printf("violet_drivers: %d FAIL\n", fails);
        return 1;
    }
    printf("violet_drivers: ALL OK (%u providers)\n", VIO_DRIVERS_MAX);
    return 0;
}
