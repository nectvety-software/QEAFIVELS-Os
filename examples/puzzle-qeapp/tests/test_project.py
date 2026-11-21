import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
import zipfile


PROJECT = Path(__file__).resolve().parents[1]


class PuzzleProjectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location(
            "project_compiler", PROJECT / "compiler" / "qecompile.py")
        launcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(launcher)
        cls.compiler, sdk_path = launcher.load_compiler()
        cls.sdk = cls.compiler.load_sdk(sdk_path)
        cls.qen = cls.compiler.load_qen()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qeapp-puzzle-test-")
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name).resolve() / "project-copy"
        shutil.copytree(PROJECT, self.project,
                        ignore=shutil.ignore_patterns("build", "__pycache__"))

    def test_manifest_and_module(self):
        manifest = json.loads((self.project / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(self.sdk.validate_manifest(manifest), [])
        module = (self.project / manifest["entry"]).read_bytes()
        self.assertEqual(self.qen.validate_module(module, manifest["architecture"][0]), [])

    def test_release_package(self):
        config = json.loads((self.project / "packaging" / "package.json").read_text(encoding="utf-8"))
        package = self.compiler.compile_project(self.project, "release", self.sdk)
        self.assertEqual(package, self.project / "build" / "releases" / (config["name"] + ".qeapp"))
        self.assertEqual(self.sdk.validate_package(package), [])
        with zipfile.ZipFile(package) as archive:
            self.assertIsNone(archive.testzip())
            names = archive.namelist()
            self.assertIn("manifest.json", names)
            self.assertIn("assets/icon.rgba", names)


if __name__ == "__main__":
    unittest.main()
