# Device Drivers and Capability Registry

Read .agents/AGENTS.md first. Use the v0.2 contracts in system/core/include/vqeaf.

- Register drivers with versioned ABI, explicit device class, probe/remove, bounded resources and error propagation.
- Never mark a device READY before a real successful probe.
- Publish capabilities as UNSUPPORTED, PRESENT, READY, DEGRADED, BLOCKED_FIRMWARE or ERROR.
- Do not fake Wi-Fi, Bluetooth, modem, display, battery, audio, sensors or biometrics.
- Bind hardware only after documenting SoC, bus, firmware and memory/I/O resources.
- Test duplicate registration, probe failure, unbind, missing provider and capability state transitions.
- Keep hardware drivers separate from UI and application code.
