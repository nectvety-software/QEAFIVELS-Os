import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / "sdk" / "templates" / "hello-qeapp-rust"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


qeaf = load_module("qeaf_rust", ROOT / "sdk" / "cli" / "qeaf.py")
qen = load_module("qen_rust", ROOT / "sdk" / "compiler" / "qen.py")
compiler = load_module("qecompile_rust", ROOT / "sdk" / "compiler" / "qecompile.py")
qenrust = load_module("qenrust", ROOT / "sdk" / "compiler" / "qenrust.py")
qesim = load_module("qesim_rust", ROOT / "sdk" / "simulator" / "qesim.py")


def rust_available(arch):
    try:
        rustc = qenrust.find_rustc()
        return qenrust.target_installed(rustc, qenrust.rust_target(arch))
    except ValueError:
        return False


def zig_available():
    try:
        qesim.find_zig()
        return True
    except ValueError:
        return False


def copy_project(parent, arch="x86_64"):
    project = Path(parent) / "project"
    shutil.copytree(TEMPLATE, project,
                    ignore=shutil.ignore_patterns("build", "__pycache__"))
    manifest_path = project / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["architecture"] = [arch]
    manifest["entry"] = "native/%s/app.qen" % arch
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n",
                             encoding="utf-8")
    return project


def compile_quietly(project, mode):
    output = io.StringIO()
    with redirect_stdout(output), redirect_stderr(output):
        package = compiler.compile_project(project, mode, qeaf)
    return package, output.getvalue()


API_PROBE = r'''
#[cfg(qeapp_combined_crate)]
use crate::qeapp as qeapp_sdk;
#[cfg(not(qeapp_combined_crate))]
extern crate qeapp_sdk;

use qeapp_sdk::prelude::*;

struct ApiProbe {
    api: ApiV5,
}

impl QeappApp for ApiProbe {
    type Api = ApiV5;

    fn new(api: &ApiV5) -> Self {
        Self { api: *api }
    }

    fn start(&mut self) -> i32 {
        unsafe {
            if self.api.log("rust api: start").is_err() ||
               self.api.draw_text(16, 32, "Rust API probe").is_err() ||
               self.api.draw_text_weighted(16, 48, "Bold", TEXT_WEIGHT_BOLD).is_err() ||
               self.api.blit_pixels(16, 64, 1, 1, 1, &[0xF800]).is_err() {
                return -1;
            }
        }
        let weights = [1u32, 3u32];
        let mut rects = [Rect::default(), Rect::default()];
        if unsafe {
            self.api.layout_split_weighted(16, 80, 100, 20, LAYOUT_AXIS_HORIZONTAL,
                                           &weights, 4, &mut rects)
        }.is_err() {
            return -1;
        }
        let mut part = [0u8; 4];
        if unsafe { self.api.read_file_part("missing.bin", 0, &mut part) }.is_ok() {
            return -1;
        }
        if unsafe { self.api.read_file("missing.bin") }.is_ok() {
            return -1;
        }
        if unsafe { self.api.log("invalid\0text") }.is_ok() {
            return -1;
        }
        if unsafe { self.api.log("rust api: checks-ok") }.is_err() {
            -1
        } else {
            0
        }
    }

    fn on_input(&mut self, event: &InputEvent) {
        if event.kind == INPUT_KEY && event.pressed != 0 {
            let _ = unsafe { self.api.log("rust api: input") };
        }
    }
}

qeapp_sdk::qeapp_app!(ApiProbe);
'''


