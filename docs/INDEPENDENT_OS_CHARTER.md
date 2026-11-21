# QEAFIVELS OS — Independent Operating System Charter

Status: architecture baseline, 2026-11-11
Target: Xiaomi Redmi Note 7 Pro (violet), Qualcomm SM6150 family
Identity: QEAFIVELS OS / QEAFIVELS Kernel / QEAFIVELS Framework

## Non-negotiable independence

The production target is a freestanding ARM64 operating system with its own boot handoff, kernel, drivers, syscall ABI, native services, framework, application loader, recovery and package ecosystem.

Forbidden as runtime foundations: Linux kernel, Linux userspace, vendor mobile-platform ROM families, vendor app runtimes, vendor framework/HAL/services, and Linux distributions. A Linux-hosted cross compiler or QEMU host is a development tool only, not a target runtime dependency.

The existing `platform/violet/main_violet.c` image is explicitly LEGACY TRANSPORT PROTOTYPE: it is an AArch64 Linux-musl PID 1/initramfs application using the stock device kernel, DRM/KMS and evdev. It is not QEAFIVELS Kernel and must not be packaged or described as the independent OS. Preserve it for reference and comparison; do not use it as the final target boot path.

The current freestanding x86_64 UEFI kernel/boot manager/recovery and QEMU tests are a development reference for kernel algorithms and interfaces only. They are not phone-compatible and must not be represented as Violet support. Port architecture-neutral algorithms; implement ARM64 entry, exception, MMU and interrupt code separately.

## Product names

- OS: QEAFIVELS OS
- Kernel: QEAFIVELS Kernel
- Framework: QEAFIVELS Framework
- Compatibility runtime: QeafVXP Runtime (isolated from native apps)
- Native package: .qeapp
- MRE compatibility package: .vxp
- Theme package: .vqeaf
- IDE / SDK: VXPEngine
- Windows PC suite: Qeafivels Tool

## Target facts and confidence

Public product/device references identify Redmi Note 7 Pro as Snapdragon 675 / SM6150, Kryo 460, Adreno 612, 4/6 GiB LPDDR4X, 64/128 GB eMMC 5.1 and 2340x1080 IPS LCD. Qualcomm's platform brief identifies SM6150, Adreno 612, Kryo 460, X12 LTE modem and Hexagon 685 DSP.

Existing local Violet hardware notes contain measured observations from one device and a stock image dump:
- DSI panel reports 1080x2340 at 60 Hz; panel name recorded as nt36672a_shenchao.
- Storage dump exposes mmcblk0 and identifies the device storage as eMMC; do not call it UFS. Exact controller/partition geometry must be rechecked against the target unit before any write.
- Existing stock-kernel DRM/KMS dumb-buffer scanout and backlight control were demonstrated from Linux userspace. These observations do not prove a bare-metal display driver.
- USB gadget UDC was observed at a600000.dwc3; configfs mass-storage was demonstrated under the stock kernel only.
- Touch node exists but raw protocol and controller identity remain unverified.
- Audio, Wi-Fi, Bluetooth, modem, GPS, sensors and camera do not have verified QEAFIVELS-native drivers.
- The local hardware notes contain a conflict: product sources say SM6150 while an older Qeafdrop README says SDM660. Resolve from Violet DTB compatible, SoC ID/revision and CPU identification before silicon-specific work.
- The local dump notes report vbmeta presence and unsigned test-image boot observations, but secure-boot/AVB and anti-rollback state are not fully characterized. Do not infer universal unlock/verification behavior.

Reference links:
- upstream Violet device-tree reference (identifier intentionally omitted under QEAF naming policy)
- https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/snapdragon_product_brief_675.pdf
- https://www.qualcomm.com/media/documents/files/secure-boot-and-image-authentication-technical-overview-v2-0.pdf

## Boot strategy and safety

Do not replace XBL/ABL or write boot-critical partitions as an exploratory step. The current evidence supports using the device's existing boot chain as a transport for a future freestanding ARM64 kernel image, but the exact entry exception level, register contract, DTB location, reserved-memory map, cache state, and secure-world handoff must first be measured/documented.

First target artifact: a serial-only freestanding ARM64 kernel that validates a documented handoff and halts safely. Display, touch and storage follow only after register-level and memory-map evidence. Recovery must be a separate minimal freestanding image/path, not a vendor mobile-platform recovery or a Linux initramfs.

No physical flash/write is authorized by this charter. All destructive actions require a separate explicit request and verified device/partition identity, backups and recovery plan.

