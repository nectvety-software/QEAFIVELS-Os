# QEAFIVELS OS — 20-stage implementation roadmap

Canonical requirements and safety policy: `docs/INDEPENDENT_OS_CHARTER.md`.

| Stage | Deliverable | Gate / current status |
|---|---|---|
| 1 | Violet hardware and boot-chain research | PARTIAL; resolve SoC, boot handoff, memory map and security |
| 2 | Boot transport proof | NOT STARTED; serial payload through documented existing chain |
| 3 | Freestanding ARM64 startup | DONE on QEMU virt (entry, EL1, vectors, PL011, safe PSCI off); Violet handoff/entry NOT STARTED |
| 4 | Physical/virtual memory | DONE on QEMU virt (identity map, PMM, per-task user roots); Violet memory map absent |
| 5 | Interrupts and timers | DONE on QEMU virt (GICv2, generic timer, PL011 RX); Violet IRQ wiring absent |
| 6 | Scheduler and synchronization | DONE on QEMU virt (preemptive round-robin, IRQ-masked critical sections, single core); SMP not started |
| 7 | IPC and syscalls | DONE on QEMU virt: EL0 SVC ABI with per-task uid/gid/capabilities, capability-gated dispatch, open/read/write/close. Object-registry IPC and revocation NOT STARTED |
| 8 | Native block/storage drivers | PARTIAL: virtio-mmio block (modern v2 + legacy v1) on QEMU only; Violet eMMC driver not started |
| 9 | VFS and native storage services | PARTIAL: kernel ramfs root + per-task handle tables with owner/mode checks; no persistent filesystem or mount layer |
| 10 | Kernel diagnostics and crash handling | PARTIAL: vectors, ESR/FAR/ELR dump, panic path and UART evidence markers; no crash dump to storage |
| 11 | Native service supervisor | partial host/reference code; ARM64 runtime integration absent |
| 12 | Driver model and capability registry | PARTIAL: probe/register model + capability registry on QEMU; phone drivers absent |
| 13 | Display and input services | PARTIAL on QEMU virt (ramfb scanout, PL011 keyboard, compositor/WM); native Violet display/touch drivers absent |
| 14 | QEAFIVELS Framework | PARTIAL: window manager, compositor, AMS launch path and EL0 SystemUI prototype; stable app ABI and lifecycle incomplete |
| 15 | Native UI and .qeapp lifecycle | UI prototypes exist; no claim of production app runtime |
| 16 | Independent recovery | x86_64 EFI reference only; Violet freestanding recovery not started |
| 17 | Networking/audio/sensors/camera | NOT STARTED; firmware and interfaces need research |
| 18 | QeafVXP Runtime and VXPEngine | compatibility design must remain isolated from .qeapp |
| 19 | Qeafivels Tool integration | SDK/CLI partial; deployment must remain non-destructive |
| 20 | Physical-device validation | NOT STARTED; requires verified serial/boot/display/input/storage tests |

## Rules

- Linux-musl `platform/violet` image is legacy stock-kernel transport only.
- "DONE on QEMU virt" means emulator evidence from `scripts/test_kernel_aarch64.py` (UART markers + clean PSCI exit). It is never evidence of Violet hardware support, and it does not cover the ARM64 boot handoff.
- Kernel security (stage 7, milestone F4): every EL0 syscall is capability-checked at dispatch and every VFS access is owner/mode checked. A new syscall without a policy-table entry, or an app task created without an explicit credential, is a gate failure.
- x86_64 UEFI/QEMU results are reference/emulator evidence only.
- No XBL/ABL replacement, boot-critical partition write or flash without separate explicit authorization, backups and a recovery plan.
- Hardware feature status is granted only after a native QEAFIVELS driver passes a documented physical-device test.
- Every stage publishes source, reproducible artifact, tests, logs and limitations.
