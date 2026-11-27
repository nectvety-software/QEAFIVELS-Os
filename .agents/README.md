# .agents — VQEAF OS Agent Pack

This folder is the canonical development guidance for coding agents working in this repository.

## Read order
1. `AGENTS.md` — always-on engineering rules and safety boundaries.
2. `prompts/MASTER_DEVELOPMENT_PROMPT.md` — full OS mission, architecture, capabilities and implementation priorities.
3. `prompts/FEATURE_BACKLOG.md` — feature inventory and acceptance evidence.
4. Relevant skill under `skills/<name>/SKILL.md` before changing that subsystem.

## Skills
- `os-architecture`: architecture and module boundaries.
- `kernel-core`: boot handoff, kernel, memory, scheduler, IPC and VFS.
- `device-drivers`: hardware abstraction, drivers and capability reporting.
- `framework-ui`: native framework, compositor, shell and screen timeout.
- `qeapp-games`: QEAPP ABI/runtime and game framework.
- `security-privacy`: permissions, biometrics, secure boot and developer mode.
- `system-apps`: file manager, clock, radio, telephony, audio, sensors and browser.
- `sdk-testing`: CLI, reproducible builds and verification.
- `violet-port`: build, package, flash and verify the aarch64 PID 1 on the Redmi Note 7 Pro (p77/recovery only, consent + two flash proofs).

## Linux-core production path and reference gates
The production Violet path is `platform/violet` on vendor Linux 4.14.215.
Verify its existing package with `python scripts/package_violet.py --verify-only`;
normal packaging writes a provenance manifest only after structural and rescue
gates pass.

`kernel/arch/aarch64` and the x86_64 `KERNEL.EFI` path are retained freestanding
reference tracks. Build the AArch64 reference with
`python scripts/build_kernel_aarch64.py` and verify it with
`python scripts/test_kernel_aarch64.py` (QEMU virt, UART markers, clean PSCI
exit, F4 credential/VFS checks). Those tests are emulator/reference evidence,
not production Linux or Violet hardware evidence.

## Important
These are implementation instructions, not claims that all listed OS features already exist. Follow `docs/STATUS.md` and mark hardware-dependent work as blocked until tested on supported hardware.
