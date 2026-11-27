# QEAFIVELS OS — Linux-Core Operating System Charter

Status: architecture baseline updated 2026-11-25
Target: Xiaomi Redmi Note 7 Pro (violet), Qualcomm SM6150 family
Identity: QEAFIVELS OS / QEAFIVELS Linux Core / QEAFIVELS Framework

The filename is retained for continuity. The former "independent kernel"
foundation was superseded by the explicit 2026-11-25 decision to use Linux
as the core for the entire OS.

## Approved foundation and ownership

The production Violet target uses the vendor Linux `4.14.215-azure-Pixel+`
AArch64 kernel. The x86_64/QEMU reference target uses a separately pinned
upstream Linux 4.14.215 kernel and deterministic initramfs. QEMU boots QEAF
PID 1, services and the native Puzzle `.qeapp`; this is headless emulator
evidence, not a physical hardware or production-release claim.

Linux owns process/memory management, scheduling, interrupts, timers,
VFS/filesystems, namespaces, kernel security primitives and kernel drivers.
QEAFIVELS owns PID 1/init, system services, capability policy, device
brokers, framework, UI, QeafVXP Runtime, `.qeapp`, `.vqeaf`, rescue and
tooling.

Forbidden as userspace foundations: Android/AOSP/ART, APK-style packages,
Java/Kotlin, vendor mobile ROMs, vendor app runtimes/frameworks/HAL/services
and Linux distributions. Linux kernel use is approved. Never rename a vendor
ROM or Linux distribution as QEAFIVELS OS, and never claim QEAFIVELS wrote
the Linux kernel.

The freestanding x86_64 UEFI and AArch64 kernels are retained as
reference/regression tracks until Linux reaches evidence parity. They are
not phone-compatible production kernels and must not be flashed as such.

## Product names

- OS: QEAFIVELS OS
- Kernel: QEAFIVELS Linux Core
- Framework: QEAFIVELS Framework
- Compatibility runtime: QeafVXP Runtime (isolated from native apps)
- Native package: .qeapp
- MRE compatibility package: .vxp
- Theme package: .vqeaf
- IDE / SDK: VXPEngine
- Windows PC suite: Qeafivels Tool

## Target facts and confidence

Public product/device references identify Redmi Note 7 Pro as Snapdragon 675
/ SM6150, Kryo 460, Adreno 612, 4/6 GiB LPDDR4X, 64/128 GB eMMC 5.1 and
2340x1080 IPS LCD. Qualcomm's platform brief identifies SM6150, Adreno 612,
Kryo 460, X12 LTE modem and Hexagon 685 DSP.

Existing local Violet hardware notes contain measured observations from one
device and a stock image dump:

- DSI panel reports 1080x2340 at 60 Hz; panel name recorded as
  `nt36672a_shenchao`.
- Storage exposes mmcblk0 and identifies eMMC; do not call it UFS. Exact
  controller/partition geometry must be rechecked before any write.
- Stock-kernel DRM/KMS scanout, backlight control, evdev and configfs USB
  gadget were demonstrated from Linux userspace.
- USB gadget UDC was observed at `a600000.dwc3`.
- Wi-Fi, Bluetooth, modem, GNSS, audio, camera and biometrics still require
  honest firmware/service/driver evidence; a HAL or UI mock is not proof.
- The dump contains `vbmeta`; secure-boot/AVB and anti-rollback state are not
  fully characterized. Do not infer universal unlock behavior.

## Boot strategy and safety

Do not replace XBL/ABL or write boot-critical partitions as an exploratory
step. Violet currently reuses the stock kernel payload and existing boot
transport, replacing only the ramdisk. Kernel/package provenance must be
verified before any deployment consideration.

No physical flash/write is authorized by this charter. Current project
policy permits only explicitly approved p77/recovery writes with both raw
read-back proofs; p76/boot, userdata and misc remain forbidden without a new
explicit authorization. All destructive actions require verified
device/partition identity, backups and a recovery plan.

## Architecture

