#!/usr/bin/env python3
"""Wrapper for the WCN3990 firmware boot state machine host test."""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts" / "wifi" / "run_hal_tests.py"


class FirmwareBootTests(unittest.TestCase):
    def test_state_machine_runner(self):
        p = subprocess.run([sys.executable, str(RUNNER)],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("test_firmware_boot: ALL OK", p.stdout)

    def test_no_fake_success(self):
        src = (ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" /
               "firmware_boot.c").read_text(encoding="utf-8")
        for token in ("wait_boot", "fw_boot_status", "-ETIMEDOUT",
                      "VQEAF_FW_WLAN_READY", "max_restarts"):
            self.assertIn(token, src)
        # No fixed sleep used as proof: sleep only between real status polls.
        self.assertIn("if(booted) return set", src)


if __name__ == "__main__":
    unittest.main(verbosity=2)
