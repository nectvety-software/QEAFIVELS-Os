import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = (ROOT / "scripts/build_violet.py").read_text(encoding="utf-8")
PACKAGE = (ROOT / "scripts/package_violet.py").read_text(encoding="utf-8")
SOURCE_PACKAGE = (ROOT / "scripts/package_violet_source_kernel.py").read_text(encoding="utf-8")


class VioletBuildIntegrityTests(unittest.TestCase):
    def test_incremental_objects_are_keyed_by_compiler_and_full_flags(self):
        self.assertIn("import hashlib", BUILD)
        self.assertIn("compiler + tuple(CFLAGS) + tuple(INCLUDES) + tuple(DRIVES_INCLUDES)", BUILD)
        self.assertIn("if previous != config:", BUILD)
        self.assertIn("shutil.rmtree(OBJDIR, ignore_errors=True)", BUILD)
        self.assertIn("shutil.rmtree(DRIVES_OBJDIR, ignore_errors=True)", BUILD)
        self.assertIn("prepare_build(build_config_digest())", BUILD)

    def test_failed_compiles_cannot_leave_reusable_objects(self):
        self.assertEqual(BUILD.count("if os.path.isfile(obj):\n            os.remove(obj)"), 2)

    def test_source_package_rejects_duplicate_init_entries(self):
        self.assertIn('if name in ("init", "./init")', SOURCE_PACKAGE)
        self.assertIn("require(len(init_entries) == 1,", SOURCE_PACKAGE)
        self.assertIn("ramdisk must contain exactly one /init entry", SOURCE_PACKAGE)

    def test_standard_package_rejects_diagnostic_mode_and_stale_flags(self):
        self.assertIn('if "-DVQEAF_RAM_ONLY_DIAGNOSTICS" in build_violet.CFLAGS:', PACKAGE)
        self.assertIn("fail(\"standard Violet packaging refuses VQEAF_RAM_ONLY_DIAGNOSTICS\")", PACKAGE)
        self.assertIn("check_build_config(builder)", PACKAGE)
        self.assertIn("rebuild without --skip-build", PACKAGE)


if __name__ == "__main__":
    unittest.main()
