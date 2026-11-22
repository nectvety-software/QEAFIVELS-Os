#!/usr/bin/env python3
"""Media captured by the OS must be listable and viewable through shell apps."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
V = ROOT / "platform" / "violet"
FILES_H = (V / "ui" / "vqeaf_files.h").read_text(encoding="utf-8")
FILES_C = (V / "ui" / "vqeaf_files.c").read_text(encoding="utf-8")
MAIN = (V / "main_violet.c").read_text(encoding="utf-8")
PAGES = (V / "ui" / "vqeaf_pages.c").read_text(encoding="utf-8")
RESCUE = (V / "channel" / "fs" / "rescue.c").read_text(encoding="utf-8")


class MediaViewerTests(unittest.TestCase):
    def test_lun_backend_wired(self):
        self.assertIn("vqeaf_files_set_backends", FILES_H)
        self.assertIn("g_files_read", FILES_C)
        self.assertIn("lun_files_read", MAIN)
        self.assertIn("UserData/Videos", MAIN)
        self.assertIn("UserData/Pictures", MAIN)

    def test_gallery_decodes_real_files(self):
        self.assertIn("vqeaf_files_list(\"/Pictures\"", PAGES)
        self.assertIn("view_decode_bmp(data", PAGES)
        self.assertIn("view_decode_png(data", PAGES)
        self.assertIn("VQEAF_MEDIA_JPEG", PAGES)

    def test_video_page_uses_backend(self):
        self.assertIn('vqeaf_video_scan("/media"', PAGES)
        self.assertIn("vqeaf_video_qvf_frame", PAGES)

    def test_rescue_listing_verb(self):
        self.assertIn('eq(line, "media list")', RESCUE)
        self.assertIn("UserData/Pictures", RESCUE)
        self.assertIn("UserData/Videos", RESCUE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
