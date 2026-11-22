#!/usr/bin/env python3
"""Three-finger swipe must be detected and mapped to a real screenshot."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
V = ROOT / "platform" / "violet"
IN = (V / "violet_input.c").read_text(encoding="utf-8")
IN_H = (V / "violet_input.h").read_text(encoding="utf-8")
MAIN = (V / "main_violet.c").read_text(encoding="utf-8")
RESCUE = (V / "channel" / "fs" / "rescue.c").read_text(encoding="utf-8")


class ThreeFingerTests(unittest.TestCase):
    def test_multislot_tracking(self):
        self.assertIn("mt_id[VIO_TOUCH_MAX_SLOTS]", IN_H)
        self.assertIn("ABS_MT_TRACKING_ID", IN)
        self.assertIn("active >= 3", IN)
        self.assertIn("three_finger_report", IN)

    def test_gesture_fires_on_move(self):
        self.assertIn("dx > 60 || dy > 60", IN)
        self.assertIn("vio_touch_take_three_finger", IN_H)

    def test_screenshot_action(self):
        self.assertIn("vio_touch_take_three_finger(&touch)", MAIN)
        self.assertIn("screenshot_png(&lun.volume, sname", MAIN)

    def test_host_test_verb(self):
        self.assertIn('starts(line, "swipe3 ")', RESCUE)
        self.assertIn("vio_touch_inject_three", MAIN)


if __name__ == "__main__":
    unittest.main(verbosity=2)
