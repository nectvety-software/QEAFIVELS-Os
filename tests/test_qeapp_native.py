import importlib.util
import io
import json
import shutil
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


qeaf = load_module("qeaf", ROOT / "sdk" / "cli" / "qeaf.py")
qen = load_module("qen", ROOT / "sdk" / "compiler" / "qen.py")
compiler = load_module("qecompile", ROOT / "sdk" / "compiler" / "qecompile.py")
TEMPLATE = ROOT / "sdk" / "templates" / "hello-qeapp-native"


def native_manifest(**overrides):
    manifest = {
        "format": "qeapp",
        "format_version": 2,
        "abi_version": 2,
        "runtime": "native",
        "id": "org.test.native",
        "name": "Native Test",
        "version": "1.0.0",
        "min_os": "0.3.0",
        "entry": "native/arm64/app.qen",
        "icon": "assets/icon.rgba",
        "architecture": ["arm64"],
        "permissions": ["ui.display"],
        "dependencies": [],
    }
    manifest.update(overrides)
    return manifest


def write_package(path, manifest, module, icon=None):
    if icon is None:
        icon = b"\0" * (64 * 64 * 4)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("manifest.json", json.dumps(manifest))
        archive.writestr(manifest["entry"], module)
        archive.writestr("assets/icon.rgba", icon)


class NativeQEAPPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="qeapp-native-")
        cls.project = Path(cls.temp.name) / "hello-native"
        shutil.copytree(TEMPLATE, cls.project,
                        ignore=shutil.ignore_patterns("build", "__pycache__"))
        cls.package = compiler.compile_project(cls.project, "release", qeaf)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_native_manifest_valid(self):
        m = json.loads((TEMPLATE / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(qeaf.validate_manifest(m), [])
        self.assertEqual(m["abi_version"], 5)

    def test_native_package_valid(self):
        self.assertTrue(self.package.is_file())
        self.assertEqual(qeaf.validate_package(self.package), [])
        with zipfile.ZipFile(self.package) as archive:
            names = archive.namelist()
            self.assertIn("native/arm64/app.qen", names)
            self.assertNotIn("src/main.qe", names)
            module = archive.read("native/arm64/app.qen")
        self.assertEqual(qen.validate_module(module, "arm64"), [])

    def test_missing_v2_fields(self):
        m = native_manifest()
        for key in ("runtime", "abi_version"):
            broken = dict(m)
            del broken[key]
            self.assertIn("missing fields", " ".join(qeaf.validate_manifest(broken)))

    def test_wrong_abi_rejected(self):
        # ABI v3 (media blit), v4 (text weight) and v5 (layout weight) are
        # accepted; v6 is not.
        self.assertEqual(qeaf.validate_manifest(native_manifest(abi_version=4)), [])
        self.assertEqual(qeaf.validate_manifest(native_manifest(abi_version=5)), [])
        self.assertIn("abi_version must be 2, 3, 4 or 5",
                      " ".join(qeaf.validate_manifest(native_manifest(abi_version=6))))

    def test_wrong_runtime_rejected(self):
        self.assertIn("runtime must be native",
                      " ".join(qeaf.validate_manifest(native_manifest(runtime="declarative"))))

    def test_native_v2_requires_one_architecture(self):
        self.assertIn("exactly one architecture",
                      " ".join(qeaf.validate_manifest(native_manifest(
                          architecture=["arm64", "x86_64"]))))

    def test_bad_entry_extension_rejected(self):
        self.assertIn("entry must be a relative .qen path",
                      " ".join(qeaf.validate_manifest(native_manifest(entry="src/main.qe"))))

    def test_unsafe_entry_rejected(self):
        self.assertIn("entry must be a relative .qen path",
                      " ".join(qeaf.validate_manifest(native_manifest(entry="../app.qen"))))

    def test_bad_native_header_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            pkg = Path(td) / "bad.qeapp"
            write_package(pkg, native_manifest(), b"NOT-QEN" + b"\0" * 64)
            errors = qeaf.validate_package(pkg)
        self.assertTrue(any("bad module magic" in e for e in errors), errors)

    def test_arch_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            pkg = Path(td) / "arch.qeapp"
            write_package(pkg, native_manifest(architecture=["x86_64"]),
                          qen.build_stub("arm64"))
            errors = qeaf.validate_package(pkg)
        self.assertTrue(any("architecture mismatch" in e for e in errors), errors)

    def test_tampered_hash_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            pkg = Path(td) / "hash.qeapp"
            module = bytearray(qen.build_stub("arm64"))
            module[49] ^= 0xFF  # payload_hash
            write_package(pkg, native_manifest(), bytes(module))
            errors = qeaf.validate_package(pkg)
        self.assertTrue(any("payload hash mismatch" in e for e in errors), errors)

    def test_qen_rejects_truncated_and_oversized(self):
        self.assertTrue(qen.validate_module(b"QEAF-QEN1"))
        self.assertTrue(qen.validate_module(b"\0" * (qen.MAX_MODULE_SIZE + 1)))

    def test_qen_rejects_section_overrun(self):
        module = bytearray(qen.build_stub("arm64"))
        module[21:25] = (1).to_bytes(4, "little")  # code_size with no payload
        self.assertIn("section sizes exceed module payload",
                      " ".join(qen.validate_module(bytes(module), "arm64")))

    def test_v1_manifest_still_valid(self):
        m = {
            "format": "qeapp", "format_version": 1, "id": "org.test.v1",
            "name": "V1", "version": "1.0.0", "min_os": "0.1.0",
            "entry": "src/main.qe", "architecture": ["arm64"],
            "permissions": [], "dependencies": [],
        }
        self.assertEqual(qeaf.validate_manifest(m), [])

    def test_create_native_scaffold(self):
        with tempfile.TemporaryDirectory() as td:
            target = Path(td) / "my-native-app"
            old = __import__("sys").argv
            __import__("sys").argv = ["qeaf", "create", str(target), "--native",
                                      "--id", "org.example.native", "--arch", "arm64"]
            try:
                output = io.StringIO()
                with redirect_stdout(output), redirect_stderr(output):
                    status = qeaf.main()
            finally:
                __import__("sys").argv = old
            self.assertEqual(status, 0, output.getvalue())
            manifest = json.loads((target / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(qeaf.validate_manifest(manifest), [])
            module = (target / manifest["entry"]).read_bytes()
            self.assertEqual(qen.validate_module(module, "arm64"), [])
            self.assertEqual(int.from_bytes(module[53:55], "little"),
                             manifest["abi_version"])
            self.assertFalse((target / "src" / "main.qe").exists())


class QenTableTests(unittest.TestCase):
    """Import/export table packing and bounds (pure Python, no toolchain)."""

    def test_tables_roundtrip(self):
        exports = [("create", 0), ("start", 0), ("stop", 0), ("destroy", 0)]
        module = qen.build_module(b"\xc3", exports=exports)
        self.assertEqual(qen.validate_module(module, "x86_64"), [])
        imports, parsed = qen.read_tables(module)
        self.assertEqual(imports, [qen.IMPORT_API_V2])
        self.assertEqual(parsed, exports)

    def test_multiple_imports_parsed(self):
        module = qen.build_module(b"\xc3", exports=[("create", 0)],
                                  imports=(qen.IMPORT_API_V2, "extra.name"))
        imports, _ = qen.read_tables(module)
        self.assertEqual(imports, [qen.IMPORT_API_V2, "extra.name"])
        self.assertEqual(qen.validate_module(module, "x86_64"), [])

    def test_export_offset_out_of_bounds(self):
        module = qen.build_module(b"\xc3", exports=[("create", 0), ("start", 999)])
        self.assertIn("export offset outside code section",
                      " ".join(qen.validate_module(module, "x86_64")))

    def test_entry_offset_out_of_bounds(self):
        with self.assertRaises(ValueError):
            qen.build_module(b"\xc3", exports=[("create", 0)], entry_offset=999)

    def test_empty_import_name_rejected(self):
        with self.assertRaises(ValueError):
            qen.build_module(b"\xc3", exports=[("create", 0)], imports=("",))

    def test_v4_module_roundtrip(self):
        exports = [("create", 0), ("start", 0), ("stop", 0), ("destroy", 0)]
        module = qen.build_module(b"\xc3", exports=exports,
                                  abi_version=4, min_api=4)
        self.assertEqual(qen.validate_module(module, "x86_64"), [])
        imports, parsed = qen.read_tables(module)
        self.assertEqual(imports, [qen.IMPORT_API_V4])
        self.assertEqual(parsed, exports)
        self.assertEqual(module[10], 4)

    def test_v4_wrong_import_rejected(self):
        module = qen.build_module(b"\xc3", exports=[("create", 0)],
                                  abi_version=4, min_api=4,
                                  imports=(qen.IMPORT_API_V2,))
        errors = " ".join(qen.validate_module(module, "x86_64"))
        self.assertIn("module must import qeaf_app_api_v4", errors)
        self.assertIn("module must not import qeaf_app_api_v2", errors)

    def test_v4_rejects_mixed_imports(self):
        module = qen.build_module(b"\xc3", exports=[("create", 0)],
                                  abi_version=4, min_api=4,
                                  imports=(qen.IMPORT_API_V4, qen.IMPORT_API_V3))
        self.assertIn("module must not import qeaf_app_api_v3",
                      " ".join(qen.validate_module(module, "x86_64")))

    def test_v5_module_roundtrip(self):
        exports = [("create", 0), ("start", 0), ("stop", 0), ("destroy", 0)]
        module = qen.build_module(b"\xc3", exports=exports,
                                  abi_version=5, min_api=5)
        self.assertEqual(qen.validate_module(module, "x86_64"), [])
        imports, parsed = qen.read_tables(module)
        self.assertEqual(imports, [qen.IMPORT_API_V5])
        self.assertEqual(parsed, exports)
        self.assertEqual(module[10], 5)

    def test_v5_wrong_import_rejected(self):
        module = qen.build_module(b"\xc3", exports=[("create", 0)],
                                  abi_version=5, min_api=5,
                                  imports=(qen.IMPORT_API_V4,))
        errors = " ".join(qen.validate_module(module, "x86_64"))
        self.assertIn("module must import qeaf_app_api_v5", errors)
        self.assertIn("module must not import qeaf_app_api_v4", errors)

    def test_v5_rejects_mixed_imports(self):
        module = qen.build_module(b"\xc3", exports=[("create", 0)],
                                  abi_version=5, min_api=5,
                                  imports=(qen.IMPORT_API_V5, qen.IMPORT_API_V2))
        self.assertIn("module must not import qeaf_app_api_v2",
                      " ".join(qen.validate_module(module, "x86_64")))

    def test_v5_min_api_must_match(self):
        with self.assertRaises(ValueError):
            qen.build_module(b"\xc3", exports=[("create", 0)],
                             abi_version=5, min_api=4)


def _zig_available():
    try:
        compiler.find_zig()
        return True
    except ValueError:
        return False


@unittest.skipUnless(_zig_available(), "zig toolchain not available")
class NativeLinkerTests(unittest.TestCase):
    """The PIC static linker emits structurally valid modules for both arches.

    arm64 is validated structurally only; executing it is a Phase 4 device gate.
    """

    def _link(self, arch):
        with tempfile.TemporaryDirectory(prefix="qenlink-test-") as td:
            project = Path(td) / "proj"
            (project / "src").mkdir(parents=True)
            entry = "native/%s/app.qen" % arch
            module = compiler.compile_native(qen, project, arch,
                                             [TEMPLATE / "src" / "main.c"], entry,
                                             abi_version=5)
        self.assertEqual(qen.validate_module(module, arch), [])
        imports, exports = qen.read_tables(module)
        self.assertEqual(imports, [qen.IMPORT_API_V5])
        names = {name for name, _ in exports}
        self.assertTrue({"create", "start", "stop", "destroy"} <= names, names)

    def test_link_x86_64(self):
        self._link("x86_64")

    def test_link_arm64(self):
        self._link("arm64")


if __name__ == "__main__":
    unittest.main()
