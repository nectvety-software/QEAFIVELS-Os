# Skill: VQEAF Security and Privacy

## Threat model first
Identify attacker, protected assets, trust boundaries, physical-access assumptions, recovery path and residual risk before implementing security-sensitive features.

## Required controls
Privilege separation, IPC authorization, least-privilege app permissions, sandbox, signed package/update verification, secure boot where supported, rollback policy, protected storage, audit log and secret redaction.

## Biometrics
Fingerprint: use vendor/TEE-backed matching and secure enrollment; never export templates.
Face: secure face unlock requires liveness/anti-spoofing and trusted sensor/TEE. Camera face detection alone is not authentication. If unavailable, report unsupported and use PIN/password fallback.

## Developer mode
Explicit opt-in with authentication, persistent audit event, visible indicator, scoped debug permissions and clear disable/revoke flow. It must not silently bypass verified boot, lock screen or app sandbox.

## Browser and network
TLS certificate validation on by default, safe URL handling, download controls, permission prompts and private-session cleanup. Never log credentials, cookies, tokens or private content.

## Freestanding kernel enforcement (implemented, milestone F4)
Threat model: a hostile or buggy EL0 process on the QEAFIVELS AArch64 kernel; EL1 is trusted. Trust boundary is the `svc` instruction and each user pointer crossing it.

- **Identity:** per-task uid/gid/capability mask. Apps get a per-app uid in the app band and a capability bundle (`VQEAF_CAPS_APP_UI`, `VQEAF_CAPS_APP_HEADLESS`); they never hold `VQEAF_CAP_SYSTEM` or `VQEAF_CAP_FS_ADMIN`. `vqeaf_task_create_user_cred()` rejects NULL so a kernel-internal harness is the only way to get system credentials.
- **Authorization:** capability policy at syscall dispatch (before arguments are read), plus vnode owner/mode checks on every VFS open and every read/write. A capability alone never crosses an app boundary because the file mode is checked separately.
- **Failure mode:** denial returns `-EACCES` and prints `SEC: deny uid= sys= need=`; no partial copy and no state change.
- **Verification:** `scripts/test_kernel_aarch64.py` (QEMU virt, emulated) asserts allowed/denied syscalls, cross-app open refusal, read-only file write refusal and per-task handle isolation. Record evidence in `docs/STATUS.md`, never from a build alone.
- **Not implemented — do not claim:** revocation of a running process, persistent app storage, a device-object broker (EL0 has no device syscall and no MMIO mapping), and per-app identity on Violet hardware.
- **Review rule:** any new EL0 syscall needs a policy-table entry, a documented capability and a denial test; any new vnode provider must set uid/gid/mode before it is mounted.

## Tests
Tampered signatures, revoked keys, replay/rollback, malicious QEAPP paths, IPC impersonation, permission revocation, lockout/rate limits, developer-mode disable, and kernel-level capability/ownership denials (`scripts/test_kernel_aarch64.py`).
