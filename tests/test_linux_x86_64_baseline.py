import gzip
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "build_linux_x86_64", ROOT / "scripts" / "build_linux_x86_64.py")
BASE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BASE)


def static_elf():
    data = bytearray(64 + 56)
    data[:4] = b"\x7fELF"
    data[4] = 2
    data[5] = 1
    struct.pack_into("<H", data, 16, 2)
    struct.pack_into("<H", data, 18, BASE.EM_X86_64)
    struct.pack_into("<H", data, 52, 64)
    struct.pack_into("<Q", data, 32, 64)
    struct.pack_into("<H", data, 54, 56)
    struct.pack_into("<H", data, 56, 1)
    struct.pack_into("<I", data, 64, BASE.PT_LOAD)
    return bytes(data)


def cpio_entry(name, body, mode=0o100755):
    name_data = name.encode("ascii") + b"\0"
    fields = [1, mode, 0, 0, 1, 0, len(body), 0, 0, 0, 0,
              len(name_data), 0]
    header = b"070701" + b"".join(
        ("%08x" % value).encode("ascii") for value in fields)
    out = bytearray(header + name_data)
    out.extend(b"\0" * (-len(out) % 4))
    out.extend(body)
    out.extend(b"\0" * (-len(out) % 4))
    return bytes(out)


def ramdisk(*entries):
    return gzip.compress(b"".join(entries), mtime=0)


def system_payload(init=None, services=None, runner=None, package=None):
    return ramdisk(
        cpio_entry("init", static_elf() if init is None else init),
        cpio_entry("qeaf-services", static_elf() if services is None else services),
        cpio_entry("qeapp-runner", static_elf() if runner is None else runner),
        cpio_entry("system/apps/puzzle.qeapp",
                   b"PK\x03\x04puzzle" if package is None else package, 0o100444),
        cpio_entry("TRAILER!!!", b"", 0),
    )


def system_artifacts():
    return {
        "init": static_elf(),
        "qeaf-services": static_elf(),
        "qeapp-runner": static_elf(),
        "system/apps/puzzle.qeapp": b"PK\x03\x04puzzle",
    }


