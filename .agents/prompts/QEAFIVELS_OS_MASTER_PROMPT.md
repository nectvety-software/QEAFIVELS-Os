# QEAFIVELS OS — TRUE INDEPENDENT OPERATING SYSTEM

Canonical mission prompt for this repository. Supersedes older identity
wording ("VQEAF OS"). The x86_64 QEMU reference and the violet platform layer
are development vehicles; the product below is the target.

## Standalone OS Architecture for Xiaomi Redmi Note 7 Pro (Violet)

### 1. Core Identity

Develop QEAFIVELS OS as a completely independent operating system.

QEAFIVELS OS MUST NOT be based on, derived from, or require:

- Vendor mobile-platform ROM families
- Linux kernel, Ubuntu, Debian, any existing general-purpose OS
- Vendor app runtime, vendor framework, vendor HAL, vendor system services
- Linux userspace or Linux distributions

Do not build a vendor mobile ROM, custom vendor firmware, Linux distribution,
or vendor launcher. The objective is a new operating system with its own
architecture, kernel, system interfaces, runtime, framework, applications
and development ecosystem.

### 2. Target Hardware

Primary device: Xiaomi Redmi Note 7 Pro (`violet`), ARM64. SoC **confirmed
SM6150 / Snapdragon 675** (fastboot `variant: SM6 EMMC`; kernel reports
`4.14.215-azure-Pixel+`), Adreno 612, 1080x2340 panel, capacitive
multi-touch, UFS storage, Wi-Fi/BT/modem/GPS, sensors, USB-C. Older notes in
`docs/VIOLET_HARDWARE.md` that suggested SDM660 are obsolete.

First, investigate the actual hardware, boot chain, memory map, peripherals
and security configuration of Violet. Never assume a hardware component is
accessible without verified documentation, driver research or physical
testing.

### 3. Independent System Architecture

- **Layer 1 — Boot**: QEAFIVELS bootloader, hardware/memory init, secure-boot
  investigation, kernel loading, boot config, recovery path, diagnostics.
- **Layer 2 — QEAFIVELS Kernel** (C/C++ + arch assembly; NOT Linux):
  ARM64 exceptions, physical/virtual memory, scheduler, interrupts, timers,
  IPC, sync primitives, syscall interface, per-task credentials and
  capabilities, syscall authorization, VFS handles with ownership/mode
  enforcement, power management, crash handling.
- **Layer 3 — HAL and drivers** (QEAFIVELS-native): display/framebuffer,
  touch, GPU, storage/UFS, USB, audio, battery/charging, power, GPIO, I2C,
  SPI, UART, sensors, Wi-Fi, BT, modem, GPS, camera, fingerprint. Minimal
  working set first. Vendor firmware components documented separately; no
  silent Linux or vendor runtime dependencies through vendor components.
- **Layer 4 — Core system services**: Process, Memory, Device, Storage,
  Power, Network, Audio, Display, Input, Security, Package, Notification,
  Update, Logging — QEAFIVELS-native APIs and IPC only.
- **Layer 5 — Framework**: Window Manager, graphics, UI toolkit, event
  system, touch/gestures, app lifecycle, resources, fonts, animation, theme,
  accessibility, native API. No vendor SDK, Java/Kotlin or vendor app runtime.
- **Layer 6 — QeafVXP Runtime**: separate compatibility subsystem for MRE
  `.vxp` apps; never confused with the native architecture.
- **Layer 7 — Native app environment**: native executable format, loader,
  `.qeapp` packages, permissions, sandbox, lifecycle, native APIs, signing.
- **Layer 8 — System UI**: lock, home, drawer, notifications, quick
  settings, settings, file manager, themes, app manager, system info,
  recovery UI — talking to QEAFIVELS services only.

### 4. Native Development Ecosystem

| Component | Name |
|---|---|
| Operating System | QEAFIVELS OS |
| Kernel | QEAFIVELS Kernel |
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

Independent QEAFIVELS Recovery: own boot entry, own execution environment,
storage access, package verification, update install, system repair,
diagnostics, logs, factory reset. Never a vendor mobile-platform recovery or TWRP.

### 6. Development Strategy (verified stages)

1. Hardware and boot-chain research — **see `docs/VIOLET_HARDWARE.md`**
2. Bootloader proof of concept
3. ARM64 kernel startup
4. Memory and interrupt management
5. UART debugging
6. Basic display output
7. Touchscreen input
8. Storage access
9. Process and memory management
10. Native system calls
11. Native system services
12. Graphics and window manager
13. QEAFIVELS Framework
14. Native `.qeapp` runtime
15. System UI
16. Recovery environment
17. Networking, audio, sensors, camera, remaining drivers
18. VXPEngine integration
19. Qeafivels Tool integration
20. Physical-device validation

Every stage must produce testable build artifacts and documented results.

### 7. Absolute Engineering Restrictions

- Never use a vendor mobile platform as the OS foundation; never use the Linux kernel.
- Never depend on a vendor app runtime or framework.
- Never repackage a vendor mobile ROM and rename it QEAFIVELS OS.
- Never fake hardware support; never claim physical-device compatibility
  based only on an emulator; never confuse the UI with the OS itself; never
  mix `.qeapp` and `.vxp` execution models.
- Never let an EL0 process widen its own uid or capability mask, and never add
  a syscall that bypasses the dispatcher's capability check or the VFS
  owner/mode check. Apps must never hold `VQEAF_CAP_SYSTEM` or
  `VQEAF_CAP_FS_ADMIN`.
- If a feature is blocked by missing hardware documentation, firmware or
  boot access, document the blocker and investigate alternative native
  implementations.

### 7b. Current development reality (evidence, 2026-11-21)

The `platform/violet` PID 1 runs on the **stock 4.14.215 vendor kernel** as a
development vehicle (not the QEAFIVELS Kernel). That kernel is monolithic —
drivers (wlan, cfg80211, bluetooth, touch, camera, fingerprint) are **already
built in**; there is nothing to "port" from the stock ROM into it. Missing
capabilities are userspace/service/firmware, not kernel drivers:
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

Deliver a genuinely independent mobile operating system for Xiaomi Redmi
Note 7 Pro: QEAFIVELS OS owns its boot architecture, kernel, drivers, system
services, framework, runtime, native applications, recovery environment and
development tools.

**QEAFIVELS OS — Explore • Create • Enjoy**
