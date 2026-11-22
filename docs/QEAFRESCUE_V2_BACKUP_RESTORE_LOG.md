# QEAFRESCUE v2 — Backup / Restore / Recovery Log

## Safety contract
Host-side file backup/restore and structured recovery events. Operates only on regular files in explicitly selected directories. No block-device, partition, Fastboot or raw-device API.

**Partition Installer remains LOCKED.** Restore writes files only into a new host directory. No partition write, erase, format, update, EFS, persist, modem, calibration or NV operation exists here.

## PowerShell commands (repository root)
- Backup: python scripts\qeafrescue_data.py backup --source .\config-snapshot --archive .\backups\config.qrb
- Verify: python scripts\qeafrescue_data.py verify --archive .\backups\config.qrb
- Dry-run restore: python scripts\qeafrescue_data.py restore --archive .\backups\config.qrb --destination .\restore-staging
- Execute restore to a NEW destination: python scripts\qeafrescue_data.py restore --archive .\backups\config.qrb --destination .\restore-staging --execute
- Append log: python scripts\qeafrescue_data.py log --dir .\recovery-logs --event BACKUP_COMPLETE --status OK --detail "archive verified"

## Backup format
ZIP container with QEAFRESCUE-MANIFEST.json. Each regular file is inventoried by normalized relative path, byte size and SHA-256. Symlinks/special files rejected. Limits: 100,000 files and 2 GiB total. Archive is written to a temporary sibling then atomically renamed. Existing archive paths are never overwritten.

## Restore behavior
Full manifest, content hash and ZIP CRC validation precedes restore. Dry-run is default. Execution stages files in a fresh sibling directory, verifies each restored file, then renames staging to destination. Existing destinations are rejected; no merge/overwrite mode exists.

## Recovery Log
recovery.jsonl is append-only JSON Lines: timestamp, session ID, event, status and detail. Status values INFO, OK, WARN, ERROR, BLOCKED. Each append is flushed and fsync'd. These are host-side tool logs, not kernel/phone hardware logs.

## Validation
Run: python -m unittest discover -s tests -p test_qeafrescue_data.py -v
Tests cover archive round-trip, dry-run, overwrite refusal, corruption detection, traversal rejection, symlink rejection and JSONL logging. Backup/verify/restore CLI actions also append success/failure events to ./recovery-logs/recovery.jsonl relative to the current working directory. Host tests do not prove physical-device storage compatibility.

## Integration boundary
This v2 deliverable is a host-side rescue tool. The on-device Recovery UI and boot-time log viewer/export are not wired to this CLI yet; those are the next integration phase. Partition Installer remains disabled.
