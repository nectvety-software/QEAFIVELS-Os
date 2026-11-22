#!/usr/bin/env python3
"""Guards for the RAM-only persist test path (no physical partition writes)."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HS = (ROOT / "platform" / "violet" / "host_services.c").read_text(encoding="utf-8")
HH = (ROOT / "platform" / "violet" / "host_services.h").read_text(encoding="utf-8")
RESCUE = (ROOT / "platform" / "violet" / "channel" / "fs" / "rescue.c").read_text(encoding="utf-8")
DOC = (ROOT / "docs" / "platforms" / "VIOLET_WIFI_BRINGUP.md").read_text(encoding="utf-8")


class RamPersistTests(unittest.TestCase):
    def test_ram_only_mount(self):
        self.assertIn('mount("tmpfs", PERSIST_DIR, "tmpfs"', HS)
        self.assertIn('strcmp(fstype, "tmpfs") == 0', HS)
        self.assertIn("wlan_started = 0", HS)

    def test_test_mac_is_labelled(self):
        self.assertIn("Intf0MacAddress=021A2B3C4D5E", HS)
        self.assertIn("session only, NOT factory", HS)
        self.assertIn("p74 untouched", HS)

    def test_qcom_mac_parser(self):
        self.assertIn("parse_qcom_mac_file", HS)
        self.assertIn("Intf0MacAddress=", HS)
        self.assertIn("unsigned char bytes[512]", HS)

    def test_rescue_verb_and_api(self):
        self.assertIn('eq(subname, "ramtest")', RESCUE)
        self.assertIn("vio_host_persist_ram_setup", HH)

    def test_evidence_recorded(self):
        for token in ("RAM-only test session", "recv_calls=0", "wlfw_cal_00",
                      "NOT factory", "wlan0"):
            self.assertIn(token, DOC)


if __name__ == "__main__":
    unittest.main(verbosity=2)
