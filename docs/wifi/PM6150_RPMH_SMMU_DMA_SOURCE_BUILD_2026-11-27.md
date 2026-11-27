# Violet PM6150 / RPMh / SMMU / DMA source build report — 2026-11-27

## Result summary

- Source-tree kernel build: **PASS**.
- Kernel release: \`4.14.215-azure-Pixel+\`.
- Violet board DTB: byte-for-byte identical to the DTB appended to the stock Violet boot reference.
- Boot-image structure: **PASS**.
- Rescue-channel audit: **PASS**.
- Fastboot RAM-boot command: bootloader returned \`Sending OKAY\` and \`Booting OKAY\`.
- QEAF PID 1 / initramfs runtime: **NOT CONFIRMED**. After USB re-enumeration, ADB reported Android 13 userspace.
- WLAN-PD and WLFW: **NOT STARTED**.
- Partition writes: **0**. No flash, erase, format, or persistent calibration writes were issued.

> Update: later read-only ADB evidence identified a bootloader-added `skip_initramfs` argument as the reason QEAF PID 1 did not run. The tag-gated correction and its unsuccessful follow-up RAM boot are documented in `RAMBOOT_INITRAMFS_SKIP_ROOTCAUSE_2026-11-27.md` and `INITRDFIX_RAMBOOT_ATTEMPT_2026-11-27.md`.

## Kernel source and configuration

Repository: \`PixelExperience-Devices/kernel_xiaomi_violet\`

Branch: \`thirteen\`

Commit: \`a8547758e03dc26a3a587a0661f17b25289e743e\`

Build tree: WSL \`/root/src/kernel-violet-4.14.215\`

Output tree: WSL \`/root/build-violet-4.14.215/perf-config\`

The final configuration was derived from the .config embedded in the stock Violet kernel, resolved with the source tree's Kconfig, and adjusted only to disable \`CONFIG_COMPAT_VDSO\` (stock already has it disabled). 32-bit compat syscalls remain enabled.

Provider symbols confirmed in the final configuration:

- \`CONFIG_SPMI_MSM_PMIC_ARB=y\`
- \`CONFIG_REGULATOR_RPMH=y\`, \`CONFIG_QTI_RPMH_API=y\`
- \`CONFIG_MSM_CLK_RPMH=y\`, \`CONFIG_MSM_GCC_SM6150=y\`
- \`CONFIG_ICNSS=y\`, \`CONFIG_ICNSS_QMI=y\`
- \`CONFIG_ARM_SMMU=y\`, \`CONFIG_IOMMU_DMA=y\`, \`CONFIG_ARM64_DMA_USE_IOMMU=y\`
- \`CONFIG_DMA_CMA=y\`, \`CONFIG_QRTR_SMD=y\`
- \`# CONFIG_CNSS2 is not set\`

The source compiled the relevant kernel objects, including RPMh regulator/API, SM6150 GCC, SPMI arbiter, ARM SMMU, ICNSS, ICNSS QMI, WLFW service interface, and QCA CLD SNOC/CE code. Compilation is not runtime proof. GCC 13 emitted warnings in legacy Qualcomm/QCA sources (format/type mismatches, maybe-uninitialized paths, and RWX linker segment warning); these remain review items, not silently suppressed.

## Native provider sequencing

The implementation is Linux-owned; PID 1 does not implement PMIC MMIO, clock register writes, IOMMU page tables, or DMA allocation.

The source ICNSS driver contains the regulator enable/rollback path, XO reference-clock enable path, SMMU domain creation/attach/detach, and CE IRQ request/free paths. The Violet DTB supplies the concrete resources and the ICNSS stream/IOVA windows. Linux DMA API + ARM64 DMA-IOMMU + CMA own coherent allocations and mappings.

The ICNSS rail requests represented by this source are:

- \`vdd-cx-mx\`: 752000 µV
- \`vdd-1.8-xo\`: 1800000 µV
- \`vdd-1.3-rfa\`: 1304000 µV
- \`vdd-3.3-ch0\`: 3312000 µV
- Clock: \`cxo_ref_clk_pin\`

These are source-driver requests and DT contracts, not measurements of regulator output on the physical phone.

## Device-tree and SMMU identity

The source-built \`violet-sm6150.dtb\` and the stock boot's appended DTB are both 678082 bytes and have the same SHA-256:

\`704bfc191b7ca51547cb639132d71a55fc7523431a5e94dab31e55d056b0f959\`

Important stock-matched ICNSS properties:

- MSA fixed region: \`0x93300000 + 0x200000\`
- SMMU stream ID: \`0x1640\`
- IOVA base/window: \`0xa0000000 + 0x10000000\`
- ICNSS register base: \`0x18800000\`, size \`0x800000\`
- IPA address window node: \`0xb0000000 + 0x10000\`
- DT requests S1 bypass for the ICNSS mapping.

The stale MSA node unit-address text is not authoritative; the actual DT \`reg\` property is. The \`reg\` value matches stock.

## Build artifacts and boot ABI

Source kernel target: \`Image.gz-dtb\`

Uncompressed ARM64 Image size: 38199304 bytes.

ARM64 Image magic: \`ARMd\`.

ARM64 \`text_offset\` and \`flags\` match the stock kernel header values.

Boot image header copied from the stock Violet reference:

- Page size: 4096
- Header version: 1
- Kernel load address: \`0x00008000\`
- Ramdisk load address: \`0x01000000\`
- Tags address: \`0x00000100\`
- Command line retains the stock Violet base and appends \`init=/init rdinit=/init\` plus a diagnostic build tag.

Source RAM-boot image:

\`D:\\QEAFIVELS-Os\\dist\\boot-vqeaf-violet-source.img\`

Size: 49029120 bytes (below the 67108864-byte p77 capacity).

SHA-256: \`f5fd916ff0ff263ec694b411483ebc6028c1ce2194158a2de8c35af48a5e865d\`

Manifest:

\`D:\\QEAFIVELS-Os\\dist\\boot-vqeaf-violet-source.manifest.json\`

Host structural validation and rescue-channel audit both passed.

## WLAN-PD fail-closed guard

PID 1 now refuses to start the modem subsystem unless both conditions are true:

1. \`/etc/qeaf/wlan-pd-provider-verified\` exists in the RAM root.
2. \`/proc/sys/kernel/osrelease\` equals \`4.14.215-azure-Pixel+\` exactly.

The marker is intentionally not packaged. Its existence is a release gate, not a cryptographic proof. The source image contains the guard code but not the marker. The policy is centralized in the host-service layer and checked before rescue/host-service WLAN mutations as well as before trace arming and modem opt-in. At the time of this initial report, 16/16 host contract tests passed; later suite totals are release evidence, not retroactive evidence for this image.

## Physical RAM-boot observation

One command was issued:

\`fastboot boot D:\\QEAFIVELS-Os\\dist\\boot-vqeaf-violet-source.img\`

Fastboot returned:

\`Sending 'boot.img' OKAY\`
\`Booting OKAY\`

After USB re-enumeration, the host observed an ADB device. Read-only ADB evidence showed:

- Fingerprint: \`Xiaomi/violet/violet:13/TQ3A.230901.001.B1/0819:userdebug/release-keys\`
- PID 1 comm: \`init\`
- Root mount: \`/dev/root / ext4\`
- Boot reason property: \`bootloader\`
- \`/proc/cmdline\` and \`/proc/sys/kernel/osrelease\` were denied to the ADB shell.
- \`wlan0\` was present in that Android userspace.

Therefore the bootloader accepted the RAM image, but **QEAF initramfs/PID 1 was not confirmed**. The Android fingerprint, mount table, and PID 1 show the device returned to an Android 13 userspace. The \`wlan0\` listing belongs to that userspace and is not a QEAF or WLFW test result. The host saw a generic USB mass-storage device with no media; this does not prove QEAF's rescue gadget was bound.

The initial attempt did not retain a kernel log, pstore console, or UART trace, so it did not establish that the Linux kernel reached QEAF PID 1. Later read-only ADB evidence identified the bootloader-added `skip_initramfs` argument; the corrected candidate and the later RAM-trace candidate still did not provide verifiable QEAF runtime.

## Safety state

- p74/p76/p77: no writes performed in this work.
- No partition flash/erase/format.
- No modem subsystem start.
- No WLAN-PD start.
- No WLFW handshake test.
- No calibration/MAC staging or changes.

## Current gate after the follow-up attempts

The static AArch64 \`/init\` contract and actual command line were subsequently checked, and the tagged \`skip_initramfs\` correction was booted once. That corrected candidate and the later RAM-trace candidate still did not confirm QEAF PID 1. Do not boot another candidate until retained pstore/ramoops or UART explains the failed handoff. Keep the WLAN-PD marker absent until the QEAF boot path is proven and the CE map/SMMU runtime state is inspected.

Only after QEAF PID 1 and the provider probe/resource state are confirmed should a separate, explicitly gated WLAN-PD/WLFW experiment be considered.
