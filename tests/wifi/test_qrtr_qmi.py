#!/usr/bin/env python3
"""Wrapper for the native QRTR/QMI C protocol tests (FIXTURE-based)."""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts" / "wifi" / "run_hal_tests.py"
FIXTURES = ROOT / "tests" / "wifi" / "fixtures"


class QrtrQmiTests(unittest.TestCase):
    def test_fixtures_are_labelled(self):
        names = [p.name for p in FIXTURES.glob("*qrtr*")] + \
                [p.name for p in FIXTURES.glob("*qmi*")]
        self.assertTrue(names)
        for n in names:
            self.assertTrue(n.startswith("FIXTURE_"), n)

    def test_protocol_runner(self):
        p = subprocess.run([sys.executable, str(RUNNER)],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("test_wifi_hal: ALL OK", p.stdout)
        self.assertIn("test_qrtr_qmi: ALL OK", p.stdout)
        self.assertIn("test_wlfw_client: ALL OK", p.stdout)

    def test_discovery_only_ingests_received_packets(self):
        src = (ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" /
               "qrtr_transport.c").read_text(encoding="utf-8")
        self.assertIn("VQEAF_QRTR_NEW_LOOKUP", src)
        # table_add is called from ingest(), which is fed only by io->recv.
        self.assertIn("static void ingest", src)
        self.assertIn("io->recv(io->ctx", src)
        self.assertIn("table_add(&pkt)", src)

    def test_client_has_timeout_and_retry_paths(self):
        qmi = (ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" /
               "qmi_service.c").read_text(encoding="utf-8")
        for token in ("VQEAF_QMI_RESP", "retries_left", "-ETIMEDOUT",
                      "bad_responses", "VQEAF_QMI_LINK_RESET"):
            self.assertIn(token, qmi)


if __name__ == "__main__":
    unittest.main(verbosity=2)
