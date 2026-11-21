import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
import zipfile


PROJECT = Path(__file__).resolve().parents[1]


class ProjectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location(
            "project_compiler", PROJECT / "compiler" / "qecompile.py")
        launcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(launcher)
        cls.compiler, sdk_path = launcher.load_compiler()
        cls.sdk = cls.compiler.load_sdk(sdk_path)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qeapp-project-test-")
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name).resolve() / "project-copy"
        shutil.copytree(PROJECT, self.project,
                        ignore=shutil.ignore_patterns("build", "__pycache__"))

    def test_manifest_and_sources(self):
        manifest = json.loads((self.project / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(self.sdk.validate_manifest(manifest), [])
        sources = [self.project / manifest["entry"]] + sorted((self.project / "libs").rglob("*.qe"))
        for source in sources:
            with self.subTest(source=source.name):
                self.assertEqual(self.sdk.compile_qe(source.read_text(encoding="utf-8")), [])
        config = json.loads((self.project / "packaging" / "package.json").read_text(encoding="utf-8"))
        self.compiler.validate_packaging(config)

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
                    self.assertEqual(archive.read("SIGNATURE").decode("ascii"),
                                     self.sdk.package_signature([(name, archive.read(name)) for name in names]))
                    manifest = json.loads(archive.read("manifest.json"))
                    compiled = archive.read(manifest["entry"]).decode("utf-8")
                    self.assertIn("Hello from libs/greeting.qe", compiled)
                    self.assertEqual(len(archive.read(manifest["icon"])), 64 * 64 * 4)
                    self.assertEqual("debug/source-map.json" in names, mode == "debug")
                    if mode == "debug":
                        mapping = json.loads(archive.read("debug/source-map.json"))
                        for item in mapping["lines"]:
                            source = archive.read("debug/sources/" + item["source"]).decode("utf-8")
                            self.assertEqual(compiled.splitlines()[item["compiled_line"] - 1],
                                             source.splitlines()[item["line"] - 1])
                original = package.read_bytes()
                self.compiler.compile_project(self.project, mode, self.sdk)
                self.assertEqual(package.read_bytes(), original)

    def test_package_name_does_not_change_app_identity(self):
        config_path = self.project / "packaging" / "package.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        config["name"] = "renamed-app"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        manifest = (self.project / "manifest.json").read_bytes()
        package = self.compiler.compile_project(self.project, "release", self.sdk)
        self.assertEqual(package.name, "renamed-app.qeapp")
        with zipfile.ZipFile(package) as archive:
            self.assertEqual(archive.read("manifest.json"), manifest)

    def test_invalid_package_names_preserve_last_build(self):
        previous = self.compiler.compile_project(self.project, "release", self.sdk)
        original = previous.read_bytes()
        config_path = self.project / "packaging" / "package.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        for name in ("../outside", "a/b", "C:\\bad", "", "con", "app.qeapp", "x" * 65, None):
            with self.subTest(name=name):
                config["name"] = name
                config_path.write_text(json.dumps(config), encoding="utf-8")
                with self.assertRaises(ValueError):
                    self.compiler.compile_project(self.project, "release", self.sdk)
                self.assertEqual(previous.read_bytes(), original)

    def test_invalid_library_reports_source_line(self):
        (self.project / "libs" / "greeting.qe").write_text('print "unterminated\n', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "libs/greeting.qe: line 1: unterminated"):
            self.compiler.compile_project(self.project, "release", self.sdk)
        self.assertFalse((self.project / "build").exists())


if __name__ == "__main__":
    unittest.main()