@unittest.skipUnless(rust_available("x86_64"), "Rust x86_64 target not installed")
class RustQEAPPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="qeapp-rust-")
        cls.project = copy_project(cls.temp.name)
        cls.release, _ = compile_quietly(cls.project, "release")
        cls.debug, _ = compile_quietly(cls.project, "debug")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_release_and_debug_packages_validate_and_preview(self):
        for package in (self.release, self.debug):
            self.assertEqual(qeaf.validate_package(package), [])
            metadata = qeaf.load_qeapp().preview_package(package)
            self.assertEqual(metadata["runtime"], "native")
            self.assertTrue(metadata["signature_verified"])
            self.assertFalse(metadata["publisher_authenticated"])

    def test_rust_module_has_v5_import_and_lifecycle_exports(self):
        with zipfile.ZipFile(self.release) as archive:
            module = archive.read("native/x86_64/app.qen")
        self.assertGreater(len(module), qen.HEADER_SIZE)
        self.assertEqual(qen.validate_module(module, "x86_64"), [])
        self.assertEqual(module[10], 5)
        self.assertEqual(module[11], qen.ARCH_X86_64)
        imports, exports = qen.read_tables(module)
        self.assertEqual(imports, [qen.IMPORT_API_V5])
        offsets = dict(exports)
        self.assertTrue({"create", "start", "pause", "resume", "stop",
                         "destroy", "on_input"} <= offsets.keys())
        self.assertEqual(int.from_bytes(module[37:41], "little"), offsets["create"])

    def test_debug_sources_are_retained_only_in_debug(self):
        with zipfile.ZipFile(self.release) as archive:
            self.assertFalse(any(name.startswith("debug/") for name in archive.namelist()))
        with zipfile.ZipFile(self.debug) as archive:
            self.assertIn("debug/source-map.json", archive.namelist())
            self.assertIn("debug/sources/src/main.rs", archive.namelist())

    def test_release_package_is_deterministic(self):
        rebuilt, _ = compile_quietly(self.project, "release")
        self.assertEqual(self.release.read_bytes(), rebuilt.read_bytes())

    @unittest.skipUnless(zig_available(), "zig toolchain not available")
    def test_rust_sample_runs_in_host_simulator(self):
        simulator = qesim.build_simulator(Path(self.temp.name) / "sim")
        proc = subprocess.run([str(simulator), str(self.release)],
                              capture_output=True, text=True, encoding="utf-8",
                              errors="replace", timeout=60)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("state: destroyed", proc.stdout)
        self.assertIn("lifecycle: create start pause resume stop destroy", proc.stdout)
        self.assertIn("Hello Rust QEAPP: start", proc.stdout)
        self.assertIn("Hello from Rust QEAPP v5", proc.stdout)
        self.assertIn("outstanding allocs: 0", proc.stdout)
        self.assertNotIn("hash=0x00000000", proc.stdout)

    @unittest.skipUnless(zig_available(), "zig toolchain not available")
    def test_api_surface_and_input_run_in_host_simulator(self):
        with tempfile.TemporaryDirectory(prefix="qeapp-rust-api-") as temp:
            project = copy_project(temp)
            (project / "src" / "main.rs").write_text(API_PROBE, encoding="utf-8")
            manifest_path = project / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["permissions"] = ["ui.display", "storage.read", "input.receive"]
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n",
                                     encoding="utf-8")
            package, _ = compile_quietly(project, "release")
            simulator = qesim.build_simulator(Path(self.temp.name) / "sim")
            proc = subprocess.run([str(simulator), str(package), "--keys", "103"],
                                  capture_output=True, text=True, encoding="utf-8",
                                  errors="replace", timeout=60)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("rust api: checks-ok", proc.stdout)
        self.assertIn("rust api: input", proc.stdout)
        self.assertIn("input delivered: 1", proc.stdout)
        self.assertIn("outstanding allocs: 0", proc.stdout)
        self.assertIn("state: destroyed", proc.stdout)

    def test_mixed_c_and_rust_sources_rejected(self):
        with tempfile.TemporaryDirectory(prefix="qeapp-rust-mixed-") as temp:
            project = copy_project(temp)
            (project / "src" / "extra.c").write_text("void extra(void) {}\n",
                                                     encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "cannot mix C and Rust"):
                compile_quietly(project, "release")

    def test_missing_lifecycle_exports_rejected(self):
        with tempfile.TemporaryDirectory(prefix="qeapp-rust-exports-") as temp:
            project = copy_project(temp)
            (project / "src" / "main.rs").write_text(
                "#[no_mangle]\npub extern \"C\" fn not_a_lifecycle_export() {}\n",
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "missing lifecycle exports"):
                compile_quietly(project, "release")

    def test_undefined_native_symbol_rejected(self):
        source = '''
unsafe extern "C" { fn absent_qeapp_symbol(); }
#[no_mangle]
pub unsafe extern "C" fn create() -> *mut core::ffi::c_void {
    absent_qeapp_symbol();
    core::ptr::null_mut()
}
#[no_mangle] pub extern "C" fn start() {}
#[no_mangle] pub extern "C" fn stop() {}
#[no_mangle] pub extern "C" fn destroy() {}
'''
        with tempfile.TemporaryDirectory(prefix="qeapp-rust-undef-") as temp:
            project = copy_project(temp)
            (project / "src" / "main.rs").write_text(source, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "undefined symbol: absent_qeapp_symbol"):
                compile_quietly(project, "release")

    def test_rust_scaffold_is_self_contained(self):
        with tempfile.TemporaryDirectory(prefix="qeapp-rust-create-") as temp:
            project = Path(temp) / "rust-app"
            old_argv = sys.argv
            sys.argv = ["qeaf", "create", str(project), "--rust", "--arch", "x86_64",
                        "--id", "org.example.rust-app"]
            try:
                output = io.StringIO()
                with redirect_stdout(output), redirect_stderr(output):
                    status = qeaf.main()
            finally:
                sys.argv = old_argv
            self.assertEqual(status, 0, output.getvalue())
            self.assertTrue((project / "sdk" / "rust" / "src" / "lib.rs").is_file())
            cargo = (project / "Cargo.toml").read_text(encoding="utf-8")
            self.assertIn('path = "sdk/rust"', cargo)
            self.assertNotIn('path = "../../rust"', cargo)
            if shutil.which("cargo"):
                metadata = subprocess.run(
                    ["cargo", "metadata", "--no-deps", "--format-version", "1"],
                    cwd=project, capture_output=True, text=True, encoding="utf-8",
                    errors="replace", timeout=60)
                self.assertEqual(metadata.returncode, 0,
                                 metadata.stdout + metadata.stderr)
                package = json.loads(metadata.stdout)["packages"][0]
                self.assertEqual(package["name"], "hello-qeapp-rust")
            package, _ = compile_quietly(project, "release")
            self.assertEqual(qeaf.validate_package(package), [])


