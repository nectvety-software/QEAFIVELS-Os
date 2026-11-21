# VQEAF OS Feature Backlog

Priorities: P0 foundation, P1 essential user experience, P2 hardware integrations, P3 advanced ecosystem.

| Area | Features | Priority | Acceptance evidence |
|---|---|---:|---|
| Violet port | touch input, interactive phone shell on 720x1560, prefs persist (p77 slot), app runtime | P1 | HOMOLOG/snap evidence on the device; touch round-trip ≤1 px via vio_panel_touch_map |
| Kernel Core | MMU, allocator, exceptions, scheduler, IPC, VFS, block layer | P0 | Unit/integration tests and QEMU stress; AArch64 gate `scripts/test_kernel_aarch64.py` (milestones A–G1) |
| Kernel security | per-task uid/gid/capabilities, capability-gated syscall dispatch, per-task VFS handles with owner/mode | P0 | F4 checks in `scripts/test_kernel_aarch64.py`: allowed/denied syscalls, cross-app `EACCES`, read-only write refusal, handle isolation. Revocation, device-object broker and persistent app storage remain NOT IMPLEMENTED |
| Boot/Recovery | boot modes, verified metadata, rollback, independent recovery | P0 | Normal/recovery/failure boot tests |
| Driver Model | probe, bind, capability registry, lifecycle, power states | P0 | Virtual device tests and hotplug |
| Display/Touch | framebuffer/display, brightness, multitouch, gestures | P1 | QEMU input tests + real device test |
| Power | battery metrics, charging, thermal, suspend/resume | P1 | Sensor-backed telemetry, sleep/wake tests |
| Screen timeout | 15s, 1m, 5m, 30m, never | P1 | Fake-clock tests and display blank/wake test |
| Storage/File Manager | volumes, folders, MIME, copy/move/search/trash | P1 | Permission/path traversal tests |
| Clock | RTC, timezone, alarms, stopwatch, timer | P1 | DST, reboot and scheduling tests |
| Audio | speaker, earpiece, microphone, routes, volume | P1 | Loopback and hardware route tests |
| Wi-Fi | scan/connect, saved networks, airplane mode | P2 | Real driver/firmware integration |
| Bluetooth | discovery, pairing, audio profiles | P2 | Real controller and stack tests |
| Telephony | SIM, registration, calls, emergency routing | P2 | Supported modem integration only |
| Biometrics | fingerprint; secure face with liveness | P2 | TEE-backed security evaluation |
| Sensors | proximity, light, accel, gyro, compass, GNSS | P2 | Driver and calibration tests |
| QEAPP | runtime, ABI, sandbox, permissions, resources | P1 | malicious package and isolation tests |
| Game Core | fixed timestep, scene, sprite, tilemap, audio, saves | P1 | deterministic simulation tests |
| Browser | tabs, TLS, history, downloads, privacy | P3 | web compatibility and security tests |
| Developer Mode | logs, profiling, debug transport, app sideload | P2 | consent, audit, disable/revoke tests |
| PC SDK/Tool | simulator, device manager, logs, package signing | P2 | host integration tests |

## Rules for feature state
- Implemented: source, tests, and integration evidence.
- Partial: code exists but missing integration or test.
- Not implemented: no code; never imply support.
- Hardware blocked: implementation waits for exact driver, firmware, modem or secure hardware.
