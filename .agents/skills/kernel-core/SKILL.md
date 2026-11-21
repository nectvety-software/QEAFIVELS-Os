# Skill: Kernel Core Engineering

## Scope
Boot handoff, architecture startup, CPU exceptions, interrupt controller, timer, memory management, scheduler, IPC primitives, VFS and kernel diagnostics.

## Required method
- Keep architecture-specific code under `kernel/arch/<arch>`; common code under `kernel/core` and `kernel/include`.
- Document privilege level, calling convention, stack alignment, memory map and ownership for each entry point.
- Validate UEFI memory map descriptors before ExitBootServices; retry GetMemoryMap if the map key changes.
- Implement serial diagnostics and panic path before complex subsystems.
- Use checked page arithmetic, alignment and overflow-safe allocation.
- Keep IRQ handlers bounded; defer noncritical work.
- Define process/thread states, scheduling policy and synchronization invariants before adding concurrency.
- Design IPC with message bounds, caller identity, capability checks and timeout/cancellation.
- VFS must enforce mount boundaries, permissions, canonical paths and safe symlink handling.

## Credentials, syscall authorization and VFS enforcement (AArch64 kernel, F4)
- Every task carries `VqeafCred {uid,gid,caps}` (`kernel/include/vqeaf/security.h`). Kernel tasks and kernel-internal EL0 harnesses are uid 0 + `VQEAF_CAP_ALL`; user tasks get an explicit credential and `vqeaf_task_create_user_cred()` rejects NULL.
- The EL0 SVC dispatcher (`kernel/arch/aarch64/irq.c`) resolves the required capability from `kernel/core/security.c` before reading arguments, returns `-EACCES` and prints `SEC: deny uid= sys= need=`. Keep the policy table keyed to the `vqeaf/el0_abi.h` numbers, never to literals.
- Validate every user buffer with `vqeaf_vm_user_range_mapped()` and copy through a bounded kernel buffer; never keep a user pointer across a syscall.
- VFS: `VqeafVnode` has uid/gid/mode as nine-bit rwx triplets. Handles are per task slot (`VQEAF_VFS_HANDLE_MAX`); open checks owner/mode, read/write re-check, and `vqeaf_task_exit()` releases the slot's whole table.
- Gate: `python scripts/test_kernel_aarch64.py` must stay ALL OK; it asserts the SEC/VFS markers (allowed and denied syscalls, cross-app `EACCES`, read-only write refusal, per-slot handle isolation) and that a restricted app still completes its UI chain.
- Never add an EL0 syscall without a policy row and a denial test, and never let an app inherit the system identity or `VQEAF_CAP_FS_ADMIN`.

## Tests
Boot handoff validation, allocator fragmentation/exhaustion, page mapping/unmapping, invalid pointers, timer wrap, scheduler fairness, IPC malformed messages, VFS traversal and panic output. Kernel security: capability allow/deny per syscall, cross-app path denial, read-only file write denial, handle-table isolation and release on exit (`scripts/test_kernel_aarch64.py`).

## Safety
No kernel code may assume a phone peripheral exists. No direct device writes on physical hardware without explicit approval.
