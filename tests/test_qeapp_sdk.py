import hashlib
import hmac
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from sdk.qeapp import QEAPPError, preview_package
from sdk.qeapp import package as qeapp_package

SPEC = importlib.util.spec_from_file_location("qeaf_sdk_tests", ROOT / "sdk" / "cli" / "qeaf.py")
qeaf = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(qeaf)
qen = qeaf.load_qen()

SCRIPT = b'app Sample\n# comment\nprint "hello\\nworld"\n'
ICON = b"\0" * (64 * 64 * 4)
DEFAULT_PATH = object()


def v1_manifest(**overrides):
    manifest = {
        "format": "qeapp",
        "format_version": 1,
        "id": "org.test.sample",
        "name": "Sample QEAPP",
        "version": "1.2.3",
        "min_os": "0.1.0",
        "entry": "src/main.qe",
        "architecture": ["x86_64"],
        "permissions": ["ui.display", "storage.read"],
        "dependencies": ["org.test.library"],
    }
    manifest.update(overrides)
    return manifest


def v2_manifest(**overrides):
    manifest = {
        "format": "qeapp",
        "format_version": 2,
        "runtime": "native",
        "abi_version": 5,
        "id": "org.test.native",
        "name": "Native QEAPP",
        "version": "2.0.0",
        "min_os": "0.3.0",
        "entry": "native/arm64/app.qen",
        "architecture": ["arm64"],
        "permissions": ["ui.display"],
        "dependencies": [],
    }
    manifest.update(overrides)
    return manifest


def manifest_entry(manifest):
    return ("manifest.json", json.dumps(manifest, sort_keys=True,
                                        ensure_ascii=False).encode("utf-8"))


def v1_entries(manifest=None, script=SCRIPT, icon=ICON):
    manifest = v1_manifest() if manifest is None else manifest
    entries = [manifest_entry(manifest), (manifest["entry"], script)]
    if icon is not None:
        entries.append(("assets/icon.rgba", icon))
    return entries


def signed(entries):
    payload = [(name, data) for name, data in entries if name != "SIGNATURE"]
    signature = qeaf.package_signature(payload).encode("ascii")
    return payload + [("SIGNATURE", signature)]


def write_package(path, entries, sign=True, compression=zipfile.ZIP_STORED):
    if sign:
        entries = signed(entries)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(path, "w", compression=compression) as archive:
            for name, data in sorted(entries, key=lambda item: item[0]):
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = compression
                info.external_attr = 0o100644 << 16
                archive.writestr(info, data)
    return path


def rewrite_entry(path, name, data, sign=False):
    with zipfile.ZipFile(path) as archive:
        entries = [(item, archive.read(item)) for item in archive.namelist()]
    entries = [(item, data if item == name else content) for item, content in entries]
    write_package(path, entries, sign=sign)


class QEAPPSDKTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qeapp-sdk-")
        self.directory = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def package(self, name="sample.qeapp"):
        return self.directory / name

    def assert_error(self, code, path=DEFAULT_PATH):
        with self.assertRaises(QEAPPError) as caught:
            preview_package(self.package() if path is DEFAULT_PATH else path)
        self.assertEqual(caught.exception.code, code, str(caught.exception))
        self.assertTrue(caught.exception.message)
        self.assertIn(code, str(caught.exception))
        return caught.exception

    def test_valid_v1_metadata_is_read_only_and_uses_canonical_signature(self):
        package = write_package(self.package(), v1_entries())
        before = package.read_bytes()
        metadata = preview_package(package)
        self.assertEqual(package.read_bytes(), before)
        self.assertEqual(metadata["format"], "qeapp")
        self.assertEqual(metadata["format_version"], 1)
        self.assertEqual(metadata["id"], "org.test.sample")
        self.assertEqual(metadata["name"], "Sample QEAPP")
        self.assertEqual(metadata["version"], "1.2.3")
        self.assertEqual(metadata["min_os"], "0.1.0")
        self.assertEqual(metadata["entry"], "src/main.qe")
        self.assertEqual(metadata["runtime"], "script")
        self.assertFalse(metadata["native"])
        self.assertIsNone(metadata["abi_version"])
        self.assertEqual(metadata["architecture"], ["x86_64"])
        self.assertEqual(metadata["permissions"], ["ui.display", "storage.read"])
        self.assertEqual(metadata["dependencies"], ["org.test.library"])
        self.assertTrue(metadata["has_icon"])
        self.assertEqual(metadata["icon_size"], 16384)
        self.assertEqual(metadata["package_path"], str(package.absolute()))
        self.assertEqual(metadata["package_size"], len(before))
        self.assertEqual(metadata["package_sha256"], hashlib.sha256(before).hexdigest())
        self.assertTrue(metadata["signature_verified"])
        self.assertEqual(metadata["signature_scheme"], "development-hmac-sha256")
        self.assertFalse(metadata["publisher_authenticated"])
        self.assertNotIn(qeaf.DEV_KEY.decode("ascii"), json.dumps(metadata))

        with zipfile.ZipFile(package) as archive:
            entries = [(name, archive.read(name)) for name in archive.namelist()
                       if name != "SIGNATURE"]
        digest = hmac.new(qeaf.DEV_KEY, qeaf.SIG_HEADER, hashlib.sha256)
        for name, data in sorted(entries):
            digest.update(name.encode("utf-8") + b"\0" + struct.pack("<I", len(data)) + data)
        with zipfile.ZipFile(package) as archive:
            self.assertEqual(archive.read("SIGNATURE"), digest.hexdigest().encode("ascii"))

    def test_v1_runtime_script_and_missing_icon_are_supported(self):
        package = write_package(self.package(),
                                v1_entries(v1_manifest(runtime="script"), icon=None))
        metadata = preview_package(package)
        self.assertEqual(metadata["runtime"], "script")
        self.assertFalse(metadata["has_icon"])
        self.assertEqual(metadata["icon_size"], 0)

    def test_valid_native_v2_preserves_each_declared_architecture_and_abi(self):
        for architecture in ("arm64", "x86_64"):
            for abi in (2, 3, 4, 5):
                with self.subTest(architecture=architecture, abi=abi):
                    manifest = v2_manifest(
                        entry="native/%s/app.qen" % architecture,
                        architecture=[architecture], abi_version=abi)
                    module = qen.build_module(
                        b"\xc3", exports=(("create", 0),), arch=architecture,
                        min_api=abi, abi_version=abi)
                    package = write_package(self.package(), [
                        manifest_entry(manifest), (manifest["entry"], module)])
                    metadata = preview_package(package)
                    self.assertTrue(metadata["native"])
                    self.assertEqual(metadata["runtime"], "native")
                    self.assertEqual(metadata["abi_version"], abi)
                    self.assertEqual(metadata["architecture"], [architecture])
                    self.assertFalse(metadata["has_icon"])

    def test_valid_native_stub_with_matching_min_api_is_accepted(self):
        manifest = v2_manifest()
        module = qen.build_stub("arm64", min_api=5, abi_version=5)
        package = write_package(self.package(), [
            manifest_entry(manifest), (manifest["entry"], module)])
        self.assertEqual(preview_package(package)["abi_version"], 5)

    def test_unsigned_tampered_and_malformed_signatures_are_rejected(self):
        unsigned = write_package(self.package("unsigned.qeapp"), v1_entries(), sign=False)
        self.assert_error("SIGNATURE_MISSING", unsigned)
        package = write_package(self.package(), v1_entries())
        rewrite_entry(package, "src/main.qe", SCRIPT + b"# changed\n")
        self.assert_error("SIGNATURE_INVALID", package)
        for signature in (b"", b"A" * 64, b"0" * 63, b"0" * 64 + b"\n"):
            with self.subTest(signature=signature):
                broken = write_package(self.package(), v1_entries())
                rewrite_entry(broken, "SIGNATURE", signature)
                self.assert_error("SIGNATURE_INVALID", broken)

    def test_deflated_and_encrypted_entries_are_rejected(self):
        deflated = write_package(self.package("deflated.qeapp"), v1_entries(),
                                 compression=zipfile.ZIP_DEFLATED)
        self.assert_error("COMPRESSION_UNSUPPORTED", deflated)
        encrypted = write_package(self.package("encrypted.qeapp"), v1_entries())
        raw = bytearray(encrypted.read_bytes())
        for signature, flag_offset in ((b"PK\x03\x04", 6), (b"PK\x01\x02", 8)):
            offset = 0
            while True:
                offset = raw.find(signature, offset)
                if offset < 0:
                    break
                raw[offset + flag_offset] |= 0x01
                offset += len(signature)
        encrypted.write_bytes(raw)
        self.assert_error("ENCRYPTED_ENTRY", encrypted)

    def test_duplicate_and_unsafe_paths_are_rejected(self):
        duplicate = write_package(self.package("duplicate.qeapp"),
                                  v1_entries() + [("src/main.qe", SCRIPT)])
        self.assert_error("DUPLICATE_ENTRY", duplicate)
        unsafe_names = (
            "/escape.qe", "../escape.qe", "a/../escape.qe", "./escape.qe",
            "a/./escape.qe", "a//escape.qe", "C:/escape.qe",
            "assets/:bad", "dir/",
        )
        for name in unsafe_names:
            with self.subTest(name=name):
                package = write_package(self.package(), v1_entries() + [(name, b"bad")])
                self.assert_error("UNSAFE_ENTRY_PATH", package)
        # ZipInfo normalizes backslashes on Windows, so patch equal-length raw
        # local and central names to exercise the actual archive parser.
        package = write_package(self.package(), v1_entries() + [("aaaaaaaaaaa", b"bad")])
        raw = bytearray(package.read_bytes())
        self._replace_entry_name(raw, b"aaaaaaaaaaa", b"a\\escape.qe")
        package.write_bytes(raw)
        self.assert_error("UNSAFE_ENTRY_PATH", package)
        for name in ("", "bad\0name", "a/\0/b"):
            self.assertFalse(qeapp_package._safe_entry_name(name), repr(name))

    @staticmethod
    def _replace_entry_name(raw, old, new):
        if len(old) != len(new):
            raise ValueError("raw ZIP name replacement must preserve length")
        for signature, name_offset, length_offset in (
                (b"PK\x03\x04", 30, 26), (b"PK\x01\x02", 46, 28)):
            offset = 0
            while True:
                offset = raw.find(signature, offset)
                if offset < 0:
                    break
                length = struct.unpack_from("<H", raw, offset + length_offset)[0]
                start = offset + name_offset
                if raw[start:start + length] == old:
                    raw[start:start + length] = new
                offset += 4

    def test_symbolic_link_entries_are_rejected(self):
        package = self.package()
        write_package(package, v1_entries())
        with zipfile.ZipFile(package, "a") as archive:
            info = zipfile.ZipInfo("assets/link")
            info.create_system = 3
            info.external_attr = 0o120777 << 16
            archive.writestr(info, b"target")
        with zipfile.ZipFile(package) as archive:
            entries = [(name, archive.read(name)) for name in archive.namelist()]
        write_package(package, entries)
        # Reapply the symlink mode in the central directory after the rewrite.
        raw = bytearray(package.read_bytes())
        self._set_entry_mode(raw, "assets/link", 0o120777)
        package.write_bytes(raw)
        self.assert_error("UNSAFE_ENTRY_TYPE", package)

    @staticmethod
    def _set_entry_mode(raw, name, mode):
        encoded = name.encode("utf-8")
        offset = 0
        while True:
            offset = raw.find(b"PK\x01\x02", offset)
            if offset < 0:
                break
            length = struct.unpack_from("<H", raw, offset + 28)[0]
            start = offset + 46
            if raw[start:start + length] == encoded:
                struct.pack_into("<I", raw, offset + 38, mode << 16)
            offset += 4

    def test_package_entry_and_size_limits_are_enforced(self):
        at_limit = v1_entries() + [("assets/extra-%d.txt" % index, b"x")
                                   for index in range(28)]
        package = write_package(self.package("at-limit.qeapp"), at_limit)
        with zipfile.ZipFile(package) as archive:
            self.assertEqual(len(archive.infolist()), 32)
        self.assertTrue(preview_package(package)["signature_verified"])
        too_many = v1_entries() + [("assets/extra-%d.txt" % index, b"x")
                                   for index in range(29)]
        package = write_package(self.package("many.qeapp"), too_many)
        with zipfile.ZipFile(package) as archive:
            self.assertEqual(len(archive.infolist()), 33)
        self.assert_error("TOO_MANY_ENTRIES", package)
        oversized = write_package(self.package("large.qeapp"),
                                  v1_entries() + [("assets/large.bin", b"x" * (512 * 1024))])
        self.assertGreater(oversized.stat().st_size, 512 * 1024)
        self.assert_error("PACKAGE_TOO_LARGE", oversized)

    def test_missing_required_entries_and_invalid_zip_are_rejected(self):
        missing_manifest = write_package(self.package("no-manifest.qeapp"),
                                         v1_entries()[1:])
        self.assert_error("MANIFEST_MISSING", missing_manifest)
        missing_signature = write_package(self.package("no-signature.qeapp"),
                                          v1_entries(), sign=False)
        self.assert_error("SIGNATURE_MISSING", missing_signature)
        duplicate_signature = write_package(
            self.package("two-signatures.qeapp"),
            signed(v1_entries()) + [("SIGNATURE", b"0" * 64)], sign=False)
        self.assert_error("DUPLICATE_ENTRY", duplicate_signature)
        invalid = self.package("invalid.qeapp")
        invalid.write_bytes(b"not a ZIP file")
        self.assert_error("INVALID_ZIP", invalid)
        trailing = write_package(self.package("trailing.qeapp"), v1_entries())
        trailing.write_bytes(trailing.read_bytes() + b"X")
        self.assert_error("INVALID_ZIP", trailing)

    def test_manifest_json_must_be_complete_and_unambiguous(self):
        valid = manifest_entry(v1_manifest())[1]
        raw_manifests = (
            b"{", b"\xff", b"[]",
            b'{"format":"qeapp",' + valid[1:],
            valid[:-1] + b',"extra":NaN}',
        )
        for raw in raw_manifests:
            with self.subTest(raw=raw[:40]):
                package = write_package(self.package(), [
                    ("manifest.json", raw), ("src/main.qe", SCRIPT)])
                self.assert_error("MANIFEST_INVALID", package)

    def test_manifest_types_values_permissions_and_dependencies_are_strict(self):
        cases = (
            ({"format": "apk"}, "format"),
            ({"format_version": 0}, "format_version"),
            ({"format_version": "1"}, "format_version"),
            ({"format_version": True}, "format_version"),
            ({"format_version": 1.0}, "format_version"),
            ({"id": "Hello"}, "id"),
            ({"id": 7}, "id must be a string"),
            ({"name": ""}, "name"),
            ({"name": "bad\nname"}, "name"),
            ({"name": 7}, "name must be a string"),
            ({"version": "1.0"}, "version"),
            ({"version": 1}, "version must be a string"),
            ({"min_os": "latest"}, "min_os"),
            ({"architecture": []}, "architecture"),
            ({"architecture": ["mips"]}, "architecture"),
            ({"architecture": "arm64"}, "architecture"),
            ({"architecture": ["arm64", "arm64"]}, "duplicates"),
            ({"permissions": ["root.all"]}, "permissions"),
            ({"permissions": [1]}, "permissions"),
            ({"permissions": "ui.display"}, "permissions"),
            ({"dependencies": {}}, "dependencies"),
            ({"dependencies": [1]}, "dependencies"),
            ({"dependencies": [""]}, "dependencies"),
            ({"entry": "../main.qe"}, "entry"),
            ({"entry": "src//main.qe"}, "entry"),
            ({"entry": "src/main.qen"}, "entry"),
            ({"entry": 1}, "entry must be a string"),
            ({"runtime": "native"}, "runtime"),
            ({"runtime": None}, "runtime"),
            ({"icon": 1}, "icon"),
        )
        for overrides, message in cases:
            with self.subTest(overrides=overrides):
                manifest = v1_manifest(**overrides)
                # Keep archive paths valid so malformed manifest entry values
                # are rejected by manifest validation rather than ZIP policy.
                package = write_package(self.package(), [
                    manifest_entry(manifest), ("src/main.qe", SCRIPT),
                    ("assets/icon.rgba", ICON)])
                error = self.assert_error("MANIFEST_INVALID", package)
                self.assertIn(message, error.message)
        missing = v1_manifest()
        del missing["min_os"]
        package = write_package(self.package(), v1_entries(missing))
        self.assert_error("MANIFEST_INVALID", package)

    def test_native_manifest_runtime_abi_and_architecture_are_strict(self):
        module = qen.build_stub("arm64", min_api=5, abi_version=5)
        cases = (
            ({"runtime": "script"}, "runtime"),
            ({"abi_version": 1}, "abi_version"),
            ({"abi_version": 6}, "abi_version"),
            ({"abi_version": True}, "abi_version"),
            ({"abi_version": "5"}, "abi_version"),
            ({"architecture": ["arm64", "x86_64"]}, "exactly one"),
            ({"entry": "native/arm64/app.qe"}, "entry"),
        )
        for overrides, message in cases:
            with self.subTest(overrides=overrides):
                manifest = v2_manifest(**overrides)
                package = write_package(self.package(), [
                    manifest_entry(manifest), (manifest["entry"], module)])
                error = self.assert_error("MANIFEST_INVALID", package)
                self.assertIn(message, error.message)

    def test_missing_entry_and_bad_scripts_are_rejected(self):
        manifest = v1_manifest()
        package = write_package(self.package(), [manifest_entry(manifest)])
        self.assert_error("ENTRY_MISSING", package)
        scripts = (
            b"frobnicate x\n", b"app 123bad\n", b'print "unterminated\n',
            b'print "ok" trailing\n', b"print \xff\n",
        )
        for script in scripts:
            with self.subTest(script=script):
                package = write_package(self.package(), v1_entries(script=script))
                self.assert_error("SCRIPT_INVALID", package)

    def test_native_structure_architecture_abi_min_api_imports_and_hash_are_checked(self):
        good = qen.build_stub("arm64", min_api=5, abi_version=5)
        bad_hash = bytearray(good)
        bad_hash[49] ^= 0xFF
        wrong_import = qen.build_module(
            b"\xc3", exports=(("create", 0),), arch="arm64", min_api=5,
            abi_version=5, imports=(qen.IMPORT_API_V4,))
        cases = (
            (v2_manifest(), b"NOT-QEN", "module shorter"),
            (v2_manifest(), qen.build_stub("x86_64", min_api=5, abi_version=5),
             "architecture mismatch"),
            (v2_manifest(), qen.build_stub("arm64", min_api=4, abi_version=4),
             "abi_version 4 does not match"),
            (v2_manifest(), qen.build_stub("arm64", min_api=4, abi_version=5),
             "min_api 4 does not match"),
            (v2_manifest(), wrong_import, "must import qeaf_app_api_v5"),
            (v2_manifest(), bytes(bad_hash), "payload hash mismatch"),
        )
        for manifest, module, message in cases:
            with self.subTest(message=message):
                package = write_package(self.package(), [
                    manifest_entry(manifest), (manifest["entry"], module)])
                error = self.assert_error("MODULE_INVALID", package)
                self.assertIn(message, error.message)

    def test_optional_rgba_icon_has_exact_size(self):
        for size in (0, 16383, 16385):
            with self.subTest(size=size):
                package = write_package(self.package(),
                                        v1_entries(icon=b"\0" * size))
                self.assert_error("ICON_INVALID", package)

    def test_declared_icon_is_canonical_and_must_exist(self):
        package = write_package(self.package(), v1_entries(
            v1_manifest(icon="assets/custom.rgba")))
        error = self.assert_error("MANIFEST_INVALID", package)
        self.assertIn("icon must be assets/icon.rgba", error.message)
        package = write_package(self.package(), v1_entries(
            v1_manifest(icon="assets/icon.rgba"), icon=None))
        self.assert_error("ICON_MISSING", package)

    def test_native_stub_defaults_min_api_to_its_abi(self):
        for abi in (2, 3, 4, 5):
            with self.subTest(abi=abi):
                module = qen.build_stub("arm64", abi_version=abi)
                self.assertEqual(struct.unpack_from("<H", module, 53)[0], abi)

    def test_missing_non_regular_and_unreadable_paths_have_structured_errors(self):
        self.assert_error("PACKAGE_NOT_FOUND", self.package("missing.qeapp"))
        self.assert_error("PACKAGE_NOT_REGULAR", self.directory)
        self.assert_error("INVALID_PATH", None)
        package = write_package(self.package(), v1_entries())
        with mock.patch.object(Path, "open", side_effect=PermissionError("access denied")):
            error = self.assert_error("PACKAGE_READ_ERROR", package)
        self.assertIn("access denied", error.message)

    def test_legacy_validation_still_accepts_unsigned_deflated_packages(self):
        package = write_package(self.package(), v1_entries(), sign=False,
                                compression=zipfile.ZIP_DEFLATED)
        self.assertEqual(qeaf.validate_package(package), [])
        self.assert_error("COMPRESSION_UNSUPPORTED", package)

    def test_cli_preview_success_is_deterministic_and_failure_is_concise(self):
        package = write_package(self.package(), v1_entries())
        command = [sys.executable, str(ROOT / "sdk" / "cli" / "qeaf.py"),
                   "preview", str(package)]
        first = subprocess.run(command, cwd=self.directory, capture_output=True,
                               text=True, encoding="utf-8", timeout=30)
        second = subprocess.run(command, cwd=self.directory, capture_output=True,
                                text=True, encoding="utf-8", timeout=30)
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        self.assertEqual(first.stderr, "")
        self.assertEqual(first.stdout, second.stdout)
        metadata = json.loads(first.stdout)
        self.assertEqual(metadata["id"], "org.test.sample")
        self.assertEqual(metadata["architecture"], ["x86_64"])
        self.assertTrue(metadata["signature_verified"])

        unsigned = write_package(self.package("unsigned.qeapp"), v1_entries(), sign=False)
        failed = subprocess.run(
            [sys.executable, str(ROOT / "sdk" / "cli" / "qeaf.py"), "preview", str(unsigned)],
            cwd=self.directory, capture_output=True, text=True, encoding="utf-8", timeout=30)
        self.assertEqual(failed.returncode, 1)
        self.assertEqual(failed.stdout, "")
        self.assertIn("ERROR [SIGNATURE_MISSING]:", failed.stderr)
        self.assertNotIn("Traceback", failed.stderr)

    def test_sdk_and_cli_work_from_a_copied_layout(self):
        copied_root = self.directory / "copied"
        copied_sdk = copied_root / "sdk"
        for relative in ("qeapp/__init__.py", "qeapp/package.py",
                         "compiler/qen.py", "cli/qeaf.py"):
            destination = copied_sdk / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / "sdk" / relative, destination)
        package = write_package(self.package(), [
            manifest_entry(v2_manifest()),
            ("native/arm64/app.qen", qen.build_stub("arm64", min_api=5, abi_version=5))])
        unrelated = self.directory / "unrelated"
        unrelated.mkdir()
        code = (
            "import json,sys; sys.path.insert(0,sys.argv[1]); "
            "from sdk.qeapp import preview_package; "
            "print(json.dumps(preview_package(sys.argv[2]),sort_keys=True))"
        )
        result = subprocess.run(
            [sys.executable, "-c", code, str(copied_root), str(package)],
            cwd=unrelated, capture_output=True, text=True, encoding="utf-8", timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout)["runtime"], "native")
        cli = subprocess.run(
            [sys.executable, str(copied_sdk / "cli" / "qeaf.py"), "preview", str(package)],
            cwd=unrelated, capture_output=True, text=True, encoding="utf-8", timeout=30)
        self.assertEqual(cli.returncode, 0, cli.stdout + cli.stderr)
        self.assertEqual(json.loads(cli.stdout)["id"], "org.test.native")
        self.assertEqual(list(unrelated.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
