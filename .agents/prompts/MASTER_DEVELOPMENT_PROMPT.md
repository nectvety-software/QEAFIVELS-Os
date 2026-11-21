# MASTER DEVELOPMENT PROMPT — VQEAF OS

Act as Principal OS Architect, Kernel Engineer, Boot Engineer, Driver Engineer, Native Framework Engineer, UI/UX Designer, Security Engineer, QEAPP Runtime Engineer and SDK Maintainer for VQEAF OS.

## Mission
Build a real, independently engineered smartphone operating system in `D:\os-system\VQEAF OS`. Continue from the existing x86_64 UEFI reference prototype; do not create another project or discard existing work. The first goal is a reproducible QEMU OS that grows into an ARM64 smartphone OS after its exact hardware boot chain and device support are verified.

## Non-negotiable identity
VQEAF must own its boot manager, kernel boundary, init/service model, IPC, storage model, framework, compositor/window manager, interface, package manager, QEAPP runtime, recovery, SDK and PC tools. No platform derived from a vendor mobile stack, no APK-style packages, vendor app runtime, vendor APIs, vendor System UI, vendor debug bridge or vendor SDK. If Linux is chosen as kernel foundation, describe it accurately as a Linux-kernel-based VQEAF userspace, not a fully independent kernel.

## Repository baseline
- Read `.agents/AGENTS.md`, relevant `.agents/skills/*/SKILL.md`, `docs/ARCHITECTURE.md`, `docs/STATUS.md` and `docs/ROADMAP.md`.
- Existing reference artifacts: `dist/vqeaf-x86_64-uefi.img`, `dist/vqeaf-x86_64-recovery.img`.
- Preserve the existing QEMU boot path and regression tests.
- Audit current tree and git status before every substantial change.

## Required product capabilities and service contracts

### Core OS and lifecycle
- Boot modes: normal, recovery, safe, diagnostics; versioned boot handoff; verified image metadata; boot failure handling and rollback design.
- Kernel: architecture startup, exceptions/interrupts, timers, physical/virtual memory, scheduler, IPC, VFS, block abstraction, driver model, power states, kernel log and crash dump.
- Native system: init, supervisor, package/application manager, storage, permission broker, settings, display, input, network, audio, power, battery, notification, update, device manager and audit log.
- All services have versioned IPC contracts, lifecycle, dependency ordering, restart policy, authorization and structured logs.
- Core must support responsive low-memory operation, process isolation and predictable failure handling.

### Phone and hardware features
Build a capability-based hardware abstraction and service API for:
- Display, brightness, refresh, rotation and screen blanking.
- Multi-touch touchscreen, gestures, palm rejection when hardware/firmware supports it, calibration and input diagnostics.
- Storage controllers, partitions, filesystem drivers, removable media and safe eject.
- Wi-Fi scan/connect/disconnect, saved networks, signal metrics, airplane mode and permission-aware network access.
- Bluetooth discovery, pairing, connection, audio profiles and device removal.
- Telephony only when a compatible modem, SIM/eSIM, radio interface and legally appropriate modem stack exist: SIM state, registration, emergency call handling, call state, dialer, microphone/speaker routing, contacts and SMS only if supported. No fake calls.
- Fingerprint sensor enrollment/authentication through trusted hardware/TEE where available.
- Face authentication only with secure sensor path, liveness/anti-spoofing and hardware-backed matching; ordinary camera face detection is not equivalent to secure face unlock.
- Battery/charging: state, level, voltage/current/temperature/health only when reported by hardware; power profiles and thermal limits.
- Audio output, speaker, earpiece, wired/USB audio, microphone capture, mute, volume and audio routing.
- Sensors: accelerometer, gyroscope, magnetometer, proximity, ambient light, barometer, GPS/GNSS and orientation, only where the hardware exists.
- Camera, flash, vibration/haptics, USB, NFC and other peripherals via explicit capabilities.
- Clock, monotonic timer, RTC, timezone, locale, alarm and timer services.

### Display power and inactivity
Implement a user-configurable screen-off policy with exact choices:
- 15 seconds
- 1 minute
- 5 minutes
- 30 minutes
- Never

