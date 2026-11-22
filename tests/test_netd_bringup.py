#!/usr/bin/env python3
"""Regression guards for the qeaf-netd bring-up ladder."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "platform" / "violet" / "netd" / "bringup" / "netd_bringup.c").read_text(encoding="utf-8")
FIXTURE = ROOT / "tests" / "fixtures" / "FIXTURE_bringup_blocked.txt"


class NetdBringupTests(unittest.TestCase):
    def test_five_steps_present(self):
        for step in range(1, 6):
            self.assertIn("step%d" % step, SRC)

    def test_stops_on_first_failure(self):
        self.assertIn("return blocked(", SRC)
        self.assertNotIn("continue; /* keep going", SRC)

    def test_no_fake_service(self):
        self.assertNotIn("NEW_SERVER", SRC)
        self.assertIn("recvfrom", SRC)
        self.assertIn("WLFW_0x45_NOT_REGISTERED", SRC)

    def test_fixture_is_labelled(self):
        self.assertIn("FIXTURE", FIXTURE.name)
        text = FIXTURE.read_text(encoding="utf-8")
        self.assertIn("net: blocked", text)
        self.assertIn("WLFW_0x45_NOT_REGISTERED", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
