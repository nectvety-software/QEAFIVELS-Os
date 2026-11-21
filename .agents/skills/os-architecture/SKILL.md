# Skill: VQEAF OS Architecture

## Goal
Maintain a coherent independent OS architecture with explicit module ownership and stable interfaces.

## Workflow
1. Inspect current source tree and architecture docs.
2. Identify owner of each responsibility: kernel, system service, framework, interface, QEAPP runtime or SDK.
3. Define a versioned C ABI or IPC schema before cross-module implementation.
4. Define startup dependencies, error handling, permissions, resource limits and shutdown semantics.
5. Update architecture, roadmap and status documents.
6. Add contract tests and migration notes.

## Boundaries
- Kernel owns scheduling, memory, interrupt, VFS, driver primitives, and per-task credentials/capabilities plus syscall authorization. Services assign app identity but must not mint capabilities on their own.
- Services own policy and privileged device operations.
- Framework offers stable app-facing APIs.
- Interface renders state and submits user intent; it does not directly manipulate drivers.
- QEAPP is a distinct package/runtime ABI.
- SDK builds, validates and deploys only over a documented protocol.

## Do not
- Introduce vendor mobile-platform components/APIs or mislabel a Linux kernel.
- Make UI depend on driver internals.
- add empty directories or speculative support claims.
- break boot protocol or QEAPP ABI without versioning.

## Deliverables
Architecture decision record, interface schema, dependency diagram, tests and accurate status.
