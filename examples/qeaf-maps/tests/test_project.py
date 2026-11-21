import json
from pathlib import Path
import unittest
import zipfile

PROJECT = Path(__file__).resolve().parents[1]

class QeafMapsProjectTests(unittest.TestCase):
    def test_manifest_declares_only_display_permission(self):
        manifest = json.loads((PROJECT / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["format_version"], 2)
        self.assertEqual(manifest["abi_version"], 3)
        self.assertEqual(manifest["runtime"], "native")
        self.assertEqual(manifest["id"], "org.vqeaf.maps")
        self.assertEqual(manifest["permissions"], ["ui.display"])
        self.assertEqual(manifest["architecture"], ["arm64"])

    def test_source_uses_framework_rendering_not_syscalls(self):
        source = (PROJECT / "src" / "main.c").read_text(encoding="utf-8")
        self.assertIn("qeaf_app_api_v3", source)
        self.assertIn("blit_pixels", source)
        self.assertIn("SHOW LIST", source)
        self.assertIn("static void pin(", source)
        self.assertNotIn("syscall(", source)
        self.assertNotIn("#include <unistd.h>", source)

    def test_release_package_is_valid_zip(self):
        package = PROJECT / "build" / "releases" / "qeaf-maps.qeapp"
        if not package.exists():
            self.skipTest("build the project first")
        with zipfile.ZipFile(package) as archive:
            self.assertIsNone(archive.testzip())
            self.assertIn("manifest.json", archive.namelist())
            self.assertIn("native/arm64/app.qen", archive.namelist())
            manifest = json.loads(archive.read("manifest.json"))
            self.assertEqual(manifest["id"], "org.vqeaf.maps")

if __name__ == "__main__":
    unittest.main()
