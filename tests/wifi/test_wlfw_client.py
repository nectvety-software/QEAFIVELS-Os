#!/usr/bin/env python3
"""Wrapper and source guards for the native WCN3990 WLFW client tests."""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts" / "wifi" / "run_hal_tests.py"
SOURCE = ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" / "wlfw_client.c"


class WlfwClientTests(unittest.TestCase):
    def test_protocol_runner(self):
        p = subprocess.run([sys.executable, str(RUNNER)],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("test_wlfw_client: ALL OK", p.stdout)

    def test_client_requires_received_evidence(self):
        src = SOURCE.read_text(encoding="utf-8")
        for token in ("vqeaf_qmi_request", "vqeaf_qmi_wait_indication",
                      "VQEAF_WLFW_FW_READY_IND", "VQEAF_WLFW_FAILED",
                      "decode_result", "VQEAF_WLFW_SEG_MAX"):
            self.assertIn(token, src)
        self.assertNotIn("send_result", src)


if __name__ == "__main__":
    unittest.main(verbosity=2)
