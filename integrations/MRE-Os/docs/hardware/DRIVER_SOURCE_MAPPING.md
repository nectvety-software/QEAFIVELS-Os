# Driver Source Mapping — Violet

Inspection date: 2026-11-23. The repository commits below are the exact shallow-clone HEADs inspected during this run.

| Reference repository | Commit | Findings |
|---|---|---|
| GuaiYiHu/android_kernel_xiaomi_sm6150 | 54dd13a140754a865867b38000ae7ae96fc43ce4 | SM6150 generic DTS, Violet performance defconfig, Qualcomm platform drivers |
| Xiaomi-Redmi-Note-7-Pro/android_device_xiaomi_violet | 541d7057cdb84658dafacc17afee3756314d5f4c | BoardConfig, Wi-Fi configuration, Android product/HAL declarations and proprietary file manifest |
| mavji7/android_device_xiaomi_violet | e85fad6b103a4f9056c411de8bde50a7842d6224 | Alternate BoardConfig, same WCNSS config family and proprietary file manifest |
| RedmiNote7Pro/device_xiaomi_violet | 8e08dc7ddebe93698fc9685781c2f8e2a9a3a014 | Alternate BoardConfig, Android device config, prebuilt dtbo.img and Image.gz-dtb |

## Confirmed source paths

Kernel repository:
- `arch/arm64/boot/dts/qcom/sm6150.dtsi`
- `arch/arm64/boot/dts/qcom/sm6150-gpu.dtsi`
- `arch/arm64/boot/dts/qcom/sm6150-pinctrl.dtsi`
- `arch/arm64/boot/dts/qcom/sm6150-usb.dtsi`
- `arch/arm64/boot/dts/qcom/sm6150-audio.dtsi`
- `arch/arm64/boot/dts/qcom/sm6150-camera.dtsi`
- `arch/arm64/boot/dts/qcom/sm6150-pm.dtsi`
- `arch/arm64/boot/dts/qcom/sm6150-qupv3.dtsi`
- `arch/arm64/boot/dts/qcom/msm-arm-smmu-sm6150.dtsi`
- `arch/arm64/configs/vendor/violet-perf_defconfig`
- `drivers/clk/qcom/clk-virt-sm6150.c`, `drivers/clk/qcom/dispcc-sm6150.c`
- `drivers/mmc/host/sdhci-msm.c`
- `drivers/net/wireless/cnss2/`, `drivers/soc/qcom/icnss*`
- `drivers/remoteproc/qcom_wcnss.c`
- `drivers/gpu/msm/` (KGSL / Adreno family)
- `drivers/i2c/busses/i2c-msm-v2.c`

Device tree repositories:
- `BoardConfig.mk`, `device.mk`, `proprietary-files.txt`
- `wifi/WCNSS_qcom_cfg.ini`
- `rootdir/`, `audio/`, `bluetooth/`, `gps/`, `fingerprint/`, `manifest.xml`

## Cross-repository findings and cautions

- The Xiaomi BoardConfig sets ARM64, board platform SM6150, GPU qcom-adreno612, boot header version 1, kernel image Image.gz-dtb, 4096-byte kernel pages, kernel source kernel/xiaomi/sm6150, config vendor/violet-perf_defconfig, WLAN qcwcn and WIFI_DRIVER_DEFAULT qca_cld3.
- The Mavji7 BoardConfig instead points TARGET_KERNEL_SOURCE to kernel/xiaomi/violet. The third BoardConfig uses armv8-2a while the first two use armv8-a. Resolve these differences against the exact kernel source and target handset before creating an MRE CPU target.
- Xiaomi BoardConfig contains an Android kernel command line with earlycon=msm_geni_serial,0x880000 and androidboot.console=ttyMSM0. This is a reference clue only; it is not yet a verified MRE UART mapping or boot handoff.
- All three device repositories contain Android build/HAL configuration and proprietary-files manifests. These are not MRE runtime inputs.
- The WCNSS_qcom_cfg.ini files contain QCA WLAN tuning values such as IMPS/BMPS, 2x2 and offload controls. These are vendor-driver configuration semantics, not a portable Wi-Fi driver.
- The kernel's generic SM6150 DTS has an embedded UFS alias, while the Violet device README identifies eMMC 5.1. Do not apply generic SoC storage nodes as the actual Violet board wiring.
- No Violet-specific source DTS was found in the inspected kernel tree listing. The third device tree contains prebuilt dtbo.img and Image.gz-dtb binaries; they are opaque references and are not copied into the MRE source tree.
- Windows checkout of the kernel worktree was blocked by reserved Windows filenames (including paths ending in aux.c / aux.h). Git metadata and tree inspection work; selected source can be read using git show without checking out the entire tree.

## Driver classification

| Subsystem | Evidence | Classification / MRE work |
|---|---|---|
| SM6150 clocks | clk-virt-sm6150.c, dispcc-sm6150.c | Linux-specific; reimplement register/clock protocol behind MRE clock API |
| GPU | sm6150-gpu.dtsi, drivers/gpu/msm KGSL | Linux-specific + secure firmware; new MRE GPU subsystem required |
| Wi-Fi | qcwcn, qca_cld3, CNSS2, ICNSS, QMI sources | Linux-specific transport/control; reimplement MRE bus, firmware and QMI stack |
| Display | SDE/DSI generic DTS and panel candidate files | Exact Violet panel remains UNKNOWN; port only after DTB identification |
| eMMC | Device README + sdhci-msm driver family | Linux-specific; verify actual controller/DTB and write a native MRE block driver |
| USB | sm6150-usb.dtsi, DWC3/QUSB2 PHY nodes | Linux-specific; identify exact role-switch/PHY wiring first |
| Audio | SM6150 audio DTS + proprietary ACDB/ADSP files | Vendor DSP dependency; native MRE audio service and firmware review required |
| Camera | camera DTS and numerous proprietary sensor modules | Board sensor inventory and calibration remain UNKNOWN |
| Modem | QMI/QRTR vendor components in manifests | Protocol/transport work required; do not reuse Android daemons |

## Required per-driver record

For each candidate, record repository URL, exact commit, source path, compatible string, bus, registers/IRQ, firmware dependency, Linux/Android dependencies, license, MRE API, build status, unit-test result and real-device evidence. Do not port a Linux driver by renaming symbols.

## 2026-11-23 DTB identity audit

The only available prebuilt kernel/DTB pair in the three inspected device-tree repositories is under device_xiaomi_violet_redminote7pro/prebuilt. Its appended FDT root compatible list is qcom,sm6150-idp, qcom,sm6150, qcom,idp. It is a generic Qualcomm SM6150 IDP reference, not an authenticated Violet board DTB.

The accompanying dtbo.img has one entry (ID 0). Its available overlay does not establish a Violet root identity. Therefore the extracted GIC/UART/timer addresses are SoC-level candidates only. They are not authorization to initialize Violet hardware.

The MRE FDT parser now reads the FDT memory reservation map as well as /reserved-memory. On this IDP reference it finds 20 fixed reserved-memory ranges and 7 children without fixed reg placement. The /memory reg property is zero-sized. The MRE platform matcher sets board_identity_verified=0 and refuses MMU preparation.

A live Violet DTB (or exact stock Violet boot.img plus matching DTBO) is still required. Do not package or boot the IDP DTB as if it were Violet.
