# Skill: VQEAF SDK, Build and Verification

## CLI contract
Commands: create, build, validate, package, sign, install, launch, logs, devices, doctor, simulate. A command not implemented must return nonzero with a clear NOT IMPLEMENTED message. Never return success for a skipped operation.

## Build
Use reproducible CMake/native builds and documented toolchain files. Keep QEMU x86_64 boot tests as mandatory regression. Add ARM64 cross-build only when toolchain and target ABI are verified.

## Testing layers
- Unit tests for core algorithms and policy.
- Contract tests for ABI/IPC/manifest.
- Integration tests for service startup and permissions.
- QEMU boot tests for normal/recovery/safe/diagnostics.
- Virtual device tests for unsupported hardware behavior.
- Hardware-in-loop tests only on an explicitly authorized test device.

## Reporting
Capture compiler version, build configuration, exact command, exit code, test count, logs and artifact hashes. Distinguish emulated evidence from physical hardware evidence.