@unittest.skipUnless(rust_available("arm64"), "Rust AArch64 target not installed")
class RustAArch64Tests(unittest.TestCase):
    def test_arm64_module_builds_and_validates(self):
        with tempfile.TemporaryDirectory(prefix="qeapp-rust-arm64-") as temp:
            project = copy_project(temp, "arm64")
            package, _ = compile_quietly(project, "release")
            self.assertEqual(qeaf.validate_package(package), [])
            with zipfile.ZipFile(package) as archive:
                module = archive.read("native/arm64/app.qen")
        self.assertGreater(len(module), qen.HEADER_SIZE)
        self.assertEqual(qen.validate_module(module, "arm64"), [])
        self.assertEqual(module[11], qen.ARCH_ARM64)
        imports, exports = qen.read_tables(module)
        self.assertEqual(imports, [qen.IMPORT_API_V5])
        self.assertIn("create", dict(exports))


class RustToolchainTests(unittest.TestCase):
    def test_unsupported_architecture_rejected(self):
        with self.assertRaisesRegex(ValueError, "unsupported Rust architecture"):
            qenrust.rust_target("riscv64")

    def test_missing_rustc_reported(self):
        old_rustc = os.environ.get("RUSTC")
        old_which = qenrust.shutil.which
        os.environ["RUSTC"] = ""
        qenrust.shutil.which = lambda _name: None
        try:
            with self.assertRaisesRegex(ValueError, "rustc not found"):
                qenrust.find_rustc()
        finally:
            qenrust.shutil.which = old_which
            if old_rustc is None:
                os.environ.pop("RUSTC", None)
            else:
                os.environ["RUSTC"] = old_rustc


if __name__ == "__main__":
    unittest.main()
