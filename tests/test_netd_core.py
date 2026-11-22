#!/usr/bin/env python3
"""Static regression guards for the qeaf-netd core layer."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = (ROOT / "platform" / "violet" / "netd" / "core" / "netd_core.c").read_text(encoding="utf-8")
HDR = (ROOT / "platform" / "violet" / "netd" / "core" / "netd_core.h").read_text(encoding="utf-8")
BUILD = (ROOT / "scripts" / "build_violet.py").read_text(encoding="utf-8")
PKG = (ROOT / "scripts" / "package_violet.py").read_text(encoding="utf-8")


class NetdCoreTests(unittest.TestCase):
    def test_log_format(self):
        self.assertIn('"net: %s %s"', CORE)

    def test_all_states(self):
        for state in ("verified", "failed", "blocked", "unknown"):
            self.assertIn(state, CORE)
            self.assertIn(state.upper().replace("ED", "ED") if False else state, CORE)

    def test_no_unsafe_copy(self):
        self.assertNotIn("strcpy(", CORE)
        self.assertNotIn("sprintf(", CORE)

    def test_epoll_and_monotonic(self):
        self.assertIn("epoll_create1", CORE)
        self.assertIn("timerfd_create", CORE)
        self.assertIn("vio_monotonic_ms", CORE)

    def test_build_integration(self):
        self.assertIn("netd_core.c", BUILD)

    def test_package_gate(self):
        self.assertIn('b"net: "', PKG)
        self.assertIn('b"blocked"', PKG)

    def test_api_header(self):
        for symbol in ("netd_log_line", "netd_loop_run", "netd_now_ms"):
            self.assertIn(symbol, HDR)


if __name__ == "__main__":
    unittest.main(verbosity=2)
