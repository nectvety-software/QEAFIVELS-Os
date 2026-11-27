# Violet QEAF RAM-boot initramfs skip: root cause and tagged fix — 2026-11-27

## Executive result

The source-built 4.14.215 kernel did boot on the physical Violet during the previous RAM-boot attempt. The bootloader also passed the candidate ramdisk at the runtime FDT address and exact byte length recorded below. QEAF PID 1 did not run because the final kernel command line contained `skip_initramfs`. In this kernel, that parameter causes `populate_rootfs()` to free the supplied initrd and switch to `default_rootfs()`, after which Android's `/system/bin/init` becomes PID 1.

A tag-gated kernel correction was applied, rebuilt, and packaged as a new, separate RAM-boot image. That candidate was subsequently booted once, but QEAF runtime was not verifiable over USB; see `INITRDFIX_RAMBOOT_ATTEMPT_2026-11-27.md`. No partition write, flash, erase, or format was performed.

## Evidence from the physical device

Read-only diagnostics were obtained from the running phone using ADB root access. ADB root was used to read `/proc`, `dmesg`, and pstore; no diagnostic files were written to device storage.

Current running kernel:

- Release: `4.14.215-azure-Pixel+`
- Build: `#1 SMP PREEMPT Fri Nov 27 12:04:53 +07 2026`
- The live command line contains `androidboot.qeaf_ramboot_probe=violet-source-4.14.215`, identifying the source-built candidate boot.
- The final command line also contains `skip_initramfs rootwait ro init=/init`.
- PID 1 executable: `/system/bin/init`; PID 1 command: `/system/bin/init second_stage`.
- Root is mounted from `/dev/root` as ext4, read-only.

The stock Violet boot header command line and the stock DTB `/chosen/bootargs` do not contain `skip_initramfs`. The running device's final `/chosen/bootargs` does contain it. The parameter is therefore added after the stock header/DTB command-line inputs are combined by the boot firmware/loader path. The exact internal bootloader component that appends it has not been identified.

The live FDT exposes:

- `linux,initrd-start = 0x8365e000`
- `linux,initrd-end = 0x856fec2b`
- Difference: `0x020a0c2b` = 34,212,907 bytes.

That difference exactly matches the candidate ramdisk's compressed byte length (34,212,907 bytes). The ramdisk was therefore passed to the running kernel; it was not absent or truncated. The observed start address is runtime FDT evidence, not the boot header's ramdisk load address (`0x01000000`); the bootloader relocation behavior between those values was not established.

Current kernel log contains no initramfs unpack message for this boot. It instead contains:

- `VFS: Mounted root (ext4 filesystem) readonly`
- `Freeing unused kernel memory: 4096K`

Kernel source `init/initramfs.c` confirms that `skip_initramfs` sets `do_skip_initramfs`; `populate_rootfs()` then calls `free_initrd()` and `default_rootfs()`. The flags `force_initramfs` and `prefer_initramfs` are not registered handlers in this source tree and therefore did not override the recognized `skip_initramfs` option.

### pstore and UART scope

Pstore is mounted and accessible with root ADB. The retained `console-ramoops-0` was pulled to `build/console-ramoops-0.txt`; it belongs to an earlier kernel boot carrying tag `androidboot.qeaf_ramboot_probe=violet-v3`, not the latest source image. It must not be attributed to the source-kernel attempt.

For the latest attempt, the live `/proc/cmdline`, FDT chosen properties, `uname`, and `dmesg` provide the evidence above. No UART serial port was enumerated on the build host, so no independent USB-UART capture was obtained.

## /init ABI validation

The supplied CPIO `./init` entry was inspected:

- CPIO mode: `0100777` (regular executable file)
- File size: 12,786,888 bytes
- ELF class/data: ELF64, little-endian
- ELF type: ET_EXEC (2)
- Machine: AArch64 (183)
- Entry: `0x145a490`
- Program headers: 9
- PT_LOAD segments: 4
- Entry point lies inside an executable PT_LOAD segment
- No PT_INTERP segment; the binary is static

The package builder now validates these properties and rejects a malformed /init before emitting a candidate image.

## Tag-gated correction

Kernel source modified: `init/initramfs.c`.

The added `__setup("qeaf_ramboot_probe=", ...)` handler activates only when the exact token is:

```
qeaf_ramboot_probe=violet-source-4.14.215
```

When both `skip_initramfs` and that exact QEAF diagnostic tag are present, the kernel logs:

```
QEAF: tagged Violet RAM boot overrides skip_initramfs; unpacking supplied initrd
```

