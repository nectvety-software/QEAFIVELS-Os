import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
import zipfile


PROJECT = Path(__file__).resolve().parents[1]


class RustProjectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location(
            "project_compiler", PROJECT / "compiler" / "qecompile.py")
        launcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(launcher)
        cls.compiler, sdk_path = launcher.load_compiler()
        cls.sdk = cls.compiler.load_sdk(sdk_path)
        try:
            cls.compiler.load_qenrust().find_rustc()
        except ValueError:
            raise unittest.SkipTest("rustc not available")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qeapp-rust-test-")
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name).resolve() / "project-copy"
        shutil.copytree(PROJECT, self.project,
                        ignore=shutil.ignore_patterns("build", "__pycache__"))
        manifest_path = self.project / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["architecture"] = ["x86_64"]
        manifest["entry"] = "native/x86_64/app.qen"
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    def test_release_and_debug_packages(self):
        for mode in ("release", "debug"):
            with self.subTest(mode=mode):
                package = self.compiler.compile_project(self.project, mode, self.sdk)
                metadata = self.sdk.load_qeapp().preview_package(package)
                self.assertTrue(metadata["signature_verified"])
                self.assertFalse(metadata["publisher_authenticated"])
                with zipfile.ZipFile(package) as archive:
                    self.assertIn("native/x86_64/app.qen", archive.namelist())
                    if mode == "debug":
                        self.assertIn("debug/sources/src/main.rs", archive.namelist())
                    else:
                        self.assertFalse(any(name.startswith("debug/")
                                             for name in archive.namelist()))


if __name__ == "__main__":
    unittest.main()
