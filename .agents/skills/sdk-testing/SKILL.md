# Skill: VQEAF SDK, Build and Verification

## CLI contract
Commands: create, build, validate, package, sign, install, launch, logs, devices, doctor, simulate. A command not implemented must return nonzero with a clear NOT IMPLEMENTED message. Never return success for a skipped operation.

## Build
Use reproducible builds and documented toolchain/config files. The production Violet path reuses verified vendor Linux 4.14 binary provenance until exact source is available. The separate x86_64 Linux 4.14.215 QEMU baseline is implemented under `platform/linux-x86_64/` and gated by `scripts/build_linux_x86_64.py` plus `tests/test_linux_x86_64_baseline.py`; keep its artifacts distinct from the freestanding x86_64/AArch64 reference tracks. Add ARM64 cross-builds only when toolchain and target ABI are verified.

## Testing layers
- Unit tests for core algorithms and policy.
- Contract tests for ABI/IPC/manifest.
- Integration tests for service startup and permissions.
- QEMU boot tests for normal/recovery/safe/diagnostics.
- Virtual device tests for unsupported hardware behavior.
- Hardware-in-loop tests only on an explicitly authorized test device.

## Reporting
Capture compiler version, build configuration, exact command, exit code, test count, logs and artifact hashes. Distinguish emulated evidence from physical hardware evidence.
