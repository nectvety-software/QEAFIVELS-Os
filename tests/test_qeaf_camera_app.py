#!/usr/bin/env python3
"""Camera app must surface the real native V4L2 census, not a mock preview."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
V = ROOT / "platform" / "violet"
UI_H = (V / "ui" / "vqeaf_ui.h").read_text(encoding="utf-8")
PAGES = (V / "ui" / "vqeaf_pages.c").read_text(encoding="utf-8")
MAIN = (V / "main_violet.c").read_text(encoding="utf-8")


class CameraAppTests(unittest.TestCase):
    def test_page_registered(self):
        self.assertIn("VQEAF_PAGE_CAMERA", UI_H)
        self.assertIn("case VQEAF_PAGE_CAMERA:   build_camera", PAGES)
        self.assertIn('strcmp(name, "camera")', MAIN)

    def test_real_driver_calls(self):
        self.assertIn("vqeaf_camera_probe(buf", PAGES)
        self.assertIn("vqeaf_camera_create_session", PAGES)
        self.assertIn("vqeaf_camera_query_caps", PAGES)

    def test_honest_no_stream(self):
        self.assertIn("capture=0", PAGES)
        self.assertIn("Không có preview hay ảnh chụp giả", PAGES)

    def test_app_tile_no_longer_stub(self):
        self.assertNotIn('"Máy ảnh",    0x2C1B3E, 0xC79BFF, VQEAF_PAGE_COUNT',
                         PAGES)


if __name__ == "__main__":
    unittest.main(verbosity=2)
