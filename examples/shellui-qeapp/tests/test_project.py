import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
import zipfile


PROJECT = Path(__file__).resolve().parents[1]


class NativeProjectTests(unittest.TestCase):
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
        self.temp = tempfile.TemporaryDirectory(prefix="qeapp-native-test-")
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name).resolve() / "project-copy"
        shutil.copytree(PROJECT, self.project,
                        ignore=shutil.ignore_patterns("build", "__pycache__"))

    def test_manifest_and_module(self):
        manifest = json.loads((self.project / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(self.sdk.validate_manifest(manifest), [])
        module = (self.project / manifest["entry"]).read_bytes()
        self.assertEqual(self.qen.validate_module(module, manifest["architecture"][0]), [])

    def test_release_and_debug_packages(self):
        config = json.loads((self.project / "packaging" / "package.json").read_text(encoding="utf-8"))
        for mode, folder in (("release", "releases"), ("debug", "debug")):
            with self.subTest(mode=mode):
                package = self.compiler.compile_project(self.project, mode, self.sdk)
                self.assertEqual(package, self.project / "build" / folder / (config["name"] + ".qeapp"))
                self.assertEqual(self.sdk.validate_package(package), [])
                with zipfile.ZipFile(package) as archive:
                    self.assertIsNone(archive.testzip())
                    names = archive.namelist()
                    self.assertFalse(any(name.startswith(("build/", "compiler/", "tests/", "packaging/"))
                                         or name in ("build.py", "README.md") for name in names))
                    self.assertTrue(all(item.compress_type == zipfile.ZIP_STORED
                                        for item in archive.infolist()))
                    manifest = json.loads(archive.read("manifest.json"))
                    self.assertTrue(manifest["entry"].endswith(".qen"))
                    module = archive.read(manifest["entry"])
                    self.assertEqual(self.qen.validate_module(module, manifest["architecture"][0]), [])
                    self.assertEqual(len(archive.read(manifest["icon"])), 64 * 64 * 4)
                    self.assertNotIn("src/main.qe", names)

    def test_missing_module_gets_compiler_stub(self):
        manifest = json.loads((self.project / "manifest.json").read_text(encoding="utf-8"))
        (self.project / manifest["entry"]).unlink()
        package = self.compiler.compile_project(self.project, "release", self.sdk)
        with zipfile.ZipFile(package) as archive:
            module = archive.read(manifest["entry"])
        self.assertEqual(self.qen.validate_module(module, manifest["architecture"][0]), [])

    def test_tampered_module_is_rebuilt_from_source(self):
        # This project ships C sources, so a stale or tampered checked-in
        # module is not trusted: the build recompiles and replaces it.
        manifest = json.loads((self.project / "manifest.json").read_text(encoding="utf-8"))
        module = bytearray((self.project / manifest["entry"]).read_bytes())
        module[49] ^= 0xFF  # payload_hash
        (self.project / manifest["entry"]).write_bytes(module)
        package = self.compiler.compile_project(self.project, "release", self.sdk)
        with zipfile.ZipFile(package) as archive:
            rebuilt = archive.read(manifest["entry"])
        self.assertEqual(self.qen.validate_module(rebuilt, manifest["architecture"][0]), [])
        self.assertNotEqual(rebuilt[49], module[49])


if __name__ == "__main__":
    unittest.main()
