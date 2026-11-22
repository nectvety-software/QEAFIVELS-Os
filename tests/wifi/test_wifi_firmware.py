#!/usr/bin/env python3
"""Wrapper for the WCN3990 firmware container validation host test."""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts" / "wifi" / "run_hal_tests.py"
SRC = (ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" /
       "wifi_firmware.c").read_text(encoding="utf-8")
PLATFORM = (ROOT / "platform" / "violet" /
            "wifi_fw_check.c").read_text(encoding="utf-8")


class WifiFirmwareTests(unittest.TestCase):
    def test_runner_passes(self):
        p = subprocess.run([sys.executable, str(RUNNER)],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("test_wifi_firmware: ALL OK", p.stdout)
        self.assertIn("test_wifi_hal: ALL OK", p.stdout)

    def test_rejections_are_real(self):
        for token in ("-EINVAL", "-ENOTSUP", "-ENODATA",
                      "VQEAF_WIFI_FW_MACHINE_HEXAGON", "payload_bytes",
                      "PT_LOAD"):
            self.assertIn(token, SRC)
        self.assertIn("-ENOENT", PLATFORM)   # absent file is mapped, not ignored

    def test_set_is_never_partial(self):
        self.assertIn("set->ok = set->dsp_rc == 0 && set->board_rc == 0 && "
                      "set->regdb_rc == 0", SRC)
        self.assertIn("icnss must not be handed this set", PLATFORM)
        # The report is derived from real files, not a stored string.
        self.assertIn('open(path, O_RDONLY', PLATFORM)
        self.assertIn('stat(path, &st)', PLATFORM)


if __name__ == "__main__":
    unittest.main(verbosity=2)