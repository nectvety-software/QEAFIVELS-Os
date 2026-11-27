# QEAFIVELS OS — LINUX-CORE OPERATING SYSTEM

Canonical mission prompt for this repository. Architecture decision approved
2026-11-25: Linux is the kernel core for the whole QEAFIVELS OS. QEAFIVELS
owns its userspace, services, framework, runtime, applications, recovery,
interface and tooling; it does not claim to have written the Linux kernel.

## Linux-Core Architecture for Xiaomi Redmi Note 7 Pro (Violet)

### 1. Core Identity

Develop QEAFIVELS OS as a Linux-kernel-based operating system with a
QEAF-owned native userspace and application ecosystem.

- Violet production kernel: vendor Linux `4.14.215-azure-Pixel+`, AArch64.
- x86_64/QEMU reference kernel: separately pinned Linux 4.14.215, not the
  Violet binary and not the retired freestanding `KERNEL.EFI` path.
- QEAFIVELS owns PID 1/init, service supervision, capability policy, device
  brokers, storage/package/update policy, framework, UI, QEAPP runtime,
  rescue and SDK/tooling.
- Linux owns scheduling, memory, interrupts, VFS/filesystems, namespaces,
  kernel security primitives and kernel drivers.

QEAFIVELS MUST NOT depend on an Android/AOSP/ART runtime, APK-style packages,
Java/Kotlin, a vendor mobile ROM, vendor framework/HAL/services or a Linux
distribution as its userspace foundation. Do not rename a vendor ROM or a
Linux distribution as QEAFIVELS OS, and do not mislabel Linux as a
QEAF-written kernel.

### 2. Target Hardware

Primary device: Xiaomi Redmi Note 7 Pro (`violet`), ARM64. SoC **confirmed
SM6150 / Snapdragon 675** (fastboot `variant: SM6 EMMC`; kernel reports
`4.14.215-azure-Pixel+`), Adreno 612, 1080x2340 panel, capacitive
multi-touch, eMMC storage, Wi-Fi/BT/modem/GPS, sensors, USB-C. Older notes in
`docs/VIOLET_HARDWARE.md` that suggested SDM660 are obsolete.

First, investigate the actual hardware, boot chain, memory map, peripherals
and security configuration of Violet. Never assume a hardware component is
accessible without verified documentation, driver research or physical
testing.

### 3. Linux-Core System Architecture

- **Layer 1 — Boot transport**: existing verified boot chain, pinned kernel
  artifact, initramfs, recovery path and diagnostics. Do not replace XBL/ABL
  or write boot-critical partitions as an exploratory step.
- **Layer 2 — Linux kernel core**: vendor Violet 4.14.215 on AArch64 and a
  pinned upstream 4.14.215 x86_64/QEMU baseline. Linux owns process/memory,
  interrupts, timers, VFS, namespaces, security primitives and drivers.
- **Layer 3 — Linux drivers and QEAF hardware services**: kernel drivers own
  probe, IRQ, DMA, clocks, regulators and device binding. QEAF services
  expose bounded, permissioned hardware APIs to apps; userspace never writes
  MMIO or opens privileged devices directly. Firmware remains external and
  provenance-recorded.
- **Layer 4 — Core system services**: Process, Memory, Device, Storage,
  Power, Network, Audio, Display, Input, Security, Package, Notification,
  Update and Logging — QEAF-owned services above Linux.
- **Layer 5 — Framework**: Window Manager, graphics, UI toolkit, event
  system, touch/gestures, app lifecycle, resources, fonts, animation, theme,
  accessibility and native API. No vendor SDK, Java/Kotlin or vendor app runtime.
- **Layer 6 — QeafVXP Runtime**: separate compatibility subsystem for MRE
  `.vxp` apps; never confused with the native architecture.
- **Layer 7 — Native app environment**: native executable format, loader,
  `.qeapp` packages, permissions, Linux process sandbox, lifecycle, native
  APIs and signing.
- **Layer 8 — System UI**: lock, home, drawer, notifications, quick
  settings, settings, file manager, themes, app manager, system info and
  recovery UI — talking to QEAFIVELS services only.

### 4. Native Development Ecosystem

| Component | Name |
|---|---|
| Operating System | QEAFIVELS OS |
| Kernel | QEAFIVELS Linux Core (vendor Violet 4.14.215; pinned x86_64 4.14.215) |
| Native Framework | QEAFIVELS Framework |
| Runtime | QeafVXP Runtime |
| Native Application | `.qeapp` |
| Existing MRE Application | `.vxp` |
| Theme Package | `.vqeaf` |
| Development IDE / SDK | VXPEngine |
| Windows PC Suite | Qeafivels Tool |

VXPEngine must eventually support native QEAFIVELS app development alongside
its existing MRE VXP toolchain.

### 5. Recovery and Update Architecture