and continues into the normal initramfs unpack path. Without the QEAF tag, the original `skip_initramfs` branch is unchanged. This avoids changing the normal Android boot policy for unrelated boots.

The rebuilt `vmlinux` contains:

- `qeaf_ramboot_probe_param`
- `__setup_qeaf_ramboot_probe_param`
- `__setup_str_qeaf_ramboot_probe_param`
- `populate_rootfs`

The compiled ARM64 image contains the diagnostic override log string and the parameter key. This confirms compile/link inclusion, not runtime behavior.

Patch:

`patches/violet/0001-qeaf-ramboot-prioritize-initramfs.patch`

Patch SHA-256:

`32d4880bc290eb75d573df1152800121e594a11d080f1a77860df195d455ee85`

## New candidate image

`dist/boot-vqeaf-violet-source-initrdfix.img`

- Size: 49,029,120 bytes
- SHA-256: `d9839a7d09f450b684a66cb07ca1f2c3c823d86a8c8eace767cbe1f05e7b03c7`
- Header: Android boot header v1, page size 4096
- Kernel address: `0x00008000`
- Ramdisk address: `0x01000000`
- Kernel release: `4.14.215-azure-Pixel+`
- DTB: 678,082 bytes; SHA-256 `704bfc191b7ca51547cb639132d71a55fc7523431a5e94dab31e55d056b0f959`, byte-for-byte equal to the stock Violet DTB.
- Header command line includes `init=/init`, `rdinit=/init`, Android diagnostic tag and the plain `qeaf_ramboot_probe=...` kernel setup token.
- Header command line does not include `skip_initramfs`, `force_initramfs`, or `prefer_initramfs`.
- Ramdisk size and SHA are validated; the WLAN-PD provider marker is deliberately not staged.

Both the boot-image structural validator and rescue-channel audit passed. The new packager's ELF ABI gate passed. The new image is below the 64 MiB budget.

Manifest:

`dist/boot-vqeaf-violet-source-initrdfix.manifest.json`

Previous candidate `dist/boot-vqeaf-violet-source.img` was not overwritten; its SHA-256 remains `f5fd916ff0ff263ec694b411483ebc6028c1ce2194158a2de8c35af48a5e865d`.

## Provider runtime status — not yet sufficient to unlock WLAN

The latest live source-kernel log does confirm:

- RPMh clock provider registered.
- ICNSS platform driver probed successfully.
- ICNSS device assigned to IOMMU group 7.
- ARM SMMU/IOMMU infrastructure is present in the running kernel.

These observations do not prove that the ICNSS power-on sequence was executed. The following remain unverified on the physical Violet:

- PM6150 regulator rails being voted/enabled by ICNSS.
- `cxo_ref_clk_pin` enable and rate/parent state during WLAN power-up.
- Actual ICNSS SMMU domain/IOVA mappings, rather than only IOMMU group attachment.
- CE channel initialization, IRQ operation, coherent DMA allocations and map/unmap lifecycle.

No WLAN-PD start, QMI/WLFW handshake, calibration/MAC update, or modem reset experiment was performed. The provider marker `/etc/qeaf/wlan-pd-provider-verified` remains absent from the candidate image.

## Later RAM-trace attempt and retry gate

The later RAM-trace image was a distinct diagnostic build, not a retry of the initrdfix candidate. Before that attempt, the static gates were the exact stock kernel identity, byte-identical appended DTB, static AArch64 `/init`, tagged initramfs override, absent WLAN-PD provider marker, RAM-only journal/App Store behavior, and both boot-image/rescue audits. Fastboot product `violet` was confirmed and only `fastboot boot` was authorized.

Those static gates did not satisfy the runtime gate. The RAM-trace attempt also ended without verifiable QEAF PID 1 or QEAF USB enumeration; see `RAMTRACE_RAMBOOT_ATTEMPT_2026-11-27.md`. No further candidate should be booted until retained pstore or UART explains the failed handoff.

## Safety / next gate

- p74/p76/p77 writes: 0.
- Flash/erase/format: 0.
- Candidate RAM boot after the correction: attempted once; QEAF PID 1/runtime not confirmed.
- WLAN-PD: disabled.
- WLFW: not tested.
- Calibration and MAC data: unchanged.

The corrected candidate has already been booted once without a verifiable QEAF runtime. Do not retry it until the failed handoff is explained from retained pstore or UART. The pass condition remains a QEAF-specific initramfs unpack log followed by QEAF `/init` as PID 1 and an explicit QEAF boot identity over the diagnostic channel. Only then should ICNSS regulator/clock state, SMMU mappings and CE DMA/IRQ runtime be investigated. WLAN-PD and WLFW remain gated until all those checks pass.
