# Skill: VQEAF OS Architecture

## Goal
Maintain a coherent Linux-core QEAFIVELS architecture with explicit module ownership and stable interfaces.

## Workflow
1. Inspect current source tree and architecture docs.
2. Identify owner of each responsibility: kernel, system service, framework, interface, QEAPP runtime or SDK.
3. Define a versioned C ABI or IPC schema before cross-module implementation.
4. Define startup dependencies, error handling, permissions, resource limits and shutdown semantics.
5. Update architecture, roadmap and status documents.
6. Add contract tests and migration notes.

## Boundaries
- Linux owns scheduling, memory, interrupts, VFS/filesystems, namespaces, kernel security primitives and kernel drivers. Violet uses vendor Linux 4.14.215; x86_64/QEMU uses the separate pinned upstream Linux 4.14.215 baseline under `platform/linux-x86_64/`.
- QEAF PID 1 owns init lifecycle and service supervision. Services own policy, capability reporting and privileged device operations.
- Framework offers stable app-facing APIs. Interface renders state and submits user intent; it does not directly manipulate drivers.
- QEAPP is a distinct package/runtime ABI with Linux process isolation, `no_new_privs`, seccomp, descriptor hygiene and resource limits.
- SDK builds, validates and deploys only over a documented protocol.
- The freestanding x86_64/AArch64 kernels are reference tracks, not the production Linux core.

## Do not
- Introduce Android/AOSP/ART, vendor mobile-platform components/APIs, APK-style packages or vendor runtimes as the userspace foundation.
- Mislabel Linux as a QEAF-written kernel or load freestanding-kernel drivers into Linux userspace.
- Make UI depend on driver internals or give apps direct MMIO/privileged device access.
- Add empty directories or speculative support claims.
- Break boot protocol or QEAPP ABI without versioning.

## Deliverables
Architecture decision record, interface schema, dependency diagram, tests and accurate status.
