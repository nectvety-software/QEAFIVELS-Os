#!/usr/bin/env python3
"""Screenshot/recorder must be reachable from Control Center and hardware keys."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
V = ROOT / "platform" / "violet"
PAGES = (V / "ui" / "vqeaf_pages.c").read_text(encoding="utf-8")
POWER = (V / "violet_power.c").read_text(encoding="utf-8")
POWER_H = (V / "violet_power.h").read_text(encoding="utf-8")
MAIN = (V / "main_violet.c").read_text(encoding="utf-8")


class CaptureShortcutTests(unittest.TestCase):
    def test_control_center_tiles(self):
        self.assertIn('"Chụp màn hình"', PAGES)
        self.assertIn('"Quay màn hình"', PAGES)
        self.assertIn("case CTL_SHOT:", PAGES)
        self.assertIn("case CTL_RECORD:", PAGES)

    def test_tiles_call_real_capture(self):
        self.assertIn("vio_screenshot_now()", PAGES)
        self.assertIn("vio_rec_start(8, 10)", PAGES)
        self.assertIn("vio_rec_stop()", PAGES)

    def test_short_and_long_combo(self):
        self.assertIn("pw->screenshot = 1", POWER)
        self.assertIn("combo_since_ms", POWER_H)
        self.assertIn(">= 1000u", POWER)
        self.assertIn("pw->record_toggle = 1", POWER)
        self.assertIn("pw->screenshot = 0", POWER)  # long press is not a still

    def test_main_handles_record_toggle(self):
        self.assertIn("power_btn.record_toggle", MAIN)
        self.assertIn("vio_rec_active()", MAIN)
        self.assertIn("Đang quay 10 giây, 8 fps", MAIN)


if __name__ == "__main__":
    unittest.main(verbosity=2)