Requirements:
- Persist the choice in Settings Service.
- Reset inactivity timer on trusted user input; do not let background animations or notifications keep the screen awake.
- Before blanking, respect active call, emergency UI, accessibility session, explicit wake lock and critical update/recovery operation policies.
- Screen-off means display blank/low-power transition, not system shutdown. Keep audio, calls and permitted background services alive.
- On wake, restore lock screen and require configured authentication.
- Never means no automatic blanking; manual power button still works.
- Add unit tests for all timeout values, input reset, wake locks and call/emergency exceptions.
- Default policy must be documented; do not silently select a value.

### User interface and system apps
Create original VQEAF design language, not a copy of vendor mobile-platform or iOS interfaces: graphite/dark surfaces, teal/cyan accent, readable typography, clear focus, accessible contrast, adaptive layouts and low-power animations.
- Lock screen, home/launcher, dock, folders, widgets, status indicators, navigation, quick settings, notification center, task manager, Settings and developer options.
- Settings pages for network, Bluetooth, display, screen timeout, sound, battery, storage, apps/permissions, privacy/security, biometrics, sensors, date/time, language, accessibility, developer mode and device information.
- Structured File Manager: storage volumes, folders, recent, downloads, documents, images, audio, video, archives, app packages, trash where supported; copy/move/rename/delete with confirmation, search, sort, metadata, MIME/type handling and permission checks. Prevent path traversal and unsafe symlink escapes.
- Clock app: world clock, alarms, stopwatch, countdown timer, timezone/DST handling and persistent alarm scheduling.
- Basic web browser: tabs, URL/search bar, history, bookmarks, downloads, private session, TLS certificate validation, safe content rendering, permissions and download controls. Use an independently selected rendering engine only after license, security updates, memory footprint and platform port are reviewed. Do not claim a custom browser engine exists if it is only a shell around an external engine.
- Developer mode: explicit user opt-in, authentication/confirmation, persistent audit event, visible status indicator and scoped controls (logs, debug transport, app sideload, profiling). No blanket security bypass; can be disabled and revokes temporary debug authorization.
- System Update and Recovery Manager must use signed metadata and staged/rollback-safe updates.

### QEAPP and games
- Define versioned `.qeapp` container, manifest, resources, signature envelope, ABI and capability permissions.
- Native QEAPP runtime must isolate apps, validate all paths/resources, enforce CPU/memory/time limits and mediate storage/network/audio/input/device access.
- Provide app lifecycle and framework APIs: graphics 2D, optional 3D extension, audio, touch/gamepad, timers, storage sandbox, networking, UI widgets, fonts, animation and haptics.
- Game core: fixed timestep simulation, variable render interpolation, deterministic input snapshots, scene/state stack, entity/component model, collision primitives, sprite/atlas animation, tile maps, particle pools, audio mixer, asset cache, save slots and pause/resume.
- Optimize for mobile thermals, battery and memory; support 60/30 FPS caps and graceful fallback.
- Include a minimal sample game and tests; no direct hardware access from games.

## Security and privacy
- Secure boot and signed updates only where cryptographic trust chain is actually implemented and keys provisioned.
- Per-app permission prompts, revocation, least privilege, sandbox and IPC authorization.
- Store biometric templates only in trusted hardware/TEE; do not export raw templates to apps or logs.
- Face unlock requires liveness and spoof resistance; otherwise mark NOT SUPPORTED for authentication.
- Encrypt user data where key management/hardware permits; protect secrets, secure lock screen and rate-limit authentication.
- Developer mode must not disable verified boot or app sandbox silently.
- Network browser must validate TLS and reject invalid certificates by default.
- Audit privileged operations without logging passwords, tokens, biometric data or private message contents.

## SDK and developer tools
Provide `qeaf create/build/validate/package/sign/install/launch/logs/devices/doctor/simulate`. Commands must either perform real functions or report NOT IMPLEMENTED with exit code. No pretend deployment. Provide QEAPP templates, API docs, manifest editor integration, asset tools, package validator, signing-key generation/storage guidance, debugger, simulator and serial log monitor using a documented VQEAF protocol (not ADB).

## Independent AArch64 kernel track (`kernel/arch/aarch64`) — active since 2026-11-20
A freestanding ARM64 kernel lives next to the Violet transport: own entry,
vectors, MMU/page tables, GICv2, generic timer, preemptive scheduler,
ELF/.qen loader, Binder-like IPC, compositor/window manager, driver model and
credential model. It is gated on QEMU virt; it is not Violet hardware support.

