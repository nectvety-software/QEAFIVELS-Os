# QEAFRESCUE v3 — Architecture & Validation Report

## 1. What was inspected
- `scripts/qeafrescue_data.py` (v2): real backup/restore/archive verifier and
  append-only JSONL logging. Reused as the storage engine; **no second backup
  implementation was created**.
- `tests/test_qeafrescue_data.py`: existing v2 unit tests, kept intact.
- `docs/QEAFRESCUE_V2_BACKUP_RESTORE_LOG.md`: v2 behaviour reference.
- `system/ui/recovery_screen.c` + `recovery/core/recovery_main.c`: current
  Recovery UI is a **C/UEFI x86_64 reference** driven by firmware keyboard; it
  renders via GOP and does not do file I/O. QEAFRESCUE v2/v3 data services are
  **host-side Python**, not linked into that UEFI binary.
- `system/core/capability.c`, `kernel/core/security.c`: existing capability
  model exists, but no device-side file service uses it for backup/restore.

Conclusion: integrating a host-side Python service directly into the x86_64
UEFI C recovery binary would create a second runtime and fake the integration.
v3 therefore implements a **real service layer over v2** with a text Recovery
UI, while the device-side graphical integration is reported as NOT IMPLEMENTED.

## 2. Implemented components (`scripts/qeafrescue_v3.py`)
| Component | Implementation |
| --- | --- |
| PermissionManager | Path-scoped grants, symlink rejection, path-traversal and sensitive-name blocks (`efs`, `persist`, `modem`, `fsg`, `nv`, `calibration`, ...), raw `/dev/*` refusal, read/write checked separately |
| StorageManager | `shutil.disk_usage`, mount status from `/proc/mounts`, no automatic mount |
| RecoveryLogService | Append-only JSONL with session/transaction/severity/error_code; tolerant reader skips invalid UTF-8, truncated and malformed lines; export to `recovery.jsonl`/`recovery.txt` without modifying source |
| TransactionJournal | `recovery-logs/transactions/<id>.json` with required fields; `list_interrupted()` detects interrupted operations |
| ArchiveVerifier | Wraps v2 `inspect_backup` (full SHA-256 + zlib CRC + manifest checks) |
| BackupService | States IDLE→SCANNING→CHECKING_STORAGE→BACKUP_RUNNING→BACKUP_VERIFYING→BACKUP_COMPLETE; progress callback; cancellation; temp cleanup; fail-closed on insufficient space |
| RestoreService | RESTORE_VALIDATING→CHECKING_STORAGE→RESTORE_RUNNING→RESTORE_VERIFYING→RESTORE_COMPLETE; dry-run; staging directory and atomic commit from v2; no overwrite/merge |
| Recovery UI (text) | `menu/backup/restore/verify/logs/export/storage/interrupted` commands; prints `Partition Installer: LOCKED` |

No partition write, erase, format, raw block write, fastboot flash, GPT or
bootloader code was added. `test_no_partition_write_api` asserts this.

## 3. Tests
Command:
```powershell
python -m unittest tests.test_qeafrescue_v3 -v
```
Result: **27 tests, OK** (host unit tests). Covers backup normal/empty/large/
insufficient/permission/symlink/hash-mismatch/cancel/temp cleanup; restore
valid/corrupt/dry-run/existing/interrupted/inventory; log viewer valid/
truncated/invalid UTF-8/empty/filters/export; permission denial; journal
interruption; Partition Installer locked.

CLI smoke test used real temp files: backup → verify → restore dry-run → logs →
TXT export → storage → interrupted. All commands returned real data.

## 4. Build / RAM-BOOT
- No boot-image source change was needed for the host-side v3 service layer.
- Existing `dist\boot-vqeaf-violet.img` SHA-256 was verified earlier as
  `72d3b71d5e49154f8a0320c4a0839d86c9cf4d2de073dc78adb4047771431884`.
- RAM-BOOT device test: **NOT TESTED** — `qeafrescue.py detect` reported no ADB
  and no Fastboot device; per the task rules, hardware testing was stopped and
  no automatic reboot/flash was performed.

## 5. Acceptance report
| Hạng mục | Kết quả |
| --- | --- |
| Recovery UI (graphical, device) | NOT IMPLEMENTED |
| Backup Screen | NOT IMPLEMENTED (CLI service available) |
| Restore Screen | NOT IMPLEMENTED (CLI service available) |
| Log Viewer | PASS (host service/CLI) |
| Log Export JSONL/TXT | PASS |
| SHA-256 verification | PASS |
| Storage permissions | PASS (host unit tests) |
| Power-loss handling | PARTIAL (journal + v2 staging; no real power-cut test) |
| Interrupted restore | PASS (journal detection + cancellation tests) |
| Unit tests | PASS (27 tests) |
| Emulator | NOT TESTED |
| RAM BOOT | NOT TESTED (no Fastboot device) |
| Physical device | NOT TESTED |
| Partition Installer | LOCKED |

## 6. Unresolved / next milestone
1. Port the v3 service contracts into a device-side C service and add actual
   Backup/Restore/Log screens to the violet LVGL recovery UI. This needs a real
   writable, permission-granted storage root on the device; no EFS/persist.
2. Run `fastboot boot` only after the device is connected and verified as violet
   with unlocked bootloader.
3. Real power-loss test requires hardware instrumentation; journal logic is
   unit-tested only.

Data integrity and permission checks were prioritised over adding UI surface.
No result was fabricated.
