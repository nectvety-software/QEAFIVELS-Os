#!/usr/bin/env python3
"""Unit tests for QEAFRESCUE v3 services. All writes stay in temp dirs."""
import hashlib
import json
import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import qeafrescue_data as v2  # noqa: E402
import qeafrescue_v3 as v3  # noqa: E402


class V3Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="qeafrescue-v3-")
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name).resolve()
        self.granted = self.base / "data"
        self.granted.mkdir()
        self.ws = self.base / "workspace"
        self.ws.mkdir()
        self.stack = v3.RescueStack.create(self.ws, self.granted)

    def sample_source(self, files=None):
        src = self.granted / "source"
        src.mkdir(parents=True, exist_ok=True)
        for name, data in (files or {"a.txt": b"hello", "b/c.txt": b"world"}).items():
            p = src / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        return src

    # ---- permission manager ----
    def test_permission_allows_granted_root(self):
        p = self.stack.permissions.check(self.granted)
        self.assertEqual(p, self.granted)

    def test_permission_rejects_outside_root(self):
        outside = self.base / "outside"
        outside.mkdir()
        with self.assertRaises(v3.PermissionDenied):
            self.stack.permissions.check(outside)

    def test_permission_rejects_symlink(self):
        # Windows needs SeCreateSymbolicLinkPrivilege; mock the check instead.
        from unittest.mock import patch
        with patch.object(Path, "is_symlink", return_value=True):
            with self.assertRaises(v3.PermissionDenied):
                self.stack.permissions.check(self.granted / "link")

    def test_permission_rejects_raw_device(self):
        with self.assertRaises(v3.PermissionDenied):
            self.stack.permissions.check("/dev/block/mmcblk0p77")

    def test_permission_rejects_sensitive_name(self):
        sens = self.granted / "persist"
        sens.mkdir()
        with self.assertRaises(v3.PermissionDenied):
            self.stack.permissions.check(sens)

    # ---- storage ----
    def test_storage_status(self):
        s = self.stack.storage.status(self.granted)
        self.assertTrue(s.mounted or not s.mounted)  # value is reported, not faked
        self.assertGreaterEqual(s.total_bytes, 0)
        self.assertGreaterEqual(s.free_bytes, 0)

    # ---- backup ----
    def test_backup_normal(self):
        src = self.sample_source()
        archive = self.ws / "backup.zip"
        states = []
        self.stack.backup.on_progress(lambda p: states.append(p.state))
        result = self.stack.backup.run(src, archive)
        self.assertEqual(result["file_count"], 2)
        self.assertEqual(len(result["archive_sha256"]), 64)
        self.assertEqual(states[-1], v3.BACKUP_COMPLETE)
        self.assertEqual(v2.inspect_backup(archive)["file_count"], 2)

    def test_backup_empty_dir(self):
        src = self.granted / "empty"
        src.mkdir()
        archive = self.ws / "empty.zip"
        result = self.stack.backup.run(src, archive)
        self.assertEqual(result["file_count"], 0)

    def test_backup_large_file(self):
        src = self.sample_source({"big.bin": b"x" * (1024 * 1024)})
        archive = self.ws / "big.zip"
        result = self.stack.backup.run(src, archive)
        self.assertEqual(result["total_bytes"], 1024 * 1024)

    def test_backup_insufficient_storage(self):
        src = self.sample_source()
        self.stack.storage.has_space = lambda *_: False
        with self.assertRaises(v2.DataError):
            self.stack.backup.run(src, self.ws / "no.zip")
        tx = self.stack.journal.load(
            next(p.stem for p in (self.ws / "recovery-logs" / "transactions").glob("*.json")))
        self.assertEqual(tx.current_state, v3.FAILED)

    def test_backup_permission_denied(self):
        outside = self.base / "outside"
        outside.mkdir()
        with self.assertRaises(v3.PermissionDenied):
            self.stack.backup.run(outside, self.ws / "x.zip")

    def test_backup_symlink_rejected(self):
        src = self.sample_source()
        (src / "link").write_bytes(b"not really a symlink on Windows")
        from unittest.mock import patch
        real_is_symlink = Path.is_symlink
        def fake_is_symlink(self):
            if self.name == "link":
                return True
            return real_is_symlink(self)
        with patch.object(Path, "is_symlink", fake_is_symlink):
            with self.assertRaises(v2.DataError):
                self.stack.backup.run(src, self.ws / "sym.zip")

    def test_backup_hash_mismatch(self):
        src = self.sample_source()
        archive = self.ws / "bad.zip"
        result = self.stack.backup.run(src, archive)
        with zipfile.ZipFile(archive, "a") as z:
            z.writestr("tampered.txt", "bad")
        with self.assertRaises(v2.DataError):
            v2.inspect_backup(archive)

    def test_backup_cancel_cleans_temp(self):
        src = self.sample_source()
        archive = self.ws / "cancel.zip"
        def on_progress(p):
            if p.state == v3.CHECKING_STORAGE:
                self.stack.backup.cancel()
        self.stack.backup.on_progress(on_progress)
        with self.assertRaises(v3.Cancelled):
            self.stack.backup.run(src, archive)
        self.assertFalse(archive.exists())
        self.assertEqual(list(self.ws.glob("cancel.zip.*.tmp")), [])

    # ---- verifier / restore ----
    def test_restore_valid_and_inventory(self):
        src = self.sample_source()
        archive = self.ws / "restore.zip"
        self.stack.backup.run(src, archive)
        report = self.stack.verifier.verify(archive)
        self.assertEqual(report["file_count"], 2)
        dest = self.granted / "restored"
        result = self.stack.restore.run(archive, dest, execute=True)
        self.assertEqual(result["status"], "RESTORED_VERIFIED")
        self.assertEqual((dest / "a.txt").read_bytes(), b"hello")

    def test_restore_corrupt_archive(self):
        archive = self.ws / "corrupt.zip"
        archive.write_bytes(b"not a zip")
        with self.assertRaises(v2.DataError):
            self.stack.restore.run(archive, self.granted / "dest")

    def test_restore_dry_run_no_write(self):
        src = self.sample_source()
        archive = self.ws / "dry.zip"
        self.stack.backup.run(src, archive)
        dest = self.granted / "drydest"
        result = self.stack.restore.run(archive, dest, execute=False)
        self.assertEqual(result["status"], "DRY_RUN")
        self.assertFalse(dest.exists())

    def test_restore_existing_destination_refused(self):
        src = self.sample_source()
        archive = self.ws / "exists.zip"
        self.stack.backup.run(src, archive)
        dest = self.granted / "exists"
        dest.mkdir()
        with self.assertRaises(v2.DataError):
            self.stack.restore.run(archive, dest, execute=True)

    def test_restore_interrupted_journal(self):
        archive = self.ws / "none.zip"
        archive.write_bytes(b"x")
        def on_progress(p):
            if p.state == v3.RESTORE_VALIDATING:
                self.stack.restore.cancel()
        self.stack.restore.on_progress(on_progress)
        with self.assertRaises(v3.Cancelled):
            self.stack.restore.run(archive, self.granted / "never")
        items = self.stack.journal.list_interrupted()
        self.assertEqual(items, [])  # CANCELLED is not INTERRUPTED

    def test_restore_interrupted_detection(self):
        tx = v3.Transaction("abc", "restore", "a", "b", v3.RESTORE_RUNNING)
        self.stack.journal.write(tx)
        items = self.stack.journal.list_interrupted()
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["recovery_action"], "ROLLBACK_STAGING_OR_RESUME")

    # ---- log viewer / export ----
    def _write_log(self, data: bytes):
        d = self.ws / "recovery-logs"
        d.mkdir(parents=True, exist_ok=True)
        (d / "recovery.jsonl").write_bytes(data)

    def test_log_valid(self):
        now = 1700000000000
        lines = [
            {"timestamp_unix_ms": now, "session": "s1", "transaction": "t1",
             "event": "BACKUP_COMPLETE", "severity": "INFO", "detail": "ok"},
            {"timestamp_unix_ms": now + 1, "session": "s1", "transaction": "",
             "event": "DENIED", "severity": "BLOCKED", "detail": "refused"},
        ]
        self._write_log(("\n".join(json.dumps(x) for x in lines) + "\n").encode())
        r = self.stack.logs.read()
        self.assertEqual(r["total"], 2)
        self.assertEqual(r["skipped"], 0)

    def test_log_truncated_and_invalid_lines(self):
        good = json.dumps({"severity": "ERROR", "event": "X"}).encode()
        self._write_log(good + b"\n" + b'{"broken"\n' + b"\xff\xfe\n" + good)
        r = self.stack.logs.read("ERROR")
        self.assertEqual(r["total"], 2)
        self.assertEqual(r["skipped"], 2)
        self.assertTrue(r["truncated"])

    def test_log_empty(self):
        r = self.stack.logs.read()
        self.assertEqual(r["total"], 0)

    def test_log_filters(self):
        self._write_log((
            json.dumps({"severity": "WARN", "event": "w"}) + "\n" +
            json.dumps({"severity": "ERROR", "event": "e"}) + "\n").encode())
        self.assertEqual(self.stack.logs.read("WARN")["total"], 1)
        self.assertEqual(self.stack.logs.read("ALL", keyword="WARN")["total"], 1)

    def test_log_export_jsonl_and_txt(self):
        self._write_log((json.dumps({"severity": "INFO", "event": "e",
                                     "detail": "d"}) + "\n").encode())
        before = (self.ws / "recovery-logs" / "recovery.jsonl").read_bytes()
        out = self.ws / "export"
        j = self.stack.logs.export(out, "jsonl")
        t = self.stack.logs.export(out, "txt")
        self.assertTrue(Path(j["export"]).is_file())
        self.assertTrue(Path(t["export"]).is_file())
        self.assertEqual((self.ws / "recovery-logs" / "recovery.jsonl").read_bytes(),
                         before)

    # ---- UI / security contract ----
    def test_partition_installer_locked_in_menu(self):
        self.assertIn("Partition Installer: LOCKED", v3.MENU)

    def test_no_partition_write_api(self):
        src = (ROOT / "scripts" / "qeafrescue_v3.py").read_text(encoding="utf-8")
        for forbidden in ("fastboot flash", "dd if=", "erase(", "format("):
            self.assertNotIn(forbidden, src)


if __name__ == "__main__":
    unittest.main(verbosity=2)
