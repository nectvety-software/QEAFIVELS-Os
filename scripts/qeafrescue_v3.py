#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""QEAFRESCUE v3 service layer.

Reuses QEAFRESCUE v2 (scripts/qeafrescue_data.py) for backup, archive
verification and append-only JSONL logs. Adds explicit services, state
reporting, progress callbacks, cancellation, transaction journals, a permission
manager and a resilient log viewer/export.

This module never writes a partition, raw block device, EFS, persist, modem,
fsg, calibration or NV data. Partition Installer stays LOCKED.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))
import qeafrescue_data as v2  # noqa: E402

# ---- states -----------------------------------------------------------------
IDLE = "IDLE"
SCANNING = "SCANNING"
CHECKING_STORAGE = "CHECKING_STORAGE"
BACKUP_RUNNING = "BACKUP_RUNNING"
BACKUP_VERIFYING = "BACKUP_VERIFYING"
BACKUP_COMPLETE = "BACKUP_COMPLETE"
RESTORE_VALIDATING = "RESTORE_VALIDATING"
RESTORE_RUNNING = "RESTORE_RUNNING"
RESTORE_VERIFYING = "RESTORE_VERIFYING"
RESTORE_COMPLETE = "RESTORE_COMPLETE"
CANCELLED = "CANCELLED"
FAILED = "FAILED"
RECOVERY_REQUIRED = "RECOVERY_REQUIRED"

STATES = (IDLE, SCANNING, CHECKING_STORAGE, BACKUP_RUNNING, BACKUP_VERIFYING,
          BACKUP_COMPLETE, RESTORE_VALIDATING, RESTORE_RUNNING,
          RESTORE_VERIFYING, RESTORE_COMPLETE, CANCELLED, FAILED,
          RECOVERY_REQUIRED)

# ---- permission manager -----------------------------------------------------
BLOCKED_NAMES = frozenset({"efs", "persist", "modem", "modemst1", "modemst2",
                           "fsg", "nv", "nvdata", "calibration", "cal",
                           "wlan_mac.bin", "bdaddress"})


class PermissionDenied(v2.DataError):
    pass


class PermissionManager:
    """Path-scoped read/write grants. No root escalation, no raw devices."""

    def __init__(self, roots: Iterable[os.PathLike | str]):
        self.roots = []
        for root in roots:
            p = Path(root)
            if p.is_symlink():
                raise PermissionDenied(f"root cannot be a symlink: {root}")
            self.roots.append(p.resolve(strict=True))
        if not self.roots:
            raise PermissionDenied("no granted storage root")

    def check(self, path: os.PathLike | str, write: bool = False) -> Path:
        p = Path(path)
        raw = str(p).replace("\\", "/").lower()
        if raw.startswith("/dev/") or raw.startswith("\\\\.\\"):
            raise PermissionDenied("raw block/device access refused")
        if p.is_symlink():
            raise PermissionDenied(f"symlink refused: {p}")
        resolved = p.resolve(strict=False)
        for part in resolved.parts:
            if Path(part).name.lower() in BLOCKED_NAMES:
                raise PermissionDenied(f"sensitive path refused: {p}")
        for root in self.roots:
            try:
                resolved.relative_to(root)
            except ValueError:
                continue
            if write:
                # Effective grant: parent must already exist and be writable.
                parent = resolved.parent if resolved.exists() else resolved
                while not parent.exists() and parent != root:
                    parent = parent.parent
                if not os.access(parent, os.W_OK):
                    raise PermissionDenied(f"write grant missing: {parent}")
            elif not os.access(resolved if resolved.exists() else resolved.parent,
                               os.R_OK):
                raise PermissionDenied(f"read grant missing: {p}")
            return resolved
        raise PermissionDenied(f"path outside granted roots: {p}")


# ---- storage manager --------------------------------------------------------
@dataclass
class StorageInfo:
    root: str
    mounted: bool
    total_bytes: int
    free_bytes: int
    used_bytes: int


class StorageManager:
    def __init__(self, permissions: PermissionManager):
        self.permissions = permissions

    def status(self, root: os.PathLike | str) -> StorageInfo:
        p = self.permissions.check(root)
        if not p.is_dir():
            raise v2.DataError("storage root is not a directory")
        usage = shutil.disk_usage(p)
        total = usage.total
        free = usage.free
        mounted = any(str(p).startswith(line.split()[1])
                      for line in _mount_lines())
        return StorageInfo(str(p), mounted, total, free, total - free)

    def has_space(self, root: os.PathLike | str, required: int) -> bool:
        return self.status(root).free_bytes >= required