class LinuxX86BaselineTests(unittest.TestCase):
    def test_source_pin_is_exact(self):
        self.assertEqual(BASE.LINUX_VERSION, "4.14.215")
        self.assertEqual(BASE.SOURCE_SHA256,
                         "9a844e290c61b39dd23df507ba1d9c2b2b4365e5993a978b19f1eb5610b926da")
        self.assertTrue(BASE.SOURCE_URL.endswith("/linux-4.14.215.tar.xz"))
        self.assertEqual(BASE.KERNEL.name, "bzImage")
        self.assertNotEqual(BASE.KERNEL.name, "KERNEL.EFI")

    def test_static_x86_64_elf_is_accepted(self):
        self.assertEqual(BASE.elf_identity(static_elf(), "init"),
                         {"machine": "x86_64", "static": True})

    def test_dynamic_elf_is_refused(self):
        data = bytearray(static_elf())
        struct.pack_into("<I", data, 64, BASE.PT_INTERP)
        with self.assertRaises(SystemExit):
            BASE.elf_identity(bytes(data), "init")

    def test_wrong_machine_is_refused(self):
        data = bytearray(static_elf())
        struct.pack_into("<H", data, 18, 183)
        with self.assertRaises(SystemExit):
            BASE.elf_identity(bytes(data), "init")

    def test_elf_without_load_segment_is_refused(self):
        data = bytearray(static_elf())
        struct.pack_into("<I", data, 64, 0)
        with self.assertRaises(SystemExit):
            BASE.elf_identity(bytes(data), "init")

    def test_elf_segment_outside_file_is_refused(self):
        data = bytearray(static_elf())
        struct.pack_into("<Q", data, 64 + 32, len(data) + 1)
        with self.assertRaises(SystemExit):
            BASE.elf_identity(bytes(data), "init")

    def test_initramfs_roundtrip_is_deterministic(self):
        artifacts = system_artifacts()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            paths = {
                "init": root / "vqeaf-init",
                "qeaf-services": root / "qeaf-services",
                "qeapp-runner": root / "qeapp-runner",
                "system/apps/puzzle.qeapp": root / "puzzle-qeapp.qeapp",
            }
            for name, path in paths.items():
                path.write_bytes(artifacts[name])
            plain = root / "initramfs.cpio"
            packed = root / "initramfs.cpio.gz"
            with mock.patch.multiple(
                    BASE, INIT=paths["init"], SERVICES=paths["qeaf-services"],
                    RUNNER=paths["qeapp-runner"],
                    PUZZLE=paths["system/apps/puzzle.qeapp"],
                    RAMDISK_PLAIN=plain, RAMDISK=packed):
                BASE.write_initramfs()
                first = packed.read_bytes()
                BASE.write_initramfs()
                self.assertEqual(first, packed.read_bytes())
            BASE.validate_ramdisk(first, artifacts)
            self.assertEqual(BASE.ramdisk_init(first), artifacts["init"])

    def test_system_initramfs_requires_exact_artifacts(self):
        artifacts = system_artifacts()
        BASE.validate_ramdisk(system_payload(), artifacts)

    def test_missing_service_is_refused(self):
        payload = ramdisk(
            cpio_entry("init", static_elf()),
            cpio_entry("qeapp-runner", static_elf()),
            cpio_entry("system/apps/puzzle.qeapp", b"PK\x03\x04puzzle", 0o100444),
            cpio_entry("TRAILER!!!", b"", 0),
        )
        with self.assertRaises(SystemExit):
            BASE.validate_ramdisk(payload, system_artifacts())

    def test_duplicate_service_is_refused(self):
        payload = ramdisk(
            cpio_entry("init", static_elf()),
            cpio_entry("qeaf-services", static_elf()),
            cpio_entry("qeaf-services", static_elf()),
            cpio_entry("qeapp-runner", static_elf()),
            cpio_entry("system/apps/puzzle.qeapp", b"PK\x03\x04puzzle", 0o100444),
            cpio_entry("TRAILER!!!", b"", 0),
        )
        with self.assertRaises(SystemExit):
            BASE.validate_ramdisk(payload, system_artifacts())

    def test_non_executable_service_is_refused(self):
        payload = ramdisk(
            cpio_entry("init", static_elf()),
            cpio_entry("qeaf-services", static_elf(), 0o100644),
            cpio_entry("qeapp-runner", static_elf()),
            cpio_entry("system/apps/puzzle.qeapp", b"PK\x03\x04puzzle", 0o100444),
            cpio_entry("TRAILER!!!", b"", 0),
        )
        with self.assertRaises(SystemExit):
            BASE.validate_ramdisk(payload, system_artifacts())

    def test_changed_system_package_is_refused(self):
        with self.assertRaises(SystemExit):
            BASE.validate_ramdisk(system_payload(package=b"PK\x03\x04changed"),
                                  system_artifacts())

    def test_boot_log_requires_ordered_qeapp_evidence(self):
        output = "\n".join(BASE.BOOT_MARKERS)
        BASE.validate_boot_log(output)
        with self.assertRaises(SystemExit):
            BASE.validate_boot_log(output.replace(BASE.QEAPP_RUN_MARKER, ""))
        markers = list(BASE.BOOT_MARKERS)
        markers[3], markers[4] = markers[4], markers[3]
        with self.assertRaises(SystemExit):
            BASE.validate_boot_log("\n".join(markers))
        with self.assertRaises(SystemExit):
            BASE.validate_boot_log(output + "\nQEAPP_RUN: FAIL\n")

    def test_missing_init_is_refused(self):
        payload = ramdisk(cpio_entry("TRAILER!!!", b"", 0))
        with self.assertRaises(SystemExit):
            BASE.ramdisk_init(payload)

    def test_missing_trailer_is_refused(self):
        payload = ramdisk(cpio_entry("init", static_elf()))
        with self.assertRaises(SystemExit):
            BASE.ramdisk_init(payload)

    def test_duplicate_init_is_refused(self):
        payload = ramdisk(cpio_entry("init", static_elf()),
                          cpio_entry("./init", static_elf()),
                          cpio_entry("TRAILER!!!", b"", 0))
        with self.assertRaises(SystemExit):
            BASE.ramdisk_init(payload)

    def test_non_executable_init_is_refused(self):
        payload = ramdisk(cpio_entry("init", static_elf(), 0o100644),
                          cpio_entry("TRAILER!!!", b"", 0))
        with self.assertRaises(SystemExit):
            BASE.ramdisk_init(payload)

    def test_trailing_gzip_data_is_refused(self):
        payload = ramdisk(cpio_entry("init", static_elf()),
                          cpio_entry("TRAILER!!!", b"", 0)) + b"junk"
        with self.assertRaises(SystemExit):
            BASE.ramdisk_init(payload)

    def test_config_fragment_enables_required_builtins(self):
        text = BASE.CONFIG_FRAGMENT.read_text(encoding="ascii")
        for symbol in ("CONFIG_BLK_DEV_INITRD=y", "CONFIG_RD_GZIP=y",
                       "CONFIG_BINFMT_ELF=y", "CONFIG_PROC_FS=y",
                       "CONFIG_SYSFS=y", "CONFIG_TMPFS=y", "CONFIG_DEVTMPFS=y",
                       "CONFIG_SERIAL_8250_CONSOLE=y", "CONFIG_SECCOMP=y",
                       "CONFIG_SECCOMP_FILTER=y", "CONFIG_MODULES=n",
                       "CONFIG_UNWINDER_FRAME_POINTER=y"):
            self.assertIn(symbol, text)
        for symbol in ("RETPOLINE", "STACK_VALIDATION", "UNWINDER_ORC"):
            self.assertIn("# CONFIG_%s is not set" % symbol, text)

    def test_incompatible_objtool_config_is_refused(self):
        required = (
            "CONFIG_64BIT", "CONFIG_X86_64", "CONFIG_BLK_DEV_INITRD", "CONFIG_RD_GZIP",
            "CONFIG_BINFMT_ELF", "CONFIG_PROC_FS", "CONFIG_SYSFS", "CONFIG_TMPFS",
            "CONFIG_DEVTMPFS", "CONFIG_DEVTMPFS_MOUNT", "CONFIG_SERIAL_8250",
            "CONFIG_SERIAL_8250_CONSOLE", "CONFIG_TTY", "CONFIG_PCI", "CONFIG_ACPI",
            "CONFIG_SECCOMP", "CONFIG_SECCOMP_FILTER",
        )
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "kernel.config"
            path.write_text(
                "".join("%s=y\n" % key for key in required) +
                'CONFIG_UNWINDER_FRAME_POINTER=y\nCONFIG_UNWINDER_ORC=y\n'
                '# CONFIG_MODULES is not set\nCONFIG_LOCALVERSION=""\n', encoding="ascii")
            with self.assertRaises(SystemExit):
                BASE.validate_config(path)

    def test_manifest_defaults_do_not_claim_qemu_pass(self):
        init = static_elf()
        required = (
            "CONFIG_64BIT", "CONFIG_X86_64", "CONFIG_BLK_DEV_INITRD", "CONFIG_RD_GZIP",
            "CONFIG_BINFMT_ELF", "CONFIG_PROC_FS", "CONFIG_SYSFS", "CONFIG_TMPFS",
            "CONFIG_DEVTMPFS", "CONFIG_DEVTMPFS_MOUNT", "CONFIG_SERIAL_8250",
            "CONFIG_SERIAL_8250_CONSOLE", "CONFIG_TTY", "CONFIG_PCI", "CONFIG_ACPI",
            "CONFIG_SECCOMP", "CONFIG_SECCOMP_FILTER",
        )
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            paths = {name: root / name for name in
                     ("bzImage", "kernel.config", "System.map", "kernel.release",
                      "vqeaf-init", "qeaf-services", "qeapp-runner",
                      "puzzle-qeapp.qeapp", "qeaf-initramfs.cpio.gz")}
            paths["bzImage"].write_bytes(b"kernel")
            paths["kernel.config"].write_text(
                "".join("CONFIG_%s=y\n" % key[7:] for key in required) +
                'CONFIG_UNWINDER_FRAME_POINTER=y\n'
                '# CONFIG_MODULES is not set\nCONFIG_LOCALVERSION=""\n', encoding="ascii")
            paths["System.map"].write_text("map\n", encoding="ascii")
            paths["kernel.release"].write_text("4.14.215\n", encoding="ascii")
            paths["vqeaf-init"].write_bytes(init)
            paths["qeaf-services"].write_bytes(static_elf())
            paths["qeapp-runner"].write_bytes(static_elf())
            paths["puzzle-qeapp.qeapp"].write_bytes(b"PK\x03\x04puzzle")
            paths["qeaf-initramfs.cpio.gz"].write_bytes(system_payload())
            with mock.patch.multiple(
                    BASE, KERNEL=paths["bzImage"], KERNEL_CONFIG=paths["kernel.config"],
                    SYSTEM_MAP=paths["System.map"], KERNEL_RELEASE=paths["kernel.release"],
                    INIT=paths["vqeaf-init"], SERVICES=paths["qeaf-services"],
                    RUNNER=paths["qeapp-runner"], PUZZLE=paths["puzzle-qeapp.qeapp"],
                    RAMDISK=paths["qeaf-initramfs.cpio.gz"]):
                manifest = BASE.build_manifest({"source_sha256": BASE.SOURCE_SHA256})
                with self.assertRaises(SystemExit):
                    BASE.build_manifest({"source_sha256": BASE.SOURCE_SHA256},
                                        qemu_boot="PASS")
        self.assertEqual(manifest["verification"]["qemu_boot"], "NOT_RUN")
        self.assertIsNone(manifest["verification"]["qemu_exit_code"])
        self.assertFalse(manifest["verification"]["physical_boot_tested"])


if __name__ == "__main__":
    unittest.main()
