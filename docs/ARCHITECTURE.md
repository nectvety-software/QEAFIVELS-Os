# QEAFIVELS OS Architecture

Status: Linux-core architecture approved 2026-11-25.

QEAFIVELS OS uses Linux as the kernel core for the whole product. The Violet
production target uses the vendor Linux `4.14.215-azure-Pixel+` AArch64
kernel. The x86_64/QEMU reference target now uses a separately pinned upstream
Linux 4.14.215 baseline. QEMU boots QEAF PID 1, starts QEAF services and runs
the native x86_64 Puzzle `.qeapp` through the sandboxed device launcher.
QEAFIVELS owns the native userspace, PID 1/init, services, framework, UI,
QEAPP runtime, rescue and tooling above Linux.

The canonical hardware-confidence ledger, boot safety policy and migration
gates are maintained in `docs/INDEPENDENT_OS_CHARTER.md`. The filename is
retained for continuity; the charter now describes a Linux-core OS.

## Current repository components

- `platform/violet`: production Linux-core PID 1/initramfs path. It uses the
  stock Violet kernel, DRM/KMS, evdev, configfs and Linux services. It is no
  longer an interim freestanding-kernel transport.
- `drives/violet`: QEAF-owned userspace provider registry and readiness policy
  over Linux interfaces. It does not own kernel probe, MMIO, IRQ or DMA.
- `drives/`: freestanding SM6150 driver contracts (SPMI PMIC arbiter/regulator,
  GENI I2C, DISP_CC, DSI packets/panel, touch) plus `drives/core/drive_catalog`
  which registers them in `system/core/driver.c`. Built as `vqeaf_drives` and as
  an AArch64 archive; not linked into the Violet PID 1, which must not do MMIO.
  See `docs/MRE_DRIVES.md`.
- `platform/linux-x86_64`: separate headless QEMU path with static Linux PID 1,
  service supervisor and native QEAPP runner. Its `bzImage`, `kernel.config`,
  `System.map`, initramfs and manifest live under `build/linux-x86_64/` and do
  not replace the freestanding x86_64 UEFI artifacts.
- `system/core`, `framework`, `runtime/qeapp`, `sdk`: QEAF-owned userspace
  work. Inspect implementation status before claiming feature completeness.
- `platform/lvgl`: UI toolkit dependency only, not an OS kernel or runtime.
- `kernel/arch/aarch64`: retained freestanding AArch64 reference kernel with
  QEMU virt evidence. It is not the production Violet kernel and must not be
  flashed or described as Linux.
- `kernel/arch/x86_64`, `bootloader/core`, `recovery/core`: retained
  freestanding x86_64 UEFI reference implementations. The Linux x86_64/QEMU
  migration must use separate artifact names and gates.
- `integrations/MRE-Os`: reference integration and porting material. Linux
  drivers, not these freestanding modules, own production hardware.

## Kernel and boot ownership

Linux owns scheduling, memory, interrupts, timers, VFS/filesystems,
namespaces, kernel security primitives and kernel drivers. QEAF PID 1 owns
init lifecycle, service supervision, rescue, package/storage policy and
controlled shutdown. QEAF services own policy and privileged device
operations; the framework exposes stable app-facing APIs; the interface
renders service state and never manipulates drivers directly.

Violet packaging reuses the stock kernel payload byte-for-byte and replaces
only the ramdisk. `scripts/package_violet.py --verify-only` checks the
existing image against the stock reference, verifies the embedded Linux
release and AArch64 Image header, extracts `/init` from the ramdisk, requires
a static AArch64 ELF, runs the structural/rescue gates and prints a
provenance manifest. Normal packaging writes
`dist/boot-vqeaf-violet.manifest.json` only after all gates pass.

The matching Violet 4.14 source tree and vendor revision are not present
locally. The current kernel is therefore verified binary provenance, not a
reproducible source build. The x86_64 reference kernel is reproducibly built
from upstream Linux 4.14.215 source pinned by SHA-256
`9a844e290c61b39dd23df507ba1d9c2b2b4365e5993a978b19f1eb5610b926da`.
`build/linux-x86_64/manifest.json` records the source URL, effective config,
artifact hashes, toolchain, QEMU result and limitations. Source archive GPG
verification and the kernel security backport/CVE review are still pending.

## Driver and capability model

Linux owns device probe/remove, IRQ, DMA, clocks, regulators, resets and
runtime power. QEAF's driver/capability registries remain service-side
bookkeeping and policy: they report what Linux and QEAF services actually
probed. A capability becomes READY only after a real provider succeeds and,
for hardware claims, a documented physical test passes.

Firmware is an external, versioned input. Record its source, version and
hash. Android HAL binaries are not the QEAF framework or OS foundation; any
retained vendor binary is an explicitly inventoried compatibility dependency,
not proof of a QEAF service. Apps never receive MMIO access. A missing provider
stays UNSUPPORTED, PRESENT, BLOCKED_FIRMWARE, DEGRADED or ERROR as evidence
permits.

## Linux security model

Threat model: a hostile or buggy QEAPP process on the Linux-core target;
PID 1 and privileged QEAF services are trusted only for their bounded role.
The trust boundaries are process creation, filesystem/device access, IPC and
package installation.

- QEAPP children drop root identity, clear supplementary groups, close
  inherited descriptors, apply `no_new_privs`, seccomp and resource limits.
- Apps receive no direct MMIO, kernel memory or privileged device nodes.
  Hardware access goes through permissioned QEAF services.
- Verify namespaces, seccomp, cgroup/device controls and LSM/SELinux settings
  from the actual target kernel configuration; never assume them.
- Signed package/update verification, persistent private storage, revocation,
  rollback and hardware-backed key storage remain separate implementation
  gates.

The x86_64/QEMU path proves the startup and lifecycle chain, not production
isolation. Its QEAPP arena is still RWX, `setrlimit()` results are not checked,
and namespaces, chroot, device cgroups, LSM policy and a storage broker are not
implemented. The embedded development HMAC detects image-relative tampering;
it is not publisher authentication. Do not describe this path as a hardened
production sandbox.

The former freestanding AArch64 credential/syscall/VFS model remains a
reference security track. `scripts/test_kernel_aarch64.py` continues to gate
that code, but its EL0 ABI is not the Linux production ABI.

## Hardware truth and release claims

See `docs/VIOLET_HARDWARE.md` and `docs/INDEPENDENT_OS_CHARTER.md`. Existing
DRM/KMS, evdev, configfs and sysfs observations are Linux-kernel evidence.
The local storage dump identifies mmcblk0/eMMC, not UFS. Resolve board and
SoC identity from the actual device before silicon-specific changes.

Never call an x86_64 image a Violet build. Never call QEMU evidence physical
hardware proof. Never claim a kernel source build when only a stock binary is
available. Every release artifact needs source/config provenance, hashes,
tests, logs and explicit limitations.