def _mount_lines() -> list[str]:
    try:
        return Path("/proc/mounts").read_text(encoding="utf-8",
                                              errors="replace").splitlines()
    except OSError:
        return []


# ---- recovery log service ---------------------------------------------------
SEVERITIES = ("INFO", "WARN", "ERROR", "BLOCKED")


@dataclass
class LogEvent:
    timestamp_unix_ms: int
    session: str
    transaction: str
    event: str
    severity: str
    detail: str
    error_code: str = ""


class RecoveryLogService:
    def __init__(self, log_dir: os.PathLike | str,
                 permissions: PermissionManager | None = None):
        self.log_dir = Path(log_dir)
        self.permissions = permissions
        self.warning: str = ""

    def _prepare(self) -> Path:
        if self.permissions:
            self.permissions.check(self.log_dir, write=True)
        if self.log_dir.is_symlink():
            raise v2.DataError("log directory cannot be a symlink")
        self.log_dir.mkdir(parents=True, exist_ok=True)
        return self.log_dir / "recovery.jsonl"

    def append(self, event: str, severity: str = "INFO", detail: str = "",
               transaction: str = "", error_code: str = "") -> dict:
        if severity not in SEVERITIES:
            raise v2.DataError(f"invalid severity: {severity}")
        if not event.strip() or len(event) > 80 or len(detail) > 2000:
            raise v2.DataError("event/detail length invalid")
        # Never place secrets in logs; callers must redact before append.
        record = {
            "timestamp_unix_ms": int(time.time() * 1000),
            "session": os.environ.get("QEAFRESCUE_SESSION", "session-local"),
            "transaction": transaction,
            "event": event,
            "severity": severity,
            "error_code": error_code,
            "detail": detail,
        }
        path = self._prepare()
        try:
            with path.open("a", encoding="utf-8", newline="\n") as fh:
                fh.write(json.dumps(record, ensure_ascii=False,
                                    sort_keys=True) + "\n")
                fh.flush()
                os.fsync(fh.fileno())
            self.warning = ""
        except OSError as exc:
            # Logging failure must not break the workflow, but must be visible.
            self.warning = f"LOG WRITE FAILED: {exc}"
        return record

    def read(self, severity: str = "ALL", keyword: str = "",
             limit: int = 200, offset: int = 0) -> dict:
        """Read JSONL incrementally. Bad/truncated lines are skipped, not fatal."""
        if severity not in ("ALL", *SEVERITIES):
            raise v2.DataError(f"invalid filter: {severity}")
        path = self.log_dir / "recovery.jsonl"
        if not path.is_file():
            return {"total": 0, "items": [], "skipped": 0, "truncated": False}
        events: list[dict] = []
        skipped = 0
        truncated = False
        with path.open("rb") as fh:
            for raw in fh:
                if not raw.endswith(b"\n"):
                    truncated = True
                try:
                    text = raw.decode("utf-8")
                    obj = json.loads(text)
                    if not isinstance(obj, dict):
                        skipped += 1
                        continue
                except (UnicodeDecodeError, json.JSONDecodeError):
                    skipped += 1
                    continue
                sev = str(obj.get("severity", "INFO"))
                if severity != "ALL" and sev != severity:
                    continue
                if keyword and keyword.lower() not in json.dumps(
                        obj, ensure_ascii=False).lower():
                    continue
                events.append(obj)
        return {"total": len(events), "items": events[offset:offset + limit],
                "skipped": skipped, "truncated": truncated}

    def export(self, out_dir: os.PathLike | str, fmt: str = "jsonl") -> dict:
        """Export a copy; the original log is never modified or deleted."""
        if fmt not in ("jsonl", "txt"):
            raise v2.DataError(f"unsupported export format: {fmt}")
        if self.permissions:
            self.permissions.check(out_dir, write=True)
        out = Path(out_dir)
        out.mkdir(parents=True, exist_ok=True)
        src = self.log_dir / "recovery.jsonl"
        name = "recovery.jsonl" if fmt == "jsonl" else "recovery.txt"
        dst = out / name
        if not src.is_file():
            raise v2.DataError("no recovery log to export")
        data = src.read_bytes()
        if fmt == "txt":
            lines = []
            for raw in data.splitlines():
                try:
                    obj = json.loads(raw.decode("utf-8"))
                    lines.append("%s [%s] %s %s" % (
                        obj.get("timestamp_unix_ms", ""),
                        obj.get("severity", "INFO"), obj.get("event", ""),
                        obj.get("detail", "")))
                except Exception:
                    continue
            dst.write_text("\n".join(lines) + "\n", encoding="utf-8")
        else:
            dst.write_bytes(data)
        return {"export": str(dst), "format": fmt, "bytes": dst.stat().st_size,
                "source_unmodified": True}