## Architecture

1. Boot: documented existing boot-chain transport initially; QEAFIVELS stage-2/boot manager only when hardware interface and signing constraints are known.
2. Kernel: freestanding AArch64 EL entry, vectors, GIC interrupt controller, generic timer, page tables/MMU, physical allocator, scheduler, IPC, syscalls, crash log.
3. HAL/drivers: native MMIO/register drivers; vendor firmware is an explicitly versioned external dependency, never an implicit vendor mobile-platform or Linux service.
4. Core services: supervisor, device/capability registry, storage/VFS, power, display, input, logging, security, package/update and notifications via QEAFIVELS IPC.
5. Framework: native graphics, LVGL integration, window manager, events, gestures, font/theme/animation/accessibility and application lifecycle.
6. QeafVXP Runtime: separate compatibility boundary; never load .vxp as .qeapp.
7. Native apps: documented executable ABI, signed .qeapp, permissions and process isolation.
8. UI: lock/home/drawer/notifications/quick settings/settings/recovery backed by real service APIs.

## Verified stage gates

A stage is DONE only when source, reproducible build artifact, automated tests and evidence are recorded. Emulator tests are labeled emulator-only. Hardware tests must include device identity, test method, logs and observed result. No feature may be marked hardware-supported from a UI mock or stock-kernel probe.

### Stage 1 — hardware/boot research
Current: PARTIAL. Public SoC/spec identity and local stock-kernel observations exist. Remaining: resolve SoC ID conflict; capture exact boot image/DTB/bootloader version; verify partition map on the actual unit; document handoff registers/EL/cache/MMU state, RAM/reserved-memory map, security/AVB/rollback and UART route. No partition writes.

### Stage 2 — boot transport proof
Build a host-testable image parser and a serial-only AArch64 payload. Prove boot entry and safe halt without replacing XBL/ABL. Record exact payload format and handoff. Do not claim a custom bootloader unless code actually runs before the kernel slot.

### Stage 3 — ARM64 kernel startup
Freestanding toolchain and linker script; AArch64 entry/vector table; early UART only after verified register/clock/pin configuration; exception diagnostics; handoff parser. First artifact must not call Linux syscalls, libc, or depend on an initramfs userspace.
Current: DONE on QEMU virt (emulated only) — `kernel/arch/aarch64`, gate `scripts/test_kernel_aarch64.py`. The Violet handoff/entry path is still NOT STARTED.

### Stages 4–10 — kernel foundations
MMU and memory map; page allocator; GIC and generic timer; scheduler/context switch; synchronization; IPC; syscall ABI; block-device contract; crash handling. Unit tests on host/QEMU plus target tests when hardware access is available.
Current: stages 4–7 are DONE on QEMU virt; stage 8 is PARTIAL (virtio-mmio only); stages 9–10 are PARTIAL (kernel ramfs with per-task handles, exception/panic diagnostics). Stage 7 includes the F4 credential model: per-task uid/gid/capabilities, capability-gated syscall dispatch and VFS owner/mode checks. None of this is Violet hardware evidence.

### Stages 11–15 — services/framework/UI
Native supervisor and versioned IPC; driver/capability model; VFS; display/input service; LVGL UI port with a QEAFIVELS display/input backend; native framework and .qeapp lifecycle. LVGL is a graphics library, not an OS foundation.

### Stages 16–20 — recovery, ecosystem, validation
Independent recovery; signed update and rollback; QeafVXP isolation; VXPEngine and Qeafivels Tool integration; staged physical validation. Wi-Fi/BT/modem/audio/camera/GPS/sensors require separate firmware and register/interface research.

## Immediate next actions

1. Freeze and relabel the Linux-musl Violet initramfs path as legacy/interim; exclude it from independent-OS release claims.
2. Audit actual repository and preserve the freestanding x86_64 reference implementation and its tests.
3. ~~Create `kernel/arch/aarch64` bring-up sources, linker script and build target with no libc/Linux ABI.~~ Done on QEMU virt: `scripts/build_kernel_aarch64.py` + `scripts/test_kernel_aarch64.py` (milestones A–G1, F4). Remaining: Violet handoff/entry.
4. Resolve target identity and boot handoff from the existing local stock dump and, where available, read-only device diagnostics.
5. ~~Build and test a serial-only AArch64 image before adding UI or attempting any flash.~~ Done on QEMU virt (serial-only boot, then display/input/compositor/UI layers, all emulated). No flash has been attempted for this track.
