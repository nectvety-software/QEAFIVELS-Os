# QEAFIVELS OS — Linux-core migration roadmap

Canonical requirements and safety policy: `docs/INDEPENDENT_OS_CHARTER.md`.
Architecture decision approved 2026-11-25.

| Phase | Deliverable | Gate / current status |
|---|---|---|
| 1 | Linux architecture and Violet package provenance | IN PROGRESS: canonical rules updated; `package_violet.py --verify-only`, 10 manifest tests and 3 ownership-boundary tests PASS on host |
| 2 | Pinned x86_64 Linux 4.14.215 baseline | PASS: pinned upstream source/config, monolithic `bzImage`, static userspace and deterministic initramfs |
| 3 | x86_64 QEMU Linux boot and initramfs | PASS: Linux 4.14.215 boots QEAF PID 1 and powers off cleanly with separate artifacts |
| 4 | QEAF PID 1/services/rescue/QEAPP on Linux QEMU | PARTIAL: service readiness and sandboxed Puzzle lifecycle PASS; headless only, rescue/UI/persistence/update not wired |
| 5 | Reproducible Violet 4.14 kernel build | BLOCKED: exact vendor source, revision, config and DTB provenance are unavailable |
| 6 | Linux driver and QEAF service/capability parity | IN PROGRESS: Linux already owns Violet hardware; service/firmware blockers remain |
| 7 | Security, persistence, signed update and rollback | PARTIAL: QEAPP sandbox exists; durable storage, revocation and rollback are incomplete |
| 8 | Physical Violet validation | NOT AUTHORIZED: separate approval plus p77-only raw read-back and boot evidence required |

## Rules

- Linux is the approved kernel core for the whole OS. Never describe it as a
  QEAF-written kernel or load freestanding-kernel drivers into Linux userspace.
- Violet packaging must keep the kernel payload byte-for-byte equal to the
  approved stock reference until an explicit kernel selector and provenance
  contract exist.
- Host verification is not physical boot evidence. QEMU evidence is not
  Violet hardware evidence.
- The freestanding x86_64 and AArch64 kernels remain reference/regression
  tracks with independent artifact names and tests.
- QEAPP processes use Linux identity separation, descriptor hygiene,
  `no_new_privs`, seccomp and resource limits; apps never receive direct MMIO
  or privileged device access. The current x86_64 QEMU sandbox is a baseline:
  its arena is RWX, rlimit failures are unchecked, and namespaces/chroot/
  device cgroups/LSM policy are not implemented.
- No XBL/ABL replacement, boot-critical partition write or flash without
  separate explicit authorization, backups and a recovery plan.
- Every phase publishes source or binary provenance, reproducible artifacts
  where source exists, tests, logs and limitations.

## Verified x86_64/QEMU evidence (2026-11-25)

- `python scripts/build_linux_x86_64.py --skip-kernel --timeout 60`: PASS.
- `python scripts/build_linux_x86_64.py --verify-only`: PASS.
- `python -m unittest tests.test_linux_x86_64_baseline`: 21/21 PASS.
- Linux/QEAPP regression selection: 66/66 PASS.
- `build/linux-x86_64/boot.log` records service READY, Puzzle verification,
  uid/gid 20000, `no_new_privs`, seccomp, full lifecycle, service STOPPED,
  system PASS and `reboot: Power down`.
- Three negative QEMU cases PASS fail-closed: missing services, missing package
  and tampered package. Logs are under `build/linux-x86_64/negative-qemu/`.
- This is emulator evidence only. No physical x86_64 or Violet test, flash,
  partition write or commit was performed.