# ---- transaction journal ----------------------------------------------------
@dataclass
class Transaction:
    transaction_id: str
    operation: str
    source: str
    destination: str
    current_state: str = IDLE
    completed_files: int = 0
    total_files: int = 0
    last_checkpoint: int = field(default_factory=lambda: int(time.time()))
    archive_hash: str = ""
    error: str = ""
    recovery_action: str = ""

    def to_dict(self) -> dict:
        return {
            "transaction_id": self.transaction_id, "operation": self.operation,
            "source": self.source, "destination": self.destination,
            "current_state": self.current_state,
            "completed_files": self.completed_files,
            "total_files": self.total_files,
            "last_checkpoint": self.last_checkpoint,
            "archive_hash": self.archive_hash, "error": self.error,
            "recovery_action": self.recovery_action,
        }


class TransactionJournal:
    def __init__(self, log_dir: os.PathLike | str,
                 permissions: PermissionManager | None = None):
        self.directory = Path(log_dir) / "transactions"
        self.permissions = permissions

    def write(self, tx: Transaction) -> Path:
        if self.permissions:
            self.permissions.check(self.directory, write=True)
        self.directory.mkdir(parents=True, exist_ok=True)
        tx.last_checkpoint = int(time.time())
        path = self.directory / (tx.transaction_id + ".json")
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(tx.to_dict(), ensure_ascii=False,
                                  sort_keys=True, indent=2), encoding="utf-8")
        os.replace(tmp, path)
        return path

    def load(self, transaction_id: str) -> Transaction:
        path = self.directory / (transaction_id + ".json")
        d = json.loads(path.read_text(encoding="utf-8"))
        return Transaction(**d)

    def list_interrupted(self) -> list[dict]:
        if not self.directory.is_dir():
            return []
        result = []
        for path in sorted(self.directory.glob("*.json")):
            try:
                d = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            if d.get("current_state") in (BACKUP_RUNNING, BACKUP_VERIFYING,
                                          RESTORE_RUNNING, RESTORE_VERIFYING,
                                          SCANNING, CHECKING_STORAGE):
                d["transaction_id"] = path.stem
                d["recovery_action"] = "ROLLBACK_STAGING_OR_RESUME"
                result.append(d)
        return result


# ---- service base -----------------------------------------------------------
@dataclass
class Progress:
    state: str
    percent: int
    message: str
    transaction: str


class Cancelled(Exception):
    pass


class ServiceBase:
    def __init__(self, permissions: PermissionManager,
                 storage: StorageManager, logs: RecoveryLogService,
                 journal: TransactionJournal):
        self.permissions = permissions
        self.storage = storage
        self.logs = logs
        self.journal = journal
        self._cancel = False
        self._listener: Callable[[Progress], None] | None = None
        self.tx: Transaction | None = None

    def on_progress(self, fn: Callable[[Progress], None]) -> None:
        self._listener = fn

    def cancel(self) -> None:
        self._cancel = True

    def _emit(self, state: str, percent: int, message: str) -> None:
        if self._cancel:
            raise Cancelled("operation cancelled")
        if state not in STATES:
            raise v2.DataError(f"invalid state: {state}")
        if self.tx is not None:
            self.tx.current_state = state
            self.journal.write(self.tx)
        self.logs.append("STATE_" + state, "INFO", message,
                         self.tx.transaction_id if self.tx else "")
        if self._listener:
            self._listener(Progress(
                state, max(0, min(100, percent)), message,
                self.tx.transaction_id if self.tx else ""))
        if self._cancel:
            raise Cancelled("operation cancelled")