1. Boot: verified existing transport, pinned kernel artifact, initramfs,
   recovery and diagnostics.
2. Linux kernel core: vendor Violet 4.14.215 on AArch64; pinned upstream
   4.14.215 x86_64/QEMU baseline.
3. Drivers: Linux owns probe/remove, IRQ, DMA, clocks, regulators, resets and
   runtime power. QEAF services expose bounded hardware APIs.
4. Core services: supervisor, device/capability registry, storage/VFS,
   power, display, input, logging, security, package/update and notifications.
5. Framework: native graphics, LVGL integration, window manager, events,
   gestures, font/theme/animation/accessibility and application lifecycle.
6. QeafVXP Runtime: separate compatibility boundary; never load `.vxp` as
   `.qeapp`.
7. Native apps: documented executable ABI, signed `.qeapp`, permissions and
   Linux process isolation.
8. UI and rescue: lock/home/drawer/notifications/quick settings/settings and
   recovery backed by real service APIs.

## Security gates

QEAPP processes must drop root identity, clear supplementary groups, close
inherited descriptors, apply `no_new_privs`, seccomp and resource limits, and
receive no direct MMIO or privileged device access. Verify the actual target
kernel's namespace, seccomp, cgroup/device and LSM configuration.

Signed package/update verification, persistent private storage, revocation,
rollback and hardware-backed key storage remain separate implementation
gates. The freestanding kernel's EL0 capability/VFS tests remain mandatory
for that reference track only.

## Verified stage gates

A stage is DONE only when source or binary provenance, reproducible build
artifact where source exists, automated tests and evidence are recorded.
Emulator tests are labeled emulator-only. Hardware tests must include device
identity, method, logs and observed result. No feature may be marked
hardware-supported from a UI mock or stock-kernel probe alone.

### Linux migration phases

1. **Architecture and provenance — IN PROGRESS.** Canonical rules now name
   Linux as the core. Violet host-only kernel/package manifest verification
   is implemented; source revision remains unavailable.
2. **x86_64 Linux baseline — PASS.** Upstream Linux 4.14.215 source, config,
   toolchain, `bzImage`, static userspace and deterministic initramfs are pinned
   and verified under `build/linux-x86_64/`.
3. **x86_64 QEMU bring-up — PARTIAL.** QEAF PID 1, service readiness, native
   Puzzle `.qeapp` lifecycle and clean poweroff PASS in QEMU. Rescue, system UI,
   persistence and updates are not wired into this headless path.
4. **Violet reproducible kernel — BLOCKED.** Exact vendor 4.14 source/config/
   DTB provenance is missing; current packaging is verified binary reuse.
5. **Driver/service parity — IN PROGRESS.** Linux owns hardware; QEAF service
   adapters and capability reporting remain under test.
6. **Security/persistence/update — PARTIAL.** QEAPP sandbox exists; durable
   storage, revocation, signed update and rollback are incomplete.
7. **Physical validation — NOT AUTHORIZED.** Requires separate explicit
   approval and p77-only evidence gates.

## Immediate next actions

1. Keep `scripts/package_violet.py --verify-only` and its unit tests green.
2. Keep `scripts/build_linux_x86_64.py`, its 21 baseline tests and the
   Linux/QEAPP regression selection green without overwriting freestanding
   `KERNEL.EFI` artifacts.
3. Extend the three passing negative QEMU cases (missing services, missing
   package, tampered package) with wrong ABI/architecture, crash, timeout and
   unavailable seccomp without weakening the PASS gate.
4. Harden the Linux QEAPP sandbox: W^X arena, checked rlimits, namespaces/
   chroot/device controls, stronger signing, private storage and revocation.
5. Complete the Linux 4.14 security backport/CVE review and source-archive GPG
   verification before any release claim.
6. Obtain exact Violet vendor kernel source/config/DTB provenance before
   claiming a reproducible Violet kernel build.
7. Preserve the freestanding kernels as reference tracks until Linux reaches
   evidence parity; do not delete them during migration.
