#!/usr/bin/env python3
"""Host-side HAL tests: real FDT parser + firmware lifecycle on FIXTURE data."""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts" / "wifi" / "run_hal_tests.py"
FIXTURE = ROOT / "tests" / "wifi" / "fixtures" / "FIXTURE_violet_wifi.dtb"


class WifiHalTests(unittest.TestCase):
    def test_fixture_is_labelled_and_real_format(self):
        self.assertIn("FIXTURE", FIXTURE.name)
        data = FIXTURE.read_bytes() if FIXTURE.exists() else b""
        self.assertGreater(len(data), 40)
        self.assertEqual(data[:4], bytes.fromhex("d00dfeed"))

    def test_c_hal_runner(self):
        p = subprocess.run([sys.executable, str(RUNNER)],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("test_wifi_hal: ALL OK", p.stdout)

    def test_no_hardcoded_bus_addresses(self):
        src = (ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" /
               "wifi_device.c").read_text(encoding="utf-8")
        self.assertIn("#address-cells", src)
        self.assertIn("#size-cells", src)
        self.assertIn("#interrupt-cells", src)
        self.assertNotIn("0x18800000", src)
        self.assertNotIn("0x93900000", src)


if __name__ == "__main__":
    unittest.main(verbosity=2)
