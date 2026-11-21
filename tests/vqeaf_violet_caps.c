/* tests/vqeaf_violet_caps.c - host gate for the violet capability census.
 *
 * Feeds synthetic probes through the pure publish/report path and asserts the
 * honest state mapping, including BLOCKED_FIRMWARE when firmware is absent and
 * the zero->full transition. Runs on the host (no device, no LVGL). */
#include "vio_caps.h"
#include "vqeaf_camera.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;

static void expect_state(const char *name, VqeafCapabilityState want)
{
    VqeafCapability c;
    if (vqeaf_cap_get(name, &c) != 0) {
        printf("FAIL: %s missing from registry\n", name);
        fails++;
        return;
    }
    if (c.state != want) {
        printf("FAIL: %s state=%d want=%d\n", name, (int)c.state, (int)want);
        fails++;
    }
}

int main(void)
{
    /* 1. All-zero probe: nothing present, wifi/bt blocked on missing firmware. */
    vqeaf_cap_reset();
    VioHwProbe z;
    memset(&z, 0, sizeof(z));
    vio_caps_publish(&z);
    expect_state("storage.emmc", VQEAF_CAP_UNSUPPORTED);
    expect_state("storage.ufs", VQEAF_CAP_UNSUPPORTED);
    expect_state("usb.gadget", VQEAF_CAP_UNSUPPORTED);
    expect_state("display.drm", VQEAF_CAP_UNSUPPORTED);
    expect_state("display.refresh_high", VQEAF_CAP_UNSUPPORTED);
    expect_state("input.touch", VQEAF_CAP_UNSUPPORTED);
    expect_state("wifi", VQEAF_CAP_BLOCKED_FIRMWARE);
    expect_state("bluetooth", VQEAF_CAP_BLOCKED_FIRMWARE);
    expect_state("audio", VQEAF_CAP_UNSUPPORTED);
    expect_state("gpu", VQEAF_CAP_UNSUPPORTED);
    expect_state("sensor.accel", VQEAF_CAP_UNSUPPORTED);
    expect_state("kvm", VQEAF_CAP_UNSUPPORTED);
    expect_state("nfc", VQEAF_CAP_UNSUPPORTED);
    expect_state("camera", VQEAF_CAP_UNSUPPORTED);
    expect_state("camera.v4l2", VQEAF_CAP_UNSUPPORTED);
    expect_state("fingerprint", VQEAF_CAP_UNSUPPORTED);
    expect_state("baseband", VQEAF_CAP_UNSUPPORTED);

    /* 2. Violet-realistic probe: eMMC, 60Hz, Adreno kgsl, accel; no UFS, no
     *    high-refresh, no wlan iface, no sound cards, no KVM. */
    vqeaf_cap_reset();
    VioHwProbe v;
    memset(&v, 0, sizeof(v));
    v.mmcblk_present = 1;
    v.usb_udc_present = 1;
    v.drm_card_present = 1;
    v.panel_max_refresh = 60;
    v.touch_present = 1;
    v.kgsl_present = 1;
    v.accel_present = 1;
    vio_caps_publish(&v);
    expect_state("storage.emmc", VQEAF_CAP_READY);
    expect_state("storage.ufs", VQEAF_CAP_UNSUPPORTED);
    expect_state("usb.gadget", VQEAF_CAP_READY);
    expect_state("display.drm", VQEAF_CAP_READY);
    expect_state("display.refresh_high", VQEAF_CAP_UNSUPPORTED);
    expect_state("input.touch", VQEAF_CAP_READY);
    expect_state("gpu", VQEAF_CAP_READY);
    expect_state("sensor.accel", VQEAF_CAP_READY);
    expect_state("wifi", VQEAF_CAP_BLOCKED_FIRMWARE);
    expect_state("audio", VQEAF_CAP_UNSUPPORTED);
    expect_state("kvm", VQEAF_CAP_UNSUPPORTED);

    /* 3. Firmware present but iface/hci down => PRESENT, not READY. */
    vqeaf_cap_reset();
    VioHwProbe fw = v;
    fw.wlan_fw_present = 1;
    fw.bt_fw_present = 1;
    vio_caps_publish(&fw);
    expect_state("wifi", VQEAF_CAP_PRESENT);
    expect_state("bluetooth", VQEAF_CAP_PRESENT);

    /* 4. Full probe: every capability READY, high-refresh + UFS + KVM on. */
    vqeaf_cap_reset();
    VioHwProbe f;
    memset(&f, 0, sizeof(f));
    f.mmcblk_present = f.ufs_present = 1;
    f.usb_udc_present = f.usb_host_present = 1;
    f.drm_card_present = f.drm_render_present = 1;
    f.panel_max_refresh = 120;
    f.touch_present = 1;
    f.wlan_iface_present = f.wlan_fw_present = 1;
    f.bt_hci_present = f.bt_fw_present = 1;
    f.sound_cards = 2;
    f.kgsl_present = 1;
    f.accel_present = 1;
    f.kvm_present = 1;
    f.v4l2_video_nodes = 5;
    f.v4l2_capture_nodes = 2;
    vio_caps_publish(&f);
    expect_state("storage.ufs", VQEAF_CAP_READY);
    expect_state("usb.host", VQEAF_CAP_READY);
    expect_state("display.refresh_high", VQEAF_CAP_READY);
    expect_state("wifi", VQEAF_CAP_READY);
    expect_state("bluetooth", VQEAF_CAP_READY);
    expect_state("audio", VQEAF_CAP_READY);
    expect_state("kvm", VQEAF_CAP_READY);
    expect_state("camera", VQEAF_CAP_PRESENT);
    expect_state("camera.v4l2", VQEAF_CAP_PRESENT);

    /* 4b. V4L2 nodes exist but no stream candidate: blocked, not unsupported. */
    vqeaf_cap_reset();
    VioHwProbe onlyctl = f;
    onlyctl.v4l2_capture_nodes = 0;
    onlyctl.fp_node_present = 1;
    vio_caps_publish(&onlyctl);
    expect_state("camera", VQEAF_CAP_BLOCKED_FIRMWARE);
    expect_state("camera.v4l2", VQEAF_CAP_PRESENT);
    expect_state("fingerprint", VQEAF_CAP_PRESENT);

    /* 5. Transition: re-publish zero over full clears stale READY states. */
    vio_caps_publish(&z);
    expect_state("wifi", VQEAF_CAP_BLOCKED_FIRMWARE);
    expect_state("kvm", VQEAF_CAP_UNSUPPORTED);
    expect_state("audio", VQEAF_CAP_UNSUPPORTED);
    expect_state("storage.ufs", VQEAF_CAP_UNSUPPORTED);
    expect_state("camera", VQEAF_CAP_UNSUPPORTED);
    expect_state("fingerprint", VQEAF_CAP_UNSUPPORTED);

    /* 6. Report renders every name with its honest state text. */
    vqeaf_cap_reset();
    vio_caps_publish(&v);
    char buf[2048];
    int n = vio_caps_report(buf, (int)sizeof(buf));
    if (n <= 0) { printf("FAIL: empty report\n"); fails++; }
    const char *need[] = { "storage.emmc", "storage.ufs", "display.refresh_high",
                           "wifi", "bluetooth", "audio", "gpu", "sensor.accel",
                           "kvm", "nfc", "camera", "camera.v4l2",
                           "fingerprint", "baseband",
                           "READY", "UNSUPPORTED", "BLOCKED_FIRMWARE" };
    for (unsigned i = 0; i < sizeof(need) / sizeof(need[0]); i++) {
        if (!strstr(buf, need[i])) {
            printf("FAIL: report missing '%s'\n", need[i]);
            fails++;
        }
    }

    /* 7. Host probe is unavailable and must not fabricate signals. */
    VioHwProbe hp;
    memset(&hp, 0xff, sizeof(hp));
    if (vio_caps_probe_device(&hp) == 0) {
        printf("FAIL: host probe unexpectedly succeeded\n");
        fails++;
    }
    if (hp.mmcblk_present != 0 || hp.wlan_fw_present != 0 ||
        hp.v4l2_capture_nodes != 0 || hp.fp_node_present != 0) {
        printf("FAIL: host probe left non-zero signals\n");
        fails++;
    }

    /* 8. Native V4L2 role classification never mistakes control/display
     *    nodes for a camera stream, and never invents a stream node. */
    struct { const char *name; VqeafV4l2Role want; } roles[] = {
        { "video0", VQEAF_V4L2_ROLE_CAPTURE },
        { "video33", VQEAF_V4L2_ROLE_CAPTURE },
        { "cam-req-mgr", VQEAF_V4L2_ROLE_CONTROL },
        { "cam_sync", VQEAF_V4L2_ROLE_CONTROL },
        { "sde_rotator", VQEAF_V4L2_ROLE_ROTATOR },
        { "msm_vidc_vdec", VQEAF_V4L2_ROLE_CODEC },
        { "msm_vidc_venc", VQEAF_V4L2_ROLE_CODEC },
        { "v4l-subdev0", VQEAF_V4L2_ROLE_OTHER },
        { "videoX", VQEAF_V4L2_ROLE_OTHER },
        { "video", VQEAF_V4L2_ROLE_OTHER },
    };
    for (unsigned i = 0; i < sizeof(roles) / sizeof(roles[0]); i++) {
        VqeafV4l2Role got = vqeaf_v4l2_role(roles[i].name);
        if (got != roles[i].want) {
            printf("FAIL: role(%s)=%s want %s\n", roles[i].name,
                   vqeaf_v4l2_role_name(got),
                   vqeaf_v4l2_role_name(roles[i].want));
            fails++;
        }
    }
    VqeafCameraNode cn[2];
    memset(cn, 0, sizeof(cn));
    snprintf(cn[0].node, sizeof(cn[0].node), "/dev/video32");
    cn[0].role = VQEAF_V4L2_ROLE_CAPTURE;
    snprintf(cn[0].driver, sizeof(cn[0].driver), "cam_req_mgr");
    cn[1].node[0] = '\0';
    char creport[1024];
    int cr = vqeaf_camera_report(cn, 2, creport, (int)sizeof(creport));
    cn[0].role = VQEAF_V4L2_ROLE_CAPTURE;
    cn[1].role = VQEAF_V4L2_ROLE_CODEC;
    cr = vqeaf_camera_report(cn, 2, creport, (int)sizeof(creport));
    if (cr <= 0 || !strstr(creport, "capture=1") ||
        !strstr(creport, "codec=1") || !strstr(creport, "cam_req_mgr")) {
        printf("FAIL: camera report missing capture census\n");
        fails++;
    }

    /* 9. Qualcomm cam-req-mgr UAPI wrapper: opcode/ioctl/struct layout must
     *    match the kernel header before any device ioctl is attempted. */
    if (VQEAF_CAM_CONTROL_IOCTL != 0xc01856c0u ||
        VQEAF_CAM_CREATE_SESSION != 267 ||
        VQEAF_CAM_DESTROY_SESSION != 268) {
        printf("FAIL: cam-req-mgr UAPI constants mismatch\n");
        fails++;
    }
    {
        int32_t payload[2] = {0, 0};
        VqeafCamControl ctl;
        vqeaf_cam_control_init(&ctl, VQEAF_CAM_CREATE_SESSION, payload, 8);
        if (ctl.op_code != VQEAF_CAM_CREATE_SESSION || ctl.size != 8 ||
            ctl.handle_type != VQEAF_CAM_HANDLE_USER_PTR ||
            ctl.handle != (uint64_t)(uintptr_t)payload || ctl.reserved != 0) {
            printf("FAIL: cam_control wrapper layout mismatch\n");
            fails++;
        }
        if (sizeof(ctl) != 24) {
            printf("FAIL: cam_control size=%zu want 24\n", sizeof(ctl));
            fails++;
        }
    }

    /* 10. CAM_QUERY_CAP / CAM_ACQUIRE_DEV / CAM_REQ_MGR_LINK payload layouts
     *     must match the Qualcomm UAPI before any device ioctl is attempted. */
    if (VQEAF_CAM_QUERY_CAP != 257 || VQEAF_CAM_ACQUIRE_DEV != 258 ||
        VQEAF_CAM_RELEASE_DEV != 262 || VQEAF_CAM_LINK != 269 ||
        VQEAF_CAM_ISP_HW_MAX != 5 || VQEAF_CAM_ISP_RES_ID_PORT != 0 ||
        VQEAF_CAM_ISP_LANE_TYPE_DPHY != 0 ||
        VQEAF_CAM_ISP_IFE_IN_RES_PHY_0 != 0x4001u ||
        VQEAF_CAM_ISP_IFE_OUT_RES_RDI_0 != 0x3006u ||
        VQEAF_CAM_FORMAT_MIPI_RAW_10 != 3 ||
        VQEAF_CAM_MIPI_DT_RAW10 != 0x2au ||
        VQEAF_CAM_ICP_RES_TYPE_BPS != 1 ||
        VQEAF_CAM_ICP_DEV_TYPE_BPS != 3) {
        printf("FAIL: camera UAPI opcode/constant mismatch\n");
        fails++;
    }
    {
        struct { size_t got, want; const char *name; } sizes[] = {
            { sizeof(VqeafCamQueryCap), 16, "cam_query_cap_cmd" },
            { sizeof(VqeafCamIspCaps), 144, "cam_isp_query_cap_cmd" },
            { sizeof(VqeafCamIcpCaps), 176, "cam_icp_query_cap_cmd" },
            { sizeof(VqeafCamSensorCaps), 40, "cam_sensor_query_cap" },
            { sizeof(VqeafCamAcquire), 24, "cam_acquire_dev_cmd" },
            { sizeof(VqeafCamLink), 268, "cam_req_mgr_link_info" },
            { sizeof(VqeafCamIspResource), 24, "cam_isp_resource" },
            { sizeof(VqeafCamIspInPort), 128, "cam_isp_in_port_info" },
            { sizeof(VqeafCamIcpAcquireInfo), 60, "cam_icp_acquire_dev_info" },
            { sizeof(VqeafCamRelease), 8, "cam_release_dev_cmd" },
        };
        for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            if (sizes[i].got != sizes[i].want) {
                printf("FAIL: %s size=%zu want=%zu\n", sizes[i].name,
                       sizes[i].got, sizes[i].want);
                fails++;
            }
        }
    }
    {
        char hbuf[128];
        if (vqeaf_camera_query_caps(hbuf, (int)sizeof(hbuf)) >= 0 ||
            vqeaf_camera_acquire_link(hbuf, (int)sizeof(hbuf)) >= 0) {
            printf("FAIL: host fallback fabricated camera ioctl success\n");
            fails++;
        }
    }

    if (fails) {
        printf("vqeaf_violet_caps: %d FAIL\n", fails);
        return 1;
    }
    printf("vqeaf_violet_caps: ALL OK (11 groups)\n");
    return 0;
}