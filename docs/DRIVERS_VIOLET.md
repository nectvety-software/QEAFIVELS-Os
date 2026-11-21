# Violet stock-ROM driver audit and port plan

Target: Xiaomi Redmi Note 7 Pro (`violet`, SM6150). This audit uses the local
stock dump at `D:\QeafivelsOfficial\QeafdropOs\out\reference\stock-violet\`
and the extracted stock `fstab.qcom` / init configuration under `temp/`.

## Important boundary

The stock ROM contains a compiled Linux kernel, firmware blobs, and Android
vendor HAL binaries. It does not contain the C source for those drivers. A
vendor HAL `.so` is not a portable device driver: the inspected camera,
Bluetooth, and fingerprint libraries are AArch64 Android binaries linked to
Android libraries and services. QEAFIVELS' independent ARM64 kernel still has
no Violet boot handoff or device bus layer, so none of these components can be
claimed as a native Violet driver yet. `platform/violet/` remains the legacy
stock-kernel transport.

The corresponding open kernel source is available from
[Xiaomi's Violet kernel source](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/violet-q-oss).
It is source material for a QEAFIVELS port, not a binary module that can be
loaded into the independent kernel.

## ROM evidence and status

| Device function | What the stock ROM shows | What is still needed for a native port |
| --- | --- | --- |
| Wi-Fi | WCN3990 configuration is in `vendor/etc/wifi`; `/firmware/wlan/qca_cld/` points to that config and a per-device MAC file. The stock stack uses `wlan`/`cfg80211` and Qualcomm modem/WLFW services. | The dump omits the `modem` partition (`p82`, mounted by stock Android at `/vendor/firmware_mnt`) and `persist` (`p74`, MAC/calibration source). Existing bring-up logs still show no WLFW `0x45` service and no `wlan0`. A native port also needs the modem/remote-processor and WLAN transport layers. |
| Bluetooth | Stock `fstab.qcom` mounts the separate `bluetooth` partition (`p38`, VFAT, read-only) at `/vendor/bt_firmware`. `init.qcom.rc` references `hci_smd`, `hci_uart`, `bluetooth_power`, rfkill, and the serial clock. | The local dump omits `p38` and `p74`. Do not assume `hci_qca` is the Violet transport: the checked-in stock init config points to the SMD/UART and power-control path. A native port needs the board's SMD/shared-memory, power, and HCI layers plus its firmware/calibration. |
| Camera | `vendor/firmware/CAMERA_ICP.elf` is present (3,614,168 bytes). The vendor image also contains `camera.qcom.so` and the Android camera-provider service. | The provider is an Android HAL with QTI camera, QMI, metadata, and Android runtime dependencies. A native camera path still needs CSI/CSID, ISP, sensor, clocks/regulators, and a QEAFIVELS capture service. The ROM blob alone is not source code. |
| Fingerprint | `vendor/firmware/goodixfp.b00` through `.b04` and Goodix/FPC Android HAL libraries are present. Stock init grants access to `/dev/goodix_fp`; it also contains FPC-specific sysfs setup. | The actual sensor/controller variant and its bus must be confirmed on the target. FPC's HAL depends on Android HIDL/QSEECom; Goodix's HAL depends on Goodix Android libraries. Neither HAL is a native biometric driver. Secure matching/storage also needs a QEAFIVELS security design. |
| GPU | Adreno 612 firmware (`a612_zap.*`, `a612_rgmu.bin`) is present. | Firmware is only one input. The native OS needs its own GPU command submission, memory, interrupt, and rendering interfaces. |
| Audio / DSP | `tas2563_uCDSP.bin` and Qualcomm IPA firmware are present. | These are firmware payloads, not codec, AFE, DSP transport, or audio-service drivers. |
| Sensors / GNSS | The ROM starts Qualcomm sensor and GNSS vendor services. | Their Android/QMI/DSP service dependencies need native replacements and verified device interfaces. No native driver is present in this tree. |

The local stock dump includes `boot.img`, `system.img`, `vendor.img`, `cust.img`,
`dtbo.img`, and `vbmeta.img`; it does not include `bluetooth.img`, `modem.img`,
or `persist.img`. The vendor image's `vendor_dlkm` and `odm_dlkm` directories
contain build properties but no loadable driver modules. Driver source must come
from the matching kernel source tree or be newly implemented against verified
hardware interfaces.

## Work in this change

The legacy Violet boot now attempts a read-only mount of p38 at
`/vendor/bt_firmware` (VFAT, then short-name FAT fallback). Probes recognize
`hci_smd` and `bluetooth_power`, and treat files in that dedicated mount as
Bluetooth firmware. Bluetooth is still READY only when a real HCI device
exists. This is firmware discovery/bring-up groundwork, not a native
Bluetooth driver port.

WLAN startup now requires a mounted ext4 p74 and a parseable, unicast
`wlan_mac.bin` from that mount before either CNSS or the WLFW fallback may
start. Invalid/empty data blocks WLAN; no locally generated address is sent.
The WLFW fallback also stages any existing `wlfw_cal_00.bin`–`wlfw_cal_04.bin`
from p74 into tmpfs and reports only those real files; if none exist it waits
for calibration data from the running modem rather than inventing files.
RMTFS startup requires the four real modem NV partitions plus a kernel-provided
`qcom_rmtfs_mem1` device or a UIO device whose sysfs name is `rmtfs`. The
daemon still uses `-P -r`; the modem remoteproc is held if that service cannot
start. These checks remain unverified on hardware until p74/p82 and a target
connection are available.

REA CLI 6.5.0 was available, but its firmware-region operation could not run on
this Windows host because it requires Linux and a caller-supplied Binwalk.
Its ELF-layout provider also rejected these AArch64 vendor HALs because the
available profile is Linux x86-64 only. No extra analyzer or firmware engine
was installed. Ordinary `readelf` inspection confirmed the HAL architecture
and Android/QTI shared-library dependencies. Four small HAL copies used for
that inspection are staged under `build/violet/stock-rom-audit/` and are not
included in OS packages.

## Native port order

1. Finish and verify Violet's freestanding ARM64 boot handoff, memory map,
   interrupt controller, clocks, and device-tree parsing.
2. Start with one narrow device path and a real probe. Bluetooth is a candidate
   after the p38/p74 firmware and the stock SMD/UART/power topology are
   verified. Wi-Fi remains blocked on modem/WLFW. Camera and secure fingerprint
   require larger ISP and trusted-service work.
3. Port source algorithms into the QEAFIVELS driver model; do not copy Android
   HAL binaries into the OS image. Keep firmware external and record its
   source, version, and hash.
4. Mark a capability READY only after a native probe and a physical-device
   functional test. Until then it stays PRESENT, BLOCKED_FIRMWARE, or
   UNSUPPORTED as evidence permits.

## Live evidence and the native camera port (2026-11-21)

A read-only census through QEAFRESCUE changed the plan for camera. The running
stock kernel exposes `/sys/class/video4linux/` with `video0`, `video1`,
`video2`, `video32`, `video33` and 17 `v4l-subdev*` nodes. Their names identify
`cam-req-mgr`, `cam_sync` and `sde_rotator`; `video32/dev` is `81:32`, matching
major 81 (`video4linux`) in `/proc/devices`. The camera sensors and ISP are
therefore already probed by the kernel. There is no driver source to port from
the ROM for this path — the correct port is a native userspace V4L2 client.

Two concrete changes implement that port:

- `vio_create_devnodes()` now scans `/sys/class/video4linux` for `video[0-9]+`
  and creates the character nodes with the kernel-published major:minor.
  Before this, the image had no `/dev/video*` even though the drivers were
  live, because devtmpfs is disabled and our PID 1 owns `/dev`.
- `platform/violet/vqeaf_camera.c` provides a native V4L2 path: node-role
  classification, `VIDIOC_QUERYCAP` census, and a real single-frame capture
  (`REQBUFS`/`MMAP`/`QBUF`/`STREAMON`/`DQBUF`) with a frame hash. It uses only
  kernel UAPI — no vendor camera HAL, binder service or Android runtime.
  The rescue verbs `cam` and `cam grab [NODE]` expose it for device proof.

`camera` is now published as PRESENT when real capture candidates exist, never
READY from a node list. READY requires `cam grab` to return an actual frame;
the capture code intentionally reports honest errors otherwise.

Other subsystems, from the same live census:

- Bluetooth: `/sys/class/bluetooth` is empty and `/dev/mmcblk0p38` (the stock
  `bt_firmware` partition) is not consumed by a native HCI path. Firmware and
  the SMD/power topology still block it; no HAL port substitutes for that.
- Fingerprint: `/sys/class/misc` has no goodix/fpc node on this boot, so
  `fingerprint` stays UNSUPPORTED. A HAL `.so` cannot make a missing sensor
  node appear.
- Wi-Fi: `wlan` major 500 and `ieee80211` are live, `wlan0` is still absent;
  the blocker remains the modem/WLFW service and per-device calibration.
- Audio/GPU: `snd`, `dri` and `kgsl` classes exist; their firmware/config
  chain is a separate native-service problem, not a kernel-driver port.

## Subdev nodes and cam-req-mgr session (2026-11-21, live proof)

The reason no capture node exists is now precise: Qualcomm's camera blocks are
V4L2 **subdevices**, not `videoN` nodes. `vio_create_devnodes()` only scanned
`video*`; it now also creates `/dev/v4l-subdevN` from sysfs major:minor.

Live `cam subs` after flashing image `c6b5ec1f…` lists all 17 real nodes:
`cam-isp` (v4l-subdev1), `cam-icp` (14), `cam-sensor-driver` (7, 8, 9),
four `cam-csiphy-driver`, `cam-cpas`, `cam-jpeg`, `cam-lrme`, eeprom/actuator/
flash nodes. This matches the device set the openpilot Spectra camera path
opens natively — no vendor HAL is needed to reach them.

`cam session` issues `VIDIOC_CAM_CONTROL` (0xc01856c0) with the kernel UAPI
`struct cam_control` and opcode `CAM_REQ_MGR_CREATE_SESSION` (267). On device it
returned a real session handle `0x00530200`. The pure wrapper layout, opcode
and ioctl value are gated in `tests/vqeaf_violet_caps.c`.

A stream still does not exist: `cam` still reports `capture=0`. Creating one
requires acquiring/configuring the sensor, CSIPHY, ISP and ICP devices and
linking them in cam-req-mgr (the openpilot sequence), including sensor-specific
register arrays. That work is not claimed here.

## CAM_QUERY_CAP / CAM_ACQUIRE_DEV (2026-11-21, built, awaiting flash)

The next layer is now implemented but has **not yet been proved on device**:

- `cam caps` calls `CAM_QUERY_CAP` (257) on the real `cam-isp`, `cam-icp` and
  all `cam-sensor-driver` subdevs. Payload structs match the sm6250 kernel
  UAPI (`cam_isp_query_cap_cmd`, `cam_icp_query_cap_cmd`,
  `cam_sensor_query_cap`); the gate asserts opcodes and byte sizes.
- `cam link` creates a session, acquires the first sensor with no resource
  payload, acquires ISP with a minimal 4-lane DPHY RAW10 IFE input/RDI0 output
  resource, acquires ICP as BPS, then calls `CAM_REQ_MGR_LINK` (269). It
  releases devices and destroys the session afterwards. No buffers are
  allocated and no request is scheduled.
- The ISP RAW10 setup uses `CAM_FORMAT_MIPI_RAW_10 = 3` for `format` and MIPI
  DT `0x2a` for `dt`; these are different fields and both are gated.
- Any kernel errno is printed verbatim. A successful link alone would still
  not mark `camera` READY; only a real frame from `cam grab` does that.

Image `494fd872…` was flashed first and proved that the sysfs subdev names
were visible but `/dev/v4l-subdevN` did not exist: the DRM connector hyphen
filter in `scan_class()` also dropped `v4l-subdev*`. The corrected image
`0d34ef9f72b9b23bdbcc5c8e816baf71303d1b727b6f0b6465a0946225ba9092`
(32,108,544 B) is now on p77, with both raw-sector flash proofs.

Live kernel answers after the fix:

- `/dev/v4l-subdev0..16` exist.
- `cam caps`: ISP `CAM_QUERY_CAP` succeeds with IOMMU handles
  0x000062be/0x00053112, CDM 0x0004647c, `num_dev=2`, IFE version 1.7.0;
  three sensor subdevs answer with slot 0; ICP open fails errno 22.
- `cam link`: session 0x006a0200; ISP `CAM_ACQUIRE_DEV` succeeds with
  handle 0x00aa0100; sensor and ICP acquire fail errno 22;
  `CAM_REQ_MGR_LINK` fails errno 22 with the single acquired device.

No buffers were allocated and no request was scheduled, so `camera` remains
honest: `cam` still reports `capture=0`. The next blockers are the sensor/ICP
acquire payloads and device-specific configuration, not a missing node.