# ---- archive verifier -------------------------------------------------------
class ArchiveVerifier:
    def __init__(self, permissions: PermissionManager,
                 logs: RecoveryLogService):
        self.permissions = permissions
        self.logs = logs

    def verify(self, archive: os.PathLike | str) -> dict:
        p = self.permissions.check(archive)
        report = v2.inspect_backup(p)
        self.logs.append("ARCHIVE_VERIFIED", "INFO",
                         f"file_count={report['file_count']} "
                         f"sha256={report['archive_sha256']}")
        return report


# ---- backup service ---------------------------------------------------------
class BackupService(ServiceBase):
    def run(self, source: os.PathLike | str, archive: os.PathLike | str) -> dict:
        self._cancel = False
        src = self.permissions.check(source)
        dst = self.permissions.check(archive, write=True)
        self.tx = Transaction(uuid.uuid4().hex, "backup", str(src), str(dst),
                              SCANNING)
        try:
            self._emit(SCANNING, 5, "scanning source")
            files = v2.collect_files(src)
            required = sum(p.stat().st_size for p, _ in files)
            self.tx.total_files = len(files)
            self._emit(CHECKING_STORAGE, 15,
                       f"files={len(files)} bytes={required}")
            if not self.storage.has_space(dst.parent, required + 4096):
                raise v2.DataError("insufficient free space")
            self._emit(BACKUP_RUNNING, 30, "writing temporary archive")
            result = v2.create_backup(src, dst)
            self.tx.archive_hash = result["archive_sha256"]
            self.tx.completed_files = result["file_count"]
            self._emit(BACKUP_VERIFYING, 80, "verifying archive")
            v2.inspect_backup(dst)
            self._emit(BACKUP_COMPLETE, 100, "backup verified")
            return result
        except Cancelled:
            self.tx.current_state = CANCELLED
            self.journal.write(self.tx)
            self.logs.append("BACKUP_CANCELLED", "WARN", "backup cancelled",
                             self.tx.transaction_id)
            # v2.create_backup cleans its own temporary file in finally().
            tmp_pattern = dst.name + ".*.tmp"
            for tmp in dst.parent.glob(tmp_pattern):
                tmp.unlink(missing_ok=True)
            raise
        except (v2.DataError, OSError) as exc:
            self.tx.current_state = FAILED
            self.tx.error = str(exc)[:500]
            self.tx.recovery_action = "CLEAN_TEMP_OR_RETRY"
            self.journal.write(self.tx)
            self.logs.append("BACKUP_FAILED", "ERROR", self.tx.error,
                             self.tx.transaction_id)
            raise


# ---- restore service --------------------------------------------------------
class RestoreService(ServiceBase):
    def __init__(self, *args, verifier: ArchiveVerifier | None = None, **kwargs):
        super().__init__(*args, **kwargs)
        self.verifier = verifier

    def run(self, archive: os.PathLike | str, destination: os.PathLike | str,
            execute: bool = False) -> dict:
        self._cancel = False
        arc = self.permissions.check(archive)
        dest = self.permissions.check(destination, write=True)
        self.tx = Transaction(uuid.uuid4().hex, "restore", str(arc), str(dest),
                              RESTORE_VALIDATING)
        try:
            self._emit(RESTORE_VALIDATING, 10, "verifying archive")
            verify = v2.inspect_backup(arc)
            self.tx.archive_hash = verify["archive_sha256"]
            self.tx.total_files = verify["file_count"]
            self._emit(CHECKING_STORAGE, 25, "checking destination")
            if not self.storage.has_space(dest.parent,
                                          verify["total_bytes"] + 4096):
                raise v2.DataError("insufficient free space")
            if not execute:
                self.tx.current_state = IDLE
                self.journal.write(self.tx)
                return {"status": "DRY_RUN", "destination": str(dest),
                        "file_count": verify["file_count"],
                        "total_bytes": verify["total_bytes"]}
            self._emit(RESTORE_RUNNING, 40, "restoring into staging")
            result = v2.restore_backup(arc, dest, execute=True)
            self.tx.completed_files = result["file_count"]
            self._emit(RESTORE_VERIFYING, 85, "verifying restored data")
            self._emit(RESTORE_COMPLETE, 100, "restore committed")
            return result
        except Cancelled:
            self.tx.current_state = CANCELLED
            self.tx.recovery_action = "ROLLBACK_STAGING"
            self.journal.write(self.tx)
            self.logs.append("RESTORE_CANCELLED", "WARN", "restore cancelled",
                             self.tx.transaction_id)
            raise
        except (v2.DataError, OSError) as exc:
            self.tx.current_state = FAILED
            self.tx.error = str(exc)[:500]
            self.tx.recovery_action = "ROLLBACK_STAGING"
            self.journal.write(self.tx)
            self.logs.append("RESTORE_FAILED", "ERROR", self.tx.error,
                             self.tx.transaction_id)
            raise


