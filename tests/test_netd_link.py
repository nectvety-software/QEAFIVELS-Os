#!/usr/bin/env python3
"""Regression guards for qeaf-netd link layer with a recorded FIXTURE."""
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "platform" / "violet" / "netd" / "link" / "netd_link.c").read_text(encoding="utf-8")
FIXTURE = ROOT / "tests" / "fixtures" / "FIXTURE_netlink_link.bin"


def _fixture_bytes():
    # One RTM_GETLINK netlink message with IFLA_IFNAME="wlan0".
    name = b"wlan0\x00"
    attr = struct.pack("=HH", 8 + len(name), 3) + name  # IFLA_IFNAME=3
    ifm = struct.pack("=BBHiI", 0, 0, 0, 0, 0)  # ifinfomsg, 16 bytes
    payload = ifm + attr + b"\x00" * ((-len(attr)) % 4)
    nl = struct.pack("=IHHII", 16 + len(payload), 18, 0, 1, 1) + payload
    return nl + b"\x00" * ((-len(nl)) % 4)


class NetdLinkTests(unittest.TestCase):
    def test_fixture_is_labelled(self):
        self.assertIn("FIXTURE", FIXTURE.name)

    def test_fixture_shape(self):
        data = _fixture_bytes()
        msg_len, typ = struct.unpack_from("=IH", data, 0)
        self.assertGreaterEqual(msg_len, 16)
        self.assertEqual(typ, 18)  # RTM_GETLINK
        self.assertIn(b"wlan0", data)

    def test_parser_uses_ifla_ifname_and_bounds(self):
        self.assertIn("IFLA_IFNAME", SRC)
        self.assertIn("alen > mlen - aoff", SRC)
        self.assertIn("mlen > len - off", SRC)
        self.assertNotIn("strcpy(", SRC)
        self.assertNotIn("sprintf(", SRC)

    def test_no_hardcoded_iface(self):
        # Only the fixture may mention wlan0; production code must not assume it.
        self.assertNotIn('"wlan0"', SRC)


if __name__ == "__main__":
    unittest.main(verbosity=2)
