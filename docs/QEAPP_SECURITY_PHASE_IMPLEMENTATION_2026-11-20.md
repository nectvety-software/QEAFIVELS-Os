# QEAPP Security Phase — implementation status

Date: 2026-11-20
Project: QEAFIVELS OS / Violet (SM6150 target)

## Implemented in this change

- New QEAPP installs start with a zero grant mask. `ui.display` is no longer silently granted.
- Updates preserve only grants still requested by the updated manifest; newly requested permissions are not granted automatically.
- Before restoring grants or app-private data from the p77 catalog for a newly installed record, the stored package is re-inspected and its manifest ID must match the incoming package ID. The catalog's storage key is still the package filename, so this identity check is required to prevent a filename collision from inheriting grants/data.
- The device runner launch interface now accepts an effective grant mask. `device_exec` intersects that mask with the permissions declared in the signed package before exposing it through API permission queries and display API checks.
- The rescue-channel direct QEAPP execution path passes a zero grant mask. It must not bypass the Permission Manager.
- Native execution is still fail-closed, but no longer blanket-refused: see "Native process sandbox" below. The child applies `vqeaf_qeapp_sandbox_apply()` and only then enters module code.
- Renamed the device canvas height constant to `VQEAF_QEAPP_DEVICE_HEIGHT` to remove its collision with the header include guard.

## Verification

- `python scripts/build_violet.py`: PASS, linked `build/violet/vqeaf-init` (9,621,728 bytes, 265 objects).
- `python -m unittest tests.test_qeapp_security_contract tests.test_qeapp tests.test_qeapp_native -q`: PASS, 69 tests.
- Added `tests/test_qeapp_security_contract.py` (6 source-contract regression checks).

The new tests are source-contract tests. They do not prove kernel credential changes, seccomp behavior, actual p77 read/write across reboot, live revocation, or target-phone execution.

## Native process sandbox (2026-11-20, later the same day)

`platform/violet/ui/vqeaf_qeapp_sandbox.c` replaces the unconditional block in
`vqeaf_qeapp_device_launch()`. The forked child must clear four layers before
`device_exec()` runs, and the launcher refuses to start module code if any layer
is refused:

1. `PR_SET_NO_NEW_PRIVS`, so no setuid binary can raise the child again.
2. `setgroups(0)` + `setgid(20000)` + `setuid(20000)`: the app no longer shares
   the shell's (uid 0) identity.
3. Descriptor sweep above fd 2, `RLIMIT_CORE=0`, `RLIMIT_NOFILE=64`, `umask(077)`,
   so no display/USB/LUN handle from the shell survives the fork.
4. A seccomp allowlist (`SECCOMP_MODE_FILTER`) of the syscalls the loader and a
   module need; everything else returns `EPERM` to the caller instead of killing
   the process.

Evidence so far: `tests/vqeaf_qeapp_sandbox_host.c` runs this exact source on a
desktop Linux kernel and passes (child uid != 0, `open()`/`fork()`/`execve()` →
`EPERM`, `write()` on the report descriptor still works), `scripts/build_violet.py`
links the image, and `scripts/test.py` passes. **Not yet proven on the handset:**
the SM6150 kernel's `CONFIG_SECCOMP_FILTER` and the SELinux capability set for
`setuid`. If the phone refuses either layer the app stays blocked with a precise
`sandbox: … failed` reason instead of running unsandboxed.

Still missing for third-party apps: mount/landlock filesystem isolation, an API
broker for dynamic grants, and a supervisor able to revoke a running app.

## Not implemented / release blockers

1. **Execution identity:** no dedicated native-app UID/GID has been established. The current package path uses a stock kernel and no verified target user database/credential policy.
2. **Seccomp:** no target-kernel config/build artifact is present in this repository to prove `CONFIG_SECCOMP` and `CONFIG_SECCOMP_FILTER` are enabled. `temp/mt689x/chopin_defconfig` is a donor MT689x config and is not evidence for the SM6150/Violet target. The repository's `kernel/` directory is a small freestanding kernel tree, not a Linux kernel source tree with seccomp implementation.
3. **Filesystem/device isolation:** no mount namespace/chroot/landlock/device broker or inherited-handle allowlist has been installed for native apps.
4. **API grant enforcement:** display and permission-query callbacks consume the passed effective mask when the device runner is eventually enabled. Storage callbacks still deny access unconditionally because there is no safe storage handle/broker. There is no IPC broker for dynamic grant changes.
5. **Live revocation:** QEAPP execution is currently a synchronous child run and the launcher is blocked. There is no long-lived app supervisor capable of revoking an already-running process or terminating it on permission withdrawal.
6. **Persistence/reboot tests:** the appstore v4 serialization path stores grant bits, but this change has not yet added an integration test against the actual p77 block device or a cold reboot on the handset.

## Important platform boundary

`scripts/package_violet.py` packages the QEAF ramdisk with the stock Violet boot image/kernel supplied by the external QeafdropOs workspace. This is not yet a standalone QEAF kernel image. Do not enable native execution or claim an independent-OS sandbox until the actual target kernel and process model are integrated and tested.

## Next required engineering work

- Decide whether native applications execute as Linux processes under the stock kernel or as EL0 tasks under the freestanding QEAF kernel. Do not mix the two security models.
- For the Linux-process route, obtain/build the exact SM6150 kernel and verify its config and runtime support before choosing a syscall filter.
- For the freestanding-kernel route, implement per-task credentials/capabilities, syscall dispatch policy, VFS namespace/handle model and device-object access control in QEAF's own kernel. **Partially done:** `VqeafTask` now carries uid/gid/capabilities, the EL0 SVC dispatcher enforces a capability policy, and VFS has per-task handle tables with vnode owner/mode checks (`kernel/include/vqeaf/security.h`, `kernel/core/security.c`, `kernel/core/vfs.c`, `kernel/core/ramfs.c`; gate F4 in `scripts/test_kernel_aarch64.py`). Device-object access control is still missing: EL0 has no device syscall and no MMIO mapping, so display access stays mediated by the window syscalls.
- Then implement a capability broker with per-app process identity and a revocation channel; add target tests for UID/GID, denied syscalls, denied path/device access, grant persistence after reboot and revocation while the app remains alive.
