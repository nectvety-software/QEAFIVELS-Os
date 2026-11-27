import gzip
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "package_violet", ROOT / "scripts" / "package_violet.py")
PKG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PKG)


def static_elf():
    data = bytearray(64 + 56)
    data[:4] = b"\x7fELF"
    data[4] = 2
    data[5] = 1
    struct.pack_into("<H", data, 16, 2)
    struct.pack_into("<H", data, 18, PKG.EM_AARCH64)
    struct.pack_into("<Q", data, 32, 64)
    struct.pack_into("<H", data, 54, 56)
    struct.pack_into("<H", data, 56, 1)
    struct.pack_into("<I", data, 64, 1)
    return bytes(data)


def dynamic_elf():
    data = bytearray(static_elf())
    struct.pack_into("<I", data, 64, PKG.PT_INTERP)
    return bytes(data)


def kernel_payload(release=PKG.VIOLET_KERNEL_RELEASE, appended=b"dtb"):
    image = bytearray(4096)
    image[0x38:0x3C] = b"ARM\x64"
    start = 1024
    image[start:start + len(release)] = release
    return gzip.compress(bytes(image)) + appended


def cpio_entry(name, body):
    name_data = name.encode("ascii") + b"\0"
    fields = [0, 0o100755, 0, 0, 1, 0, len(body), 0, 0, 0, 0,
              len(name_data), 0]
    header = b"070701" + "".join("%08x" % value for value in fields).encode("ascii")
    if len(header) != 110:
        raise AssertionError("bad test cpio header")
    out = header + name_data
    out += b"\0" * (PKG.align_up(len(out), 4) - len(out))
    out += body
    out += b"\0" * (PKG.align_up(len(out), 4) - len(out))
    return out


def ramdisk_payload(init):
    trailer = cpio_entry("TRAILER!!!", b"")
    return gzip.compress(cpio_entry("init", init) + trailer)


def boot_image(kernel, ramdisk):
    page = 4096
    header = bytearray(page)
    header[:8] = b"ANDROID!"
    struct.pack_into("<I", header, 0x08, len(kernel))
    struct.pack_into("<I", header, 0x0C, 0x8000)
    struct.pack_into("<I", header, 0x10, len(ramdisk))
    struct.pack_into("<I", header, 0x14, 0x01000000)
    struct.pack_into("<I", header, 0x24, page)
    struct.pack_into("<I", header, 0x28, 1)
    header[0x40:0x40 + len(b"console=tty0")] = b"console=tty0"
    out = header + kernel
    out += b"\0" * (PKG.align_up(len(out), page) - len(out))
    out += ramdisk
    out += b"\0" * (PKG.align_up(len(out), page) - len(out))
    return out


class VioletPackageManifestTests(unittest.TestCase):
    def test_static_aarch64_elf_is_accepted(self):
        self.assertEqual(PKG.elf_identity(static_elf(), "init"),
                         {"machine": "aarch64", "static": True})

    def test_dynamic_elf_is_refused(self):
        with self.assertRaises(SystemExit):
            PKG.elf_identity(dynamic_elf(), "init")

    def test_elf_without_load_segment_is_refused(self):
        data = bytearray(static_elf())
        struct.pack_into("<I", data, 64, 0)
        with self.assertRaises(SystemExit):
            PKG.elf_identity(bytes(data), "init")

    def test_kernel_identity_checks_arm64_release_and_appended_dtb(self):
        info = PKG.kernel_identity(kernel_payload())
        self.assertEqual(info["release"], "4.14.215-azure-Pixel+")
        self.assertEqual(info["appended_data_size"], 3)
        self.assertEqual(info["appended_data_sha256"], PKG.sha256_bytes(b"dtb"))

    def test_wrong_kernel_release_is_refused(self):
        with self.assertRaises(SystemExit):
            PKG.kernel_identity(kernel_payload(b"4.14.214-test"))

    def test_ramdisk_init_is_extracted(self):
        init = static_elf()
        self.assertEqual(PKG.ramdisk_init(ramdisk_payload(init)), init)

    def test_missing_ramdisk_init_is_refused(self):
        payload = gzip.compress(cpio_entry("TRAILER!!!", b""))
        with self.assertRaises(SystemExit):
            PKG.ramdisk_init(payload)

    def test_missing_cpio_trailer_is_refused(self):
        payload = gzip.compress(cpio_entry("init", static_elf()))
        with self.assertRaises(SystemExit):
            PKG.ramdisk_init(payload)

    def test_manifest_records_verified_contract(self):
        kernel = kernel_payload()
        init = static_elf()
        ramdisk = ramdisk_payload(init)
        with tempfile.TemporaryDirectory() as td:
            image = Path(td) / "boot.img"
            reference = Path(td) / "reference.img"
            image.write_bytes(boot_image(kernel, ramdisk))
            reference.write_bytes(boot_image(kernel, ramdisk_payload(static_elf())))
            manifest = PKG.build_manifest(str(image), str(reference))
        self.assertTrue(manifest["kernel"]["matches_stock_reference"])
        self.assertTrue(manifest["init"]["static"])
        self.assertEqual(manifest["init"]["sha256"], PKG.sha256_bytes(init))
        self.assertEqual(manifest["verification"]["structural_gate"], "NOT_RUN")
        self.assertEqual(manifest["verification"]["rescue_gate"], "NOT_RUN")
        self.assertFalse(manifest["verification"]["physical_boot_tested"])

    def test_changed_kernel_is_refused(self):
        kernel = kernel_payload()
        changed = bytearray(kernel)
        changed[-1] ^= 1
        with tempfile.TemporaryDirectory() as td:
            image = Path(td) / "boot.img"
            reference = Path(td) / "reference.img"
            image.write_bytes(boot_image(bytes(changed), ramdisk_payload(static_elf())))
            reference.write_bytes(boot_image(kernel, ramdisk_payload(static_elf())))
            with self.assertRaises(SystemExit):
                PKG.build_manifest(str(image), str(reference))


if __name__ == "__main__":
    unittest.main()
