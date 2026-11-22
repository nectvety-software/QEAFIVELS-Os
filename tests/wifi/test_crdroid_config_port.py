#!/usr/bin/env python3
"""Tests for the crDroid Wi-Fi config port: provenance + no fake MAC."""
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "wifi"))
import port_crdroid_config as PC  # noqa: E402

CFG = ROOT / "firmware" / "violet" / "config"
PKG = (ROOT / "scripts" / "package_violet.py").read_text(encoding="utf-8")


class CrdroidConfigPortTests(unittest.TestCase):
    def test_manifest_provenance(self):
        import json
        m = json.loads((CFG / "manifest.json").read_text(encoding="utf-8"))
        self.assertIn("crdroidandroid/android_device_xiaomi_violet", m["upstream"])
        self.assertTrue(m["upstream_commit"])
        self.assertEqual(len(m["files"]), 3)
        for f in m["files"]:
            self.assertEqual(len(f["source_sha256"]), 64)
            self.assertEqual(len(f["output_sha256"]), 64)

    def test_no_placeholder_mac_in_output(self):
        ini = (CFG / "WCNSS_qcom_cfg.ini").read_text(encoding="utf-8")
        self.assertNotIn("000AF58989FF", ini)
        self.assertNotIn("000AF58989FE", ini)
        self.assertIn("persist/wlan_mac.bin", ini)
        for key in ("gDot11Mode", "WmmIsEnabled", "gWlanMccToSccSwitchMode"):
            self.assertIn(key, ini)

    def test_sanitizer_on_labelled_fixture(self):
        with tempfile.TemporaryDirectory(prefix="FIXTURE-crdroid-") as td:
            src = Path(td) / "src"
            (src / "wifi").mkdir(parents=True)
            (src / "wifi" / "WCNSS_qcom_cfg.ini").write_text(
                "gDot11Mode=0\nIntf0MacAddress=000AF58989FF\n"
                "Intf1MacAddress=000AF58989FE\n", encoding="utf-8")
            (src / "wifi" / "wpa_supplicant_overlay.conf").write_text("x=1\n")
            (src / "wifi" / "p2p_supplicant_overlay.conf").write_text("y=1\n")
            out = Path(td) / "out"
            PC.port(src, out)
            text = (out / "WCNSS_qcom_cfg.ini").read_text(encoding="utf-8")
            self.assertNotIn("000AF58989FF", text)
            self.assertIn("gDot11Mode=0", text)
            self.assertIn("# Intf0MacAddress=<removed>", text)

    def test_packaging_uses_tracked_config(self):
        self.assertIn('"firmware", "violet", "config"', PKG)
        self.assertIn('"WCNSS_qcom_cfg.ini"', PKG)


if __name__ == "__main__":
    unittest.main(verbosity=2)
