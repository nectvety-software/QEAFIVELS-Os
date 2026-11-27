# Violet stock-ROM driver audit and port plan

Target: Xiaomi Redmi Note 7 Pro (`violet`, SM6150). This audit uses the local
stock dump at `D:\QeafivelsOfficial\QeafdropOs\out\reference\stock-violet\`
and the extracted stock `fstab.qcom` / init configuration under `temp/`.

## Important boundary

The stock ROM contains the compiled vendor Linux 4.14 kernel, firmware blobs
and Android vendor HAL binaries. Linux 4.14 is now the approved QEAFIVELS
kernel core; `platform/violet/` is the production Linux-core PID 1 path. The
ROM does not contain the C source for the kernel drivers. A vendor HAL `.so`
is not a portable native QEAF driver: the inspected camera, Bluetooth and
fingerprint libraries are AArch64 Android binaries linked to Android
libraries/services.

The corresponding open kernel source is available from
[Xiaomi's Violet kernel source](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/violet-q-oss).
Use matching source for reviewed Linux kernel changes; do not load vendor HAL
binaries or freestanding QEAF kernel modules into Linux userspace. Current
Violet packaging verifies binary kernel provenance because the exact vendor
source revision is not available locally.

## ROM evidence and status

| Device function | What the stock ROM shows | What is still needed for a native port |
| --- | --- | --- |
| Wi-Fi | WCN3990 configuration is in `vendor/etc/wifi`; `/firmware/wlan/qca_cld/` points to that config and a per-device MAC file. The stock stack uses `wlan`/`cfg80211` and Qualcomm modem/WLFW services. | The dump omits the `modem` partition (`p82`, mounted by stock Android at `/vendor/firmware_mnt`) and `persist` (`p74`, MAC/calibration source). Existing bring-up logs still show no WLFW `0x45` service and no `wlan0`. A native port also needs the modem/remote-processor and WLAN transport layers. |
| Bluetooth | Stock `fstab.qcom` mounts the separate `bluetooth` partition (`p38`, VFAT, read-only) at `/vendor/bt_firmware`. The device tree puts the controller behind **SLIMbus**: `/soc/bt_wcn3990` (`qca,wcn3990`, three `qca,bt-vdd-core/pa/ldo-supply` rails) and `/soc/slim@62e40000/wcn3990` (`qcom,btfmslim_slave`, elemental addresses `00 01 20 02 17 02` / `00 00 20 02 17 02`). | The local dump omits `p38` and `p74`. The SMD/UART references in `init.qcom.rc` are generic; the board tree says SLIMbus, so a native port needs a SLIMbus controller client, the three regulator rails, and HCI over the btfmslim logical channel, plus firmware/calibration. |
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

## Linux-core integration order

1. Verify the exact Linux kernel/config/DTB and firmware provenance. Keep the
   current stock kernel payload byte-for-byte unchanged until a reviewed
   kernel selector and reproducible source build exist.
2. Use Linux drivers and standard kernel interfaces first: DRM/KMS, evdev,
   V4L2, ALSA, netdev/cfg80211, HCI, input, power-supply and sysfs. QEAF
   services adapt these interfaces for apps; userspace never drives MMIO.
3. Treat the freestanding AArch64 display, SLIMbus and Bluetooth code below as
   reference/sequence material only. Port a missing delta into the matching
   Linux source as a proper driver/backport with DT bindings and lifecycle
   tests; never run it beside Linux.
4. Keep firmware external and record source, version and hash. Do not copy
   Android HAL binaries into the OS image.
5. Mark a capability READY only after a real Linux/QEAF provider succeeds and,
   for hardware support, a physical-device functional test passes. Until then
   it stays PRESENT, BLOCKED_FIRMWARE, DEGRADED or UNSUPPORTED as evidence
   permits.

## QEAF userspace driver registry (2026-11-26)

`drives/violet/violet_drivers.c` is the QEAF-owned provider registry for the
Linux-core Violet image. It does not replace a Linux driver and never accesses
MMIO, IRQs, DMA, clocks, regulators or reset registers. Linux remains the owner
of probe/remove and hardware lifecycle; the registry adapts the resulting
sysfs/dev nodes and QEAF runtime evidence into bounded capability reports.

The ABI-v1 registry has 19 fixed providers covering eMMC/UFS, DRM/display
refresh, touch/power input, USB gadget/host, Wi-Fi, Bluetooth, audio, GPU,
accelerometer, camera/V4L2, fingerprint, KVM, NFC and baseband. `drivers` in
QEAFRESCUE refreshes the read-only census and prints the Linux interface,
state, detail and blocker for every provider. DRM, touch, USB gadget and camera
require runtime proof (successful modeset/scanout, evdev bind, gadget bind or a
real captured frame) before READY. GPU, sensors, fingerprint and KVM stay at
most PRESENT from node discovery. Missing Wi-Fi/BT transport or firmware stays
BLOCKED_FIRMWARE; absent unsupported providers stay UNSUPPORTED.

Host coverage is in `tests/violet_drivers.c`, including ABI rejection, bounded
output, no-frame camera denial and firmware/interface state transitions. This
registry adds build and host evidence only; it does not change the hardware
blockers or claim a new physical test.

## Reviewed Axion backports (2026-11-26)

Axion's manifest points to `AxionAOSP-devices/android_kernel_xiaomi_violet`, not
the Android HAL repository. Its pinned `lineage-23.2` commit
`057098841e8bc82f87c3ae5fb11eb7c956416f15` is Linux 4.14.357-openela and is not
proven to match the running 4.14.215-azure-Pixel+ binary.

Three narrow fixes are staged under `platform/violet/kernel-patches/axion/`:
skip empty QoS-clock setup in `msm_bus`, clear DRM platform drvdata on bind
failure, and clear DSI ESD recovery only on an active-mode change. All three
pass `git apply --check` and apply cleanly to their files in MiCode commit
`1488fa3d05e2f9be5fc62e0bb16ad85d96db2076` (4.14.117). The provenance manifest
and `scripts/check_violet_axion_backports.py` pin hashes and enforce reviewed
paths/base files before application.

Provenance was re-verified on 2026-11-26 by fetching the three reference files
directly at Axion commit `057098841e8bc82f87c3ae5fb11eb7c956416f15`: all three
SHA-256 values match `provenance.json` (`5bad7df5…`, `55d2a74f…`, `635e8614…`)
and every patch postcondition is present in the Axion reference file. The
manifest check (`scripts/check_violet_axion_backports.py`) passes and the
`tests/test_violet_axion_backports.py` suite passes 5/5.

No kernel build, QEMU boot or physical test has run. The production kernel
payload remains the verified stock binary. These patches do not claim to fix
Wi-Fi firmware/QMI, Bluetooth SLIMbus, GNSS or fingerprint blockers, and no
Android HAL or proprietary vendor blob is imported.

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

## Bluetooth freestanding reference port from device-tree evidence (2026-11-24)

This section records the retained freestanding-driver reference track. It is
not the production Linux Bluetooth path and does not establish a Linux driver
or physical HCI readiness.

### Linux-core isolation (2026-11-25)

`platform/violet/bt_bringup.c` is now a read-only Linux evidence reporter for
`bt`, `bt diag`, `bt fw` and `slim`. The `bt up` verb explicitly refuses to
start a userspace radio path. `scripts/build_violet.py` no longer links
`wcn3990_bt.c`, `wcn3990_slim.c`, `slim_qcom_ngd.c` or the MMIO-capable
`slim_ngd_bringup.c` into the production PID 1. Those files remain host and
freestanding-reference code; Linux owns probe, MMIO, DMA, IRQ, regulators and
HCI. The dated entries below preserve the earlier reference implementation
history and do not describe the current production build.

The transport question is now answered by the reference device tree, not by the
generic `init.qcom.rc` text: violet's Bluetooth is `qca,wcn3990` whose HCI is
carried over the SLIMbus function `qcom,btfmslim_slave` at `/soc/slim@62e40000`.
The live ABL tree (`build/violet/violet-sm6150-abl.dtb`) does not include this
node, so the SLIMbus identity is reference-tree evidence and still has to be
measured on the handset.

Ported into the driver model, with the transport supplied by the platform:

| Layer | File | Role |
| --- | --- | --- |
| Contract | `kernel/include/vqeaf/wcn3990_bt.h` | `vqeaf_bt_ops_t` (rails, slim open/close, HCI read/write, firmware), DTB-derived rail voltages and elemental addresses |
| Driver | `kernel/drivers/bluetooth/wcn3990_bt.c` | fail-closed bring-up: BTFM.bin container check, rail order with rollback, HCI Reset `01 03 0C 00`, ready only on Command Complete `04 0E` `0x0C03` status 0 |
| Host gate | `tests/wcn3990_bt_smoke.c` + `vqeaf_wcn3990_bt_smoke` in `CMakeLists.txt` | 11 lifecycle checks, every failure branch unwinds what it enabled |
| Platform truth | `platform/violet/bt_bringup.c` | reads live `/proc/device-tree` nodes, p38 `BTFM.bin`, `/sys/class/bluetooth`; verb `bt` / `bt diag` / `bt fw` |
| Build | `scripts/build_violet.py` | `bt_bringup.c` compiled into the violet init image |

Status: driver logic and host gate PASS; platform report is read-only and
honest. Bluetooth stays BLOCKED: QEAFIVELS Kernel has no SLIMbus controller or
regulator layer yet, the stock kernel owns `slim@62e40000`, and `BTFM.bin` is
not in the local dump (only its manifest entry). No HCI transaction has been
attempted on the phone.

### Correction: the board source, not the reference blob (2026-11-24)

The numbers above were re-checked against the in-tree board source
`platform/violet/device-tree/sources/guaiyihu-sm6150/arch/arm64/boot/dts/qcom/sm6150.dtsi`.
`build/violet-reference.dtb` turned out to hold only 31 CPU/cache nodes and is
therefore not a source for this node at all; the driver and header now cite the
dtsi. Three values were wrong and are fixed:

| Property (sm6150.dtsi) | Value | Was |
| --- | --- | --- |
| `qca,bt-vdd-pa-voltage-level` | min **1304000** uV | 1300000 uV |
| `qca,bt-vdd-ldo-voltage-level` | min **3312000** uV | 3300000 uV |
| `qcom,btfm-slim-ifd-elemental-addr` | **00 00 20 02 17 02** | 00 02 00 02 17 02 |

The controller itself is in the same file: `slim@62e40000`,
`compatible = "qcom,slim-ngd"`, `reg` 0x62e40000+0x2c000 (`slimbus_physical`)
and 0x62e04000+0x20000 (`slimbus_bam_physical`), `interrupts = <0 291 0>,
<0 292 0>` (GIC SPI 291/292), `status = "ok"`, `qcom,iommu-s1-bypass`. That is
the register/BAM/IRQ set a native SLIMbus client would have to drive; nothing
in this tree drives it yet, so the bring-up contract is unchanged and the
capability stays BLOCKED.

The platform half now reads the live controller node and repeats both six-byte
elemental addresses (hex, straight from `/proc/device-tree`) in `bt` and
`bt diag` instead of printing compiled constants. The host gate asserts the
byte-exact addresses and the three rail voltages (1800000/1304000/3312000 uV,
1 uA each).

### SLIMbus client `qcom,slim-ngd` (2026-11-24)

The controller the Bluetooth function sits behind now has a driver in the
QEAFIVELS model, ported the same way as the WCN3990 Copy Engine: the register
block, BAM pipes, IRQ and DMA are injected, so the state machine is host-gated
without any platform code.

| Layer | File | Role |
| --- | --- | --- |
| Contract | `kernel/include/vqeaf/slim_qcom_ngd.h` | `VqeafSlimNgdDesc` + MMIO/BAM/IRQ/DMA ops; the violet base, BAM base, IRQ 291/292 and NGD offset as constants |
| Driver | `kernel/drivers/slimbus/slim_qcom_ngd.c` | rings, BAM bring-up, `INT_EN`/`RX_MSGQ_CFG`/`CFG` with read-back, interrupt status/ack, TX that needs `TX_MSG_SENT`, RX descriptor re-arm |
| Host gate | `tests/slim_qcom_ngd_smoke.c` + `vqeaf_slim_qcom_ngd_smoke` | 7 groups: contract, DT check, programming order, four start failures, TX ack, IRQ paths, RX pop |

Hardware facts: violet's `slim@62e40000` is `qcom,slim-ngd` with
`reg = <0x62e40000 0x2c000>, <0x62e04000 0x20000>` and
`interrupts = <0 291 0>, <0 292 0>`. The NGD block sits at `+0x1000`
(`CFG 0x00`, `STATUS 0x04`, `RX_MSGQ_CFG 0x08`, `INT_EN 0x10`, `INT_STAT 0x14`,
`INT_CLR 0x18`), messages are at most 40 bytes, and each direction has 32
descriptors. Those register and ring facts come from upstream
`drivers/slimbus/qcom-ngd-ctrl.c`; they have **not** been measured on this
handset, and `check_violet_dt()` only compares the device-tree numbers.

Bluetooth still does not move: no QEAFIVELS Kernel provider maps
`0x62e40000`, allocates BAM/DMA memory or requests IRQ 291/292, and the stock
kernel still owns the controller. The capability therefore stays BLOCKED; the
next step is that provider, then wiring `vqeaf_slim_ngd_xfer`/`rx_pop` to the
btfmslim channel.

### Violet provider and the btfmslim bridge (2026-11-24)

The client now has a Violet half and the Bluetooth driver has a real transport
behind `vqeaf_bt_ops_t`.

| Layer | File | Role |
| --- | --- | --- |
| Provider | `platform/violet/slim_ngd_bringup.c/.h` | fills the descriptor from `sm6150.dtsi`, checks the live `compatible`, maps controller (0x62e40000+0x2c000) and BAM (0x62e04000+0x20000) windows, forwards DMA/BAM/IRQ 291/292 through `VioSlimNgdHooks`, starts the client, reports `slim` |
| Bridge | `kernel/include/vqeaf/wcn3990_slim.h`, `kernel/drivers/bluetooth/wcn3990_slim.c` | `vqeaf_bt_slim_open/close/write/read` drive `vqeaf_slim_ngd_xfer`/`rx_pop`; `vqeaf_bt_slim_compose()` builds a complete `vqeaf_bt_ops_t` from the platform's rails/firmware plus this transport |
| Platform wiring | `platform/violet/bt_bringup.c` | `vio_bt_native_ops()` returns the composed contract only when the controller is UP; `bt up` is the single real attempt (reads and container-checks `BTFM.bin`, then stops at the rails the stock kernel owns) |
| Host gates | `tests/slim_ngd_bringup_smoke.c`, `tests/wcn3990_slim_smoke.c` | provider wiring (map addresses, IRQ 291, 32 RX descriptors, IRQ entry point) and the link (open only on UP, `-EMSGSIZE` over 40 B, `TX_MSG_SENT` wait, `rx_pop`, `-ENOSPC` without losing the message, full `vqeaf_bt_start` through the bridge) |

Fail-closed choices worth recording: `/dev/mem` is refused by default because
the stock kernel owns the block (opt in with `VIO_SLIM_NGD_DEVMEM=1`), a
missing DMA/BAM/IRQ backend surfaces as `-ENOSYS` at that exact step, and the
bridge neither adds nor strips the SLIMbus message header.

That last point is the remaining gap: the btfmslim channel header around an HCI
packet is not established by any evidence in this tree, so the bridge is a
message transport only. `vqeaf_bt_start()` therefore reaches the transport and
fails at the first received message with `-EPROTO`, exactly as
`vqeaf_wcn3990_slim_smoke` asserts; no test or code path claims `hci-ready`.
`BTFM.bin` is still absent from the local dump and no device was attached, so
nothing here is measured on the handset.


## Driver update verification (2026-11-26, host build)

The current QEAFIVELS production path remains the approved stock Linux
4.14.215 kernel plus the static AArch64 PID 1. Linux owns hardware probe,
MMIO, DMA, IRQ, clocks, regulators and device lifecycle; the QEAF registry
reports Linux providers and only promotes runtime-proven capabilities.

Verification on the Windows build host:

- `ctest --test-dir build-native --output-on-failure`: **32/32 PASS**.
  Coverage includes clipboard, Violet provider registry, SM6150 panel/DSI/
  display, GENI UART, WCN3990 Wi-Fi/CE/BT/SLIMbus, storage and system core.
- `scripts/wifi/run_hal_tests.py`: Wi-Fi HAL, QRTR/QMI, WLFW, firmware
  boot and firmware-container checks PASS.
- `scripts/wifi/run_violet_platform_tests.py`: PASS.
- Python suites for Axion backport provenance, package manifest and Violet
  Wi-Fi bring-up: **30/30 PASS**.
- `scripts/build_kernel_aarch64.py`: freestanding AArch64 ELF linked,
  798344 bytes; ELF64/AArch64 verified.
- `scripts/build_violet.py`: static PID 1 linked, 12624728 bytes,
  292 objects; ELF64/AArch64 verified. The 19-provider
  `drives/violet/violet_drivers.c` registry is part of this production
  PID 1 source list.
- The separate `libvqeaf-drives.a` archive contains 17 compiled
  AArch64 objects (303614 bytes): GENI I2C, SPMI/PMIC, regulator,
  touchscreen, SM6150 display clock, DSI packet/register/panel support,
  driver/device/firmware managers, FDT and QEAF driver API. It is a
  freestanding/reference deliverable and is intentionally **not** linked
  into Linux PID 1, which must not access controller MMIO.
- The packaged boot image is 51666944 bytes, SHA-256
  `0aaa8f0caf2d9e55b39693a753e4ef8a7c161357992d7507a896c90b2b61b5ce`.
  Structural and rescue gates PASS; manifest records
  `physical_boot_tested=false`.

Unresolved driver work is deliberately not represented as READY:
WCN3990 Wi-Fi still needs a working WLFW/modem service and real per-device
persist calibration/MAC; Bluetooth needs the production Linux SLIMbus/HCI
path and actual BTFM firmware; camera needs a successful V4L2 capture frame;
GPU/audio/sensors/fingerprint require their respective Linux providers and
functional tests. NFC and QEAF baseband providers are not implemented.
The separate freestanding DSI host/PHY reference files are not in the
17-object archive because their SM6150 header/register contract is
incomplete; do not invent offsets or promote them to production.

No physical-device test, fastboot boot, flash, erase or partition write was
performed during this verification because the handset was not enumerated
by ADB, Fastboot or QEAFRESCUE. The previous live-device census remains
historical evidence only and must be refreshed when Violet reconnects.
