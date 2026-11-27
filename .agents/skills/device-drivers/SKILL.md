# Device Drivers and Capability Registry

Read .agents/AGENTS.md first. Use the v0.2 contracts in system/core/include/vqeaf.

- Linux owns production kernel probe/remove, IRQ, DMA, clocks, regulators, resets and runtime power. QEAF driver/capability registries are service-side policy and bookkeeping, not a parallel kernel driver model.
- Register QEAF providers with versioned ABI, explicit device class, bounded resources and error propagation. Never expose MMIO or privileged device nodes to apps.
- Never mark a device READY before a real Linux/QEAF provider succeeds; hardware support also requires a documented physical test.
- Publish capabilities as UNSUPPORTED, PRESENT, READY, DEGRADED, BLOCKED_FIRMWARE or ERROR.
- Do not fake Wi-Fi, Bluetooth, modem, display, battery, audio, sensors or biometrics.
- Bind hardware only after documenting SoC, bus, firmware and memory/I/O resources. Kernel changes require exact source/config provenance and a reviewed Linux patch.
- Test duplicate registration, probe failure, unbind, missing provider and capability state transitions. Keep hardware drivers separate from UI and application code.