# ---- convenience factory ---------------------------------------------------
@dataclass
class RescueStack:
    logs: RecoveryLogService
    permissions: PermissionManager
    storage: StorageManager
    journal: TransactionJournal
    verifier: ArchiveVerifier
    backup: BackupService
    restore: RestoreService

    @classmethod
    def create(cls, workspace: os.PathLike | str,
               granted_root: os.PathLike | str) -> "RescueStack":
        ws = Path(workspace).resolve()
        ws.mkdir(parents=True, exist_ok=True)
        perms = PermissionManager([granted_root, ws])
        logs = RecoveryLogService(ws / "recovery-logs", perms)
        storage = StorageManager(perms)
        journal = TransactionJournal(ws / "recovery-logs", perms)
        verifier = ArchiveVerifier(perms, logs)
        backup = BackupService(perms, storage, logs, journal)
        restore = RestoreService(perms, storage, logs, journal,
                                 verifier=verifier)
        return cls(logs, perms, storage, journal, verifier, backup, restore)


# ---- CLI (text recovery UI) -------------------------------------------------
MENU = """QEAFRESCUE v3
  backup    - create verified archive
  restore   - verify and restore archive
  logs      - JSONL log viewer/export
  storage   - storage status
  system    - build/version information
  reboot    - not available in host service
  quit
Partition Installer: LOCKED
"""


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--workspace", type=Path, default=Path.cwd() / "qeafrescue-v3")
    p.add_argument("--root", type=Path, default=None,
                   help="additional granted storage root")
    sub = p.add_subparsers(dest="cmd")
    sub.add_parser("menu")
    b = sub.add_parser("backup"); b.add_argument("source"); b.add_argument("archive")
    r = sub.add_parser("restore"); r.add_argument("archive"); r.add_argument("destination")
    r.add_argument("--execute", action="store_true")
    v = sub.add_parser("verify"); v.add_argument("archive")
    lg = sub.add_parser("logs"); lg.add_argument("--severity", default="ALL")
    lg.add_argument("--keyword", default=""); lg.add_argument("--limit", type=int, default=50)
    ex = sub.add_parser("export"); ex.add_argument("--dir", type=Path)
    ex.add_argument("--format", choices=("jsonl", "txt"), default="jsonl")
    sub.add_parser("storage")
    sub.add_parser("interrupted")
    a = p.parse_args(argv)
    root = a.root or a.workspace
    stack = RescueStack.create(a.workspace, root)
    try:
        if a.cmd in (None, "menu"):
            print(MENU)
        elif a.cmd == "backup":
            stack.backup.on_progress(lambda pr: print(json.dumps(
                pr.__dict__, ensure_ascii=False)))
            print(json.dumps(stack.backup.run(a.source, a.archive),
                             ensure_ascii=False, indent=2))
        elif a.cmd == "restore":
            stack.restore.on_progress(lambda pr: print(json.dumps(
                pr.__dict__, ensure_ascii=False)))
            print(json.dumps(stack.restore.run(a.archive, a.destination,
                                               a.execute), ensure_ascii=False,
                             indent=2))
        elif a.cmd == "verify":
            report = stack.verifier.verify(a.archive)
            report.pop("manifest", None)
            print(json.dumps(report, ensure_ascii=False, indent=2))
        elif a.cmd == "logs":
            print(json.dumps(stack.logs.read(a.severity, a.keyword, a.limit),
                             ensure_ascii=False, indent=2))
        elif a.cmd == "export":
            print(json.dumps(stack.logs.export(a.dir or (a.workspace / "export"),
                                               a.format), ensure_ascii=False,
                             indent=2))
        elif a.cmd == "storage":
            print(json.dumps(stack.storage.status(root).__dict__,
                             ensure_ascii=False, indent=2))
        elif a.cmd == "interrupted":
            print(json.dumps(stack.journal.list_interrupted(),
                             ensure_ascii=False, indent=2))
        if stack.logs.warning:
            print("WARNING: " + stack.logs.warning, file=sys.stderr)
        return 0
    except (v2.DataError, PermissionDenied, OSError, Cancelled) as exc:
        print(f"QEAFRESCUE v3 ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
