#!/usr/bin/env python3
"""QEAFRESCUE FAT16 has no fixed per-file cap and reclaims deleted clusters."""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FAT = (ROOT / "platform" / "violet" / "channel" / "fs" / "fat16.c").read_text(encoding="utf-8")
HDR = (ROOT / "platform" / "violet" / "channel" / "fs" / "fat16.h").read_text(encoding="utf-8")
RESCUE = (ROOT / "platform" / "violet" / "channel" / "fs" / "rescue.c").read_text(encoding="utf-8")


class RescueFatTests(unittest.TestCase):
    def test_no_64_cluster_cap(self):
        self.assertNotIn("need > 64", FAT)
        self.assertNotIn("n < 64", FAT)
        self.assertIn("fat_alloc", FAT)

    def test_delete_reclaims_clusters(self):
        self.assertIn("int fat16_delete_file", FAT)
        self.assertIn("fat16_delete_file", HDR)
        self.assertIn("fat_set(vol, cur, 0)", FAT)

    def test_rescue_verbs(self):
        self.assertIn('eq(line, "df")', RESCUE)
        self.assertIn('starts(line, "del ")', RESCUE)

    def test_worker_fault_hands_over(self):
        main = (ROOT / "platform" / "violet" / "main_violet.c").read_text(encoding="utf-8")
        self.assertIn("if(!is_main) rescue_thread_up = 0", main)
        self.assertIn("rescue_thread_up", main)

    def test_host_fat_suite(self):
        p = subprocess.run([sys.executable, str(ROOT / "scripts" / "test_fat16_path.py")],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("large file reads back byte for byte", p.stdout)
        self.assertIn("delete reclaims all clusters", p.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
