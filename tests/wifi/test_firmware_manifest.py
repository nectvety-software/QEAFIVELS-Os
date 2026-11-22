#!/usr/bin/env python3
"""Unit tests for the firmware inspector using only labelled fixtures."""
import importlib.util
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "wifi"))
import inspect_violet_firmware as IV  # noqa: E402


class FirmwareManifestTests(unittest.TestCase):
    def test_classifier(self):
        self.assertEqual(IV.classify("image/WLANMDSP.MBN"), "wcn3990_wlan")
        self.assertEqual(IV.classify("image/BDWLAN.B04"), "wcn3990_wlan")
        self.assertEqual(IV.classify("firmware-update/BTFM.bin"), "bluetooth")
        self.assertEqual(IV.classify("firmware-update/NON-HLOS.bin"), "modem")

    def test_magic(self):
        self.assertEqual(IV.magic(b"PK\x03\x04rest"), "zip")
        self.assertEqual(IV.magic(b"\x89PNG\r\n\x1a\n"), "png")

    def test_fixture_manifest(self):
        with tempfile.TemporaryDirectory(prefix="FIXTURE-fw-") as td:
            pkg = Path(td) / "FIXTURE_fw.zip"
            with zipfile.ZipFile(pkg, "w") as z:
                z.writestr("firmware-update/NON-HLOS.bin", b"modem-image")
                z.writestr("firmware-update/BTFM.bin", b"bt-image")
            out = Path(td) / "manifest"
            m = IV.inspect(pkg, out)
            self.assertEqual(m["member_count"], 2)
            self.assertEqual(len(m["sha256" if False else "files"]), 2)
            self.assertFalse(m["required_present"]["wlanmdsp.mbn"])
            self.assertTrue((out / "manifest.json").is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