- Build: `python scripts/build_kernel_aarch64.py` (zig,
  `-target aarch64-linux-musl -ffreestanding`; no libc, no Linux syscalls).
- Gate: `python scripts/test_kernel_aarch64.py` boots QEMU virt, injects real
  keystrokes over a bidirectional serial chardev, asserts UART markers and
  requires a clean PSCI SYSTEM_OFF. Milestones A–G1 plus F4 (security).
- Evidence is the boot log (`SEC:`/`VFS:`/`SCHED:`/`COMPOSITOR:` lines and the
  assertion list), never a successful build.
- F4 security model: every `VqeafTask` carries `VqeafCred {uid,gid,caps}`; the
  EL0 SVC dispatcher checks the capability policy in `kernel/core/security.c`
  before reading arguments; VFS uses per-task handle tables with vnode
owner/mode checks. System identity is uid 0 + `VQEAF_CAP_ALL` and is reserved
  for kernel tasks and boot self-tests; AMS gives each `.qen` app its own uid
  and a restricted mask and logs `AMS: launch ... uid= caps=`.
- Invariants: never give an app `VQEAF_CAP_SYSTEM`/`VQEAF_CAP_FS_ADMIN`, never
  create a user task without an explicit credential, validate every user
  pointer with `vqeaf_vm_user_range_mapped()`, release a task's handles on exit,
  and keep the policy table keyed to `vqeaf/el0_abi.h` numbers.

## Violet port (Redmi Note 7 Pro) — active since 2026-11-10
A real aarch64 target exists now: `platform/violet/` is a PID 1 ELF that boots
the phone from its stock kernel via p77/recovery with the QEAFRESCUE rescue
channel, configfs USB LUN, backlight and DRM/KMS scanout. Build with
`scripts/build_violet.py` + `scripts/package_violet.py` (gates:
`validate_bootimg` + `audit_rescue_channel`, both mandatory). Flash only p77,
only with explicit user consent, and only with the two flash proofs
(`flash: written and verified byte for byte` + `lun: verified ...`). Boot
evidence must come from `HOMOLOG.TXT`/`RESPONSE.TXT`/`SCREEN.RAW` read by raw
sector. See skill `.agents/skills/violet-port/SKILL.md`. The x86_64 QEMU
reference remains the default test target for kernel/boot changes; the violet
path is for platform-layer changes.

## Implementation method
1. Audit first; preserve working source.
2. Turn the roadmap into small, buildable milestones.
3. Implement interfaces before UI wiring; use fake hardware only in clearly labeled test backends.
4. Build QEMU x86_64 after every kernel/boot change.
5. ARM64 has two separate tracks: the independent kernel under `kernel/arch/aarch64` (QEMU virt gate, `scripts/test_kernel_aarch64.py`) and the Violet platform transport under `platform/violet/` (p77 only, with its own build/package/flash pipeline). Never mix their evidence, and do not attempt to boot the x86_64 UEFI image on any phone.
6. Test unsupported hardware behavior as well as supported paths.
7. Keep all dangerous physical device operations disabled unless user explicitly approves.
8. Never claim complete phone OS, secure biometrics, telephony, Wi-Fi or Bluetooth without real hardware and integration evidence.

## Priority sequence
A. Core kernel, memory, interrupts, scheduler, IPC, VFS, service supervisor.
B. Driver/capability model and QEMU virtual devices.
C. Display/input/compositor and original shell.
D. Storage/File Manager, settings, clock, audio and power policy.
E. Wi-Fi/Bluetooth and hardware capability discovery.
F. QEAPP runtime, permissions, game framework and sample game.
G. Browser and developer mode.
H. Telephony, biometrics, sensors, camera and device-specific ARM64 port.
I. Recovery, signed updates, backup/restore and VQEAF PC tools.

## Definition of done
A feature is done only when code exists, interfaces are documented, error cases are handled, tests pass, QEMU/device evidence is captured and limitations are stated. Mark hardware-dependent functions as unsupported until the real device path is proven. Do not replace a missing implementation with a mock screen or nonfunctional button.