QEAFRESCUE and QEAF Recovery run as QEAF-owned Linux userspace/initramfs
paths with bounded storage access, package verification, diagnostics and
repair tools. Never present a vendor mobile-platform recovery or TWRP as
QEAFIVELS Recovery. Signed update and rollback remain separate gates.

### 6. Development Strategy (Linux-core migration)

1. Record the approved Linux foundation and kernel/package provenance.
2. Verify Violet stock-kernel reuse without changing the binary or flashing.
3. Pin and build the x86_64 Linux 4.14.215 QEMU baseline and initramfs.
4. Boot QEAF PID 1/services on x86_64 QEMU, then validate rescue and QEAPP.
5. Make the Violet Linux kernel source/config/DTB build reproducible when
   exact vendor sources are available; until then declare binary-only provenance.
6. Move hardware ownership to Linux drivers and QEAF service adapters; retain
   the freestanding kernel only as a reference/regression track.
7. Complete Linux security, persistence, update and rollback evidence.
8. Validate physical Violet only after separate explicit authorization.

Every stage must produce testable build artifacts and documented results.

### 7. Absolute Engineering Restrictions

- Linux is the approved kernel core; never claim QEAFIVELS wrote the Linux
  kernel and never load freestanding-kernel drivers into Linux userspace.
- Never depend on an Android/AOSP/ART runtime, vendor app runtime/framework,
  APK-style packages, Java/Kotlin or a vendor mobile ROM.
- Never repackage a vendor mobile ROM or Linux distribution and rename it
  QEAFIVELS OS.
- Never fake hardware support; never claim physical-device compatibility
  based only on an emulator; never confuse the UI with the OS itself; never
  mix `.qeapp` and `.vxp` execution models.
- QEAPP processes must drop root identity, clear supplementary groups, close
  inherited descriptors, apply `no_new_privs`, seccomp and resource limits,
  and receive no direct MMIO or privileged device access.
- Do not modify boot/recovery/radio/userdata/misc partitions, flash firmware
  or boot-test physical hardware without separate explicit authorization.
- If a feature is blocked by missing hardware documentation, firmware,
  kernel configuration or source provenance, document the blocker and keep
  the capability disabled or blocked with an explanation.

### 7b. Current development reality (evidence, 2026-11-25)

The `platform/violet` PID 1 runs on the **vendor Linux 4.14.215 kernel**, now
the approved Violet kernel core. That kernel is monolithic — drivers (wlan,
cfg80211, bluetooth, touch, camera, fingerprint) are already built in; there
is nothing to "port" from the stock ROM into Linux userspace. Missing
capabilities are firmware, userspace/service, kernel configuration or a
reviewed kernel backport:
- Wi-Fi: `wlan0` absent because WLFW QMI service `0x45` is never published by
  the modem; cnss-daemon now runs (binderfs `/dev/binder` fixed) but is silent.
- GNSS: kernel exposes no `/dev/gnss` or `/sys/class/gnss`; HAL service is
  present but has no device node.
- wpa_supplicant / cnss-daemon: all NEEDED `.so` were extracted from the real
  `stock-violet/system.img` + `vendor.img` APEX (`/system/apex`, not `/apex`)
  into `/opt/bionic-cnss` (WSL); 13/13 and 15/15 resolve, zero missing.
- No mock/bypass QMI, no fake `wlan0`, no fake firmware. See
  `docs/platforms/VIOLET_WIFI_BRINGUP.md` for full evidence and blockers.

### 7c. Design language — VQEAF Midnight V2 (2026-11-22)
- The shell uses the independent **VQEAF Midnight V2** design system; canonical
  spec is `docs/design/VQEAF_MIDNIGHT_V2.md`. Dark UI: Midnight Navy `#070B20`,
  Deep Blue `#101A3A`, Neon Violet `#8157FF`, Electric Blue `#287BFF`,
  Cyan Glow `#25D9F2`, text `#F4F5FF` / `#AAB2D0`. Ubuntu font with full
  Vietnamese diacritics. Slogan: "Your Freedom, Our OS".
- **No Google anywhere**: no Play Store/GMS/Firebase/Google Search/Assistant/
  Gmail/Maps/Photos or third-party fake logos. Use QEAF App Store, QEAF Search,
  QEAF Assistant, QEAF File Manager, QEAF Update, QEAF Account, QEAF Notification.
- Implement design by updating `platform/violet/ui/vqeaf_theme.c` palette and
  component styles; never create a second UI framework. Hardware state always
  comes from real providers, never invented for the look.

### 8. Final Objective

Deliver QEAFIVELS OS for Xiaomi Redmi Note 7 Pro as a Linux-core mobile OS:
QEAFIVELS owns PID 1/init, system services, hardware policy, framework,
runtime, native applications, recovery, interface and development tools,
with every kernel/firmware artifact and hardware capability traceable to
real evidence.

**QEAFIVELS OS — Explore • Create • Enjoy**
