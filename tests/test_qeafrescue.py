#!/usr/bin/env python3
"""Host-side safety tests for QEAFRESCUE v1 transport and boot verifier."""
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import qeafrescue as q


def make_image(path: Path, version=1, page=4096):
    kernel = b"K" * 8192
    ramdisk = b"R" * 4096
    header = bytearray(page)
    header[:8] = b"ANDROID!"
    struct.pack_into("<10I", header, 8, len(kernel), 0x8000,
                     len(ramdisk), 0x01000000, 0, 0, 0x100,
                     page, version, 0)
    path.write_bytes(bytes(header) + kernel + ramdisk)


class RescueTests(unittest.TestCase):
    def test_parse_fastboot_devices(self):
        self.assertEqual(q.parse_fastboot_devices("ABC123 fastboot\n"), ["ABC123"])

    def test_parse_adb_states(self):
        self.assertEqual(q.parse_adb_devices("List of devices attached\nX device product:violet\nY unauthorized\n"),
                         [{"serial": "X", "state": "device"}, {"serial": "Y", "state": "unauthorized"}])

    def test_valid_boot_image(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "boot.img"
            make_image(p)
            report = q.inspect_boot_image(p)
            self.assertEqual(report["header_version"], 1)
            self.assertEqual(report["kernel_size"], 8192)
            self.assertEqual(report["ramdisk_size"], 4096)

    def test_reject_wrong_magic(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "bad.img"
            p.write_bytes(b"NOTBOOT!" + b"\0" * 8192)
            with self.assertRaises(q.RescueError):
                q.inspect_boot_image(p)

    def test_reject_truncated_payload(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "short.img"
            make_image(p)
            p.write_bytes(p.read_bytes()[:5000])
            with self.assertRaises(q.RescueError):
                q.inspect_boot_image(p)

    def test_reject_bad_page_size(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "badpage.img"
            make_image(p, page=3000)
            with self.assertRaises(q.RescueError):
                q.inspect_boot_image(p)

    def test_reject_unsupported_header(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "future.img"
            make_image(p, version=3)
            with self.assertRaises(q.RescueError):
                q.inspect_boot_image(p)

    def test_ram_boot_is_not_called_without_execute(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "boot.img"
            make_image(p)
            args = type("Args", (), {"image": str(p), "expect_sha256": None,
                                     "serial": None, "execute": False, "timeout": 10})()
            with patch.object(q, "select_fastboot", return_value=("fastboot", "S")):
                with patch.object(q, "fastboot_getvar", side_effect=["violet", "yes"]):
                    with patch.object(q, "run") as run_mock:
                        self.assertEqual(q.cmd_ram_boot(args), 0)
                        run_mock.assert_not_called()

    def test_ram_boot_refuses_non_violet(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "boot.img"
            make_image(p)
            args = type("Args", (), {"image": str(p), "expect_sha256": None,
                                     "serial": None, "execute": True, "timeout": 10})()
            with patch.object(q, "select_fastboot", return_value=("fastboot", "S")):
                with patch.object(q, "fastboot_getvar", return_value="chopin"):
                    with self.assertRaises(q.RescueError):
                        q.cmd_ram_boot(args)

    def test_locked_bootloader_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "boot.img"
            make_image(p)
            args = type("Args", (), {"image": str(p), "expect_sha256": None,
                                     "serial": None, "execute": True, "timeout": 10})()
            with patch.object(q, "select_fastboot", return_value=("fastboot", "S")):
                with patch.object(q, "fastboot_getvar", side_effect=["violet", "no"]):
                    with self.assertRaises(q.RescueError):
                        q.cmd_ram_boot(args)

    def test_unknown_unlock_state_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "boot.img"
            make_image(p)
            args = type("Args", (), {"image": str(p), "expect_sha256": None,
                                     "serial": None, "execute": True, "timeout": 10})()
            with patch.object(q, "select_fastboot", return_value=("fastboot", "S")):
                with patch.object(q, "fastboot_getvar", side_effect=["violet", "unknown"]):
                    with self.assertRaises(q.RescueError):
                        q.cmd_ram_boot(args)

    def test_execute_issues_boot_not_flash(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "boot.img"
            make_image(p)
            args = type("Args", (), {"image": str(p), "expect_sha256": None,
                                     "serial": None, "execute": True, "timeout": 10})()
            with patch.object(q, "select_fastboot", return_value=("fastboot", "VIOLET123")):
                with patch.object(q, "fastboot_getvar", side_effect=["violet", "yes"]):
                    with patch.object(q, "run", return_value=SimpleNamespace(
                            stdout="OKAY", stderr="", returncode=0)) as run_mock:
                        self.assertEqual(q.cmd_ram_boot(args), 0)
                        command = run_mock.call_args.args[0]
                        self.assertEqual(command[3], "boot")
                        self.assertNotIn("flash", command)
                        self.assertNotIn("erase", command)


if __name__ == "__main__":
    unittest.main(verbosity=2)
