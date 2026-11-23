#!/usr/bin/env python3
"""USB device identity must come from the OS, not a vendor handset name."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
USB = (ROOT / "platform" / "violet" / "usb_lun.c").read_text(encoding="utf-8")
FAT = (ROOT / "platform" / "violet" / "channel" / "fs" / "fat16.h").read_text(encoding="utf-8")


class UsbIdentityTests(unittest.TestCase):
    def test_device_name_is_os(self):
        self.assertIn('write_attr(strings, "product", QEAF_OS_NAME " violet")', USB)
        self.assertIn('write_attr(strings, "manufacturer", QEAF_OS_NAME)', USB)

    def test_rescue_volume_label_unchanged(self):
        self.assertIn('#define FAT16_LABEL "QEAFRESCUE"', FAT)


if __name__ == "__main__":
    unittest.main(verbosity=2)
