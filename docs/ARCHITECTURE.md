# QEAFIVELS OS Architecture

QEAFIVELS OS is a freestanding, independent operating system. Its production phone target is ARM64 Violet. It must not depend on Linux kernel/userspace, a vendor mobile-platform stack, vendor app runtime, vendor framework/HAL, or a Linux distribution at runtime.

The canonical architecture, hardware confidence ledger, boot safety policy and stage gates are maintained in `docs/INDEPENDENT_OS_CHARTER.md`.

## Current repository components

- `kernel/arch/x86_64`: existing freestanding UEFI reference implementation. Useful for host/QEMU kernel algorithm development; not the Violet target.
- `bootloader/core`: existing x86_64 UEFI boot manager and handoff. Not a Violet bootloader.
- `recovery/core`: existing x86_64 standalone recovery reference.
- `kernel/core`: architecture-neutral memory/scheduler/IPC/VFS primitives; audit each module before ARM64 reuse. `security.c` (capability policy), `vfs.c` (per-task handles, owner/mode checks) and `ramfs.c` (kernel root namespace) are shared by the AArch64 kernel.
- `kernel/arch/aarch64`: freestanding AArch64 kernel (QEMU virt) with entry/vectors, MMU, GICv2, generic timer, preemptive scheduler, ELF/.qen loader, IPC, compositor/window manager and the F4 credential model. Gate: `scripts/test_kernel_aarch64.py` (emulated only; no Violet hardware evidence).
- `system/core`, `framework`, `runtime/qeapp`, `sdk`: native OS work in progress; inspect implementation status before claiming feature completeness.
- `platform/lvgl`: vendored LVGL library. It is a UI toolkit dependency only, not an OS kernel or runtime.
- `platform/violet`: currently contains a Linux-musl PID 1/initramfs transport prototype using the stock kernel. It is explicitly legacy/interim and is not QEAFIVELS Kernel.

## Kernel strategy

The phone target requires a new freestanding AArch64 kernel, entered through a documented boot transport. Implement ARM64 exception vectors, MMU/page tables, GIC, generic timer, allocator, scheduler, IPC and syscall ABI independently. Do not link musl or invoke Linux syscalls in the target kernel.

### Kernel security model (AArch64, milestone F4)

Threat model: a hostile or buggy EL0 process; EL1 (the kernel) is trusted. The trust boundary is the `svc` instruction and every user pointer crossing it.

- **Identity.** Each `VqeafTask` carries `VqeafCred {uid, gid, caps}` (`kernel/include/vqeaf/security.h`). Kernel tasks and kernel-internal EL0 harnesses run as uid 0 with `VQEAF_CAP_ALL`. AMS gives every `.qen` app its own uid in the app band plus a capability bundle (`VQEAF_CAPS_APP_UI`, `VQEAF_CAPS_APP_HEADLESS`); apps never receive `VQEAF_CAP_SYSTEM` or `VQEAF_CAP_FS_ADMIN`. `vqeaf_task_create_user_cred()` rejects a NULL credential, so no user task can silently inherit the system identity.
- **Authorization.** `kernel/core/security.c` maps each syscall number (from `vqeaf/el0_abi.h`) to the capability it needs; the EL0 dispatcher in `kernel/arch/aarch64/irq.c` checks that before reading arguments, returns `-EACCES`, and prints `SEC: deny uid= sys= need=` as an audit line. User buffers are validated with `vqeaf_vm_user_range_mapped()` and copied through a bounded kernel buffer.
- **VFS.** `VqeafVnode` carries uid/gid/mode (nine-bit rwx triplets). Handles live in a per-task-slot table (`VQEAF_VFS_HANDLE_MAX` per task); open checks owner/mode, read/write re-check it, and task exit releases the whole slot. Root namespace today is `/system/version` (0444) and `/data/<uid>/private` (0600, RAM only).
- **Failure mode.** Denial is a returned error plus an audit line; kernel state is untouched and no partial copy is performed.
- **Evidence.** `scripts/test_kernel_aarch64.py` asserts the boot self-test markers (allowed vs denied syscalls, cross-app open refused with EACCES, read-only write refused, per-slot handle isolation) and that the restricted window app still completes the F3 chain. Emulated QEMU virt evidence only.
- **Not implemented:** revocation of a running process, persistent storage behind `/data`, device-object broker (EL0 has no device syscall and no MMIO mapping), and per-app identity on Violet hardware.

## Hardware truth

See `docs/VIOLET_HARDWARE.md` and `docs/INDEPENDENT_OS_CHARTER.md`. Existing DRM/KMS, evdev, configfs USB gadget and sysfs observations were made under the stock Linux kernel and are not native-driver implementations. The local storage dump identifies mmcblk0/eMMC, not UFS. Resolve the SM6150/SDM660 discrepancy from the actual device before silicon-specific drivers.

## Release claims

Never call the current x86_64 QEMU image a Violet build. Never call the current Violet Linux PID 1 image an independent OS. Never claim hardware support from emulator tests or UI screenshots. Every stage needs a reproducible artifact and evidence.
