#!/usr/bin/env python3
"""Build and boot the pinned QEAFIVELS Linux 4.14.215 x86_64/QEMU baseline."""
import argparse
import gzip
import hashlib
import json
import os
import shlex
import shutil
import stat
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "linux-x86_64"
KERNEL = BUILD / "bzImage"
KERNEL_CONFIG = BUILD / "kernel.config"
SYSTEM_MAP = BUILD / "System.map"
KERNEL_RELEASE = BUILD / "kernel.release"
INIT = BUILD / "vqeaf-init"
SERVICES = BUILD / "qeaf-services"
RUNNER = BUILD / "qeapp-runner"
PUZZLE = ROOT / "examples" / "puzzle-qeapp" / "build" / "releases" / "puzzle-qeapp.qeapp"
RAMDISK = BUILD / "qeaf-initramfs.cpio.gz"
RAMDISK_PLAIN = BUILD / "qeaf-initramfs.cpio"
MANIFEST = BUILD / "manifest.json"
BOOT_LOG = BUILD / "boot.log"
QEMU_STDERR = BUILD / "qemu-stderr.log"
KERNEL_LOG = BUILD / "kernel-build.log"
INIT_LOG = BUILD / "init-build.log"
SERVICES_LOG = BUILD / "services-build.log"
RUNNER_LOG = BUILD / "runner-build.log"
TOOLCHAIN = BUILD / "toolchain.json"

INIT_SOURCE = ROOT / "platform" / "linux-x86_64" / "init.c"
SERVICES_SOURCE = ROOT / "platform" / "linux-x86_64" / "qeaf_services.c"
RUNNER_SOURCE = ROOT / "platform" / "linux-x86_64" / "qeapp_runner.c"
CONFIG_FRAGMENT = ROOT / "config" / "linux-x86_64-qemu.fragment"
LINUX_VERSION = "4.14.215"
SOURCE_URL = "https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-4.14.215.tar.xz"
SOURCE_SHA256 = "9a844e290c61b39dd23df507ba1d9c2b2b4365e5993a978b19f1eb5610b926da"
WSL_DISTRO = os.environ.get("QEAF_WSL_DISTRO", "Ubuntu-24.04")
WSL_WORK = os.environ.get("QEAF_LINUX_WSL_WORK", "/tmp/qeafivels-linux-x86_64-4.14.215")
QEAFDROPOS = Path(os.environ.get("QEAFDROPOS", r"D:\QeafivelsOfficial\QeafdropOs"))
ZIG = Path(os.environ.get("ZIG", str(
    QEAFDROPOS / "out" / "toolchain" / "zig-windows-x86_64-0.13.0" / "zig.exe")))
QEMU = Path(os.environ.get("VQEAF_QEMU", shutil.which("qemu-system-x86_64") or
                          r"C:\Program Files\QEMU\qemu-system-x86_64.exe"))

EM_X86_64 = 62
PT_LOAD = 1
PT_INTERP = 3
READY_MARKER = "QEAF_LINUX_X86_64_INIT: READY pid=1 release=4.14.215"
FS_MARKER = "QEAF_LINUX_X86_64_FS: READY proc sysfs devtmpfs run tmp"
SERVICES_READY_MARKER = "QEAF_SERVICES: READY"
QEAPP_VERIFY_MARKER = "QEAPP_VERIFY: PASS arch=x86_64"
QEAPP_SANDBOX_MARKER = "QEAPP_SANDBOX: PASS uid=20000 gid=20000 no_new_privs seccomp"
QEAPP_RUN_MARKER = "QEAPP_RUN: PASS arch=x86_64"
SYSTEM_APP_MARKER = "QEAF_SYSTEM_APP: PASS runner-exit=0"
SERVICES_STOPPED_MARKER = "QEAF_SERVICES: STOPPED"
SYSTEM_PASS_MARKER = "QEAF_LINUX_X86_64_SYSTEM: PASS"
POWEROFF_MARKER = "QEAF_LINUX_X86_64_INIT: POWEROFF"
BOOT_MARKERS = (
    "Linux version " + LINUX_VERSION,
    FS_MARKER,
    READY_MARKER,
    SERVICES_READY_MARKER,
    QEAPP_VERIFY_MARKER,
    QEAPP_SANDBOX_MARKER,
    QEAPP_RUN_MARKER,
    SYSTEM_APP_MARKER,
    SERVICES_STOPPED_MARKER,
    SYSTEM_PASS_MARKER,
    POWEROFF_MARKER,
    "reboot: Power down",
)


def fail(message):
    sys.stderr.write("linux_x86_64: %s\n" % message)
    raise SystemExit(1)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def artifact(path):
    return {"path": path.name, "size": path.stat().st_size,
            "sha256": sha256_file(path)}


def wsl_path(path):
    resolved = Path(path).resolve()
    drive = resolved.drive.rstrip(":").lower()
    parts = resolved.parts[1:]
    return "/mnt/%s/%s" % (drive, "/".join(parts))


def run_logged(command, log_path, cwd=ROOT):
    environment = os.environ.copy()
    environment.setdefault("SOURCE_DATE_EPOCH", "1610478720")
    with open(log_path, "wb") as log:
        result = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                                env=environment)
    if result.returncode != 0:
        with open(log_path, "rb") as log:
            sys.stderr.buffer.write(log.read()[-12000:])
        fail("command failed with exit code %d; see %s" %
             (result.returncode, log_path))
    return result


def wsl_script(script, log_path=None):
    command = ["wsl.exe", "-d", WSL_DISTRO, "--", "sh", "-lc", script]
    if log_path is not None:
        return run_logged(command, log_path)
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors="replace")
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        fail("WSL command failed with exit code %d" % result.returncode)
    return result.stdout


def wsl_capture(arguments):
    return wsl_script(shlex.join(arguments)).strip()


def read_toolchain():
    if not ZIG.is_file():
        fail("Zig 0.13 is missing: %s" % ZIG)
    zig = subprocess.run([str(ZIG), "version"], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if zig.returncode != 0:
        fail("unable to query the Zig version")
    return {
        "wsl_distro": WSL_DISTRO,
        "gcc": wsl_capture(["gcc", "-dumpfullversion", "-dumpversion"]),
        "ld": wsl_capture(["ld", "--version"]).splitlines()[0],
        "make": wsl_capture(["make", "--version"]).splitlines()[0],
        "zig": zig.stdout.strip(),
        "architecture": wsl_capture(["uname", "-m"]),
    }


def build_kernel():
    if os.name != "nt":
        fail("the pinned kernel build entrypoint requires Windows with WSL")
    if not WSL_WORK.startswith("/tmp/qeafivels-linux-x86_64"):
        fail("QEAF_LINUX_WSL_WORK must stay under /tmp/qeafivels-linux-x86_64")
    runner = ROOT / "scripts" / "build_linux_x86_64_kernel.sh"
    command = [
        "wsl.exe", "-d", WSL_DISTRO, "--", "sh", wsl_path(runner),
        WSL_WORK, LINUX_VERSION, SOURCE_URL, SOURCE_SHA256,
        wsl_path(CONFIG_FRAGMENT), wsl_path(BUILD),
    ]
    run_logged(command, KERNEL_LOG)
    print("Linux %s bzImage built: %s" % (LINUX_VERSION, KERNEL))


def validate_config(path=None):
    if path is None:
        path = KERNEL_CONFIG
    values = {}
    for raw in Path(path).read_text(encoding="ascii").splitlines():
        if raw.startswith("CONFIG_") and "=" in raw:
            key, value = raw.split("=", 1)
            values[key] = value
        elif raw.startswith("# CONFIG_") and raw.endswith(" is not set"):
            values[raw[2:].split(" ", 1)[0]] = "n"
    required = (
        "CONFIG_64BIT", "CONFIG_X86_64", "CONFIG_BLK_DEV_INITRD", "CONFIG_RD_GZIP",
        "CONFIG_BINFMT_ELF", "CONFIG_PROC_FS", "CONFIG_SYSFS", "CONFIG_TMPFS",
        "CONFIG_DEVTMPFS", "CONFIG_DEVTMPFS_MOUNT", "CONFIG_SERIAL_8250",
        "CONFIG_SERIAL_8250_CONSOLE", "CONFIG_TTY", "CONFIG_PCI", "CONFIG_ACPI",
        "CONFIG_SECCOMP", "CONFIG_SECCOMP_FILTER",
    )
    missing = [key for key in required if values.get(key) != "y"]
    if missing:
        fail("kernel config is missing built-ins: " + ", ".join(missing))
    incompatible = [key for key in (
        "CONFIG_RETPOLINE", "CONFIG_STACK_VALIDATION", "CONFIG_UNWINDER_ORC",
    ) if values.get(key) == "y"]
    if incompatible:
        fail("kernel config enables incompatible Linux 4.14 objtool paths: " +
             ", ".join(incompatible))
    if values.get("CONFIG_UNWINDER_FRAME_POINTER") != "y":
        fail("kernel config must use the frame-pointer unwinder")
    if values.get("CONFIG_MODULES") != "n" or values.get("CONFIG_LOCALVERSION") != '""':
        fail("kernel config must be monolithic with an empty local version")
    return values


def build_init():
    if not ZIG.is_file():
        fail("Zig 0.13 is missing: %s" % ZIG)
    command = [
        str(ZIG), "cc", "-target", "x86_64-linux-musl", "-static", "-Os", "-s",
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-fno-ident",
        "-Wl,--build-id=none", "-o", str(INIT), str(INIT_SOURCE),
    ]
    run_logged(command, INIT_LOG)
    elf_identity(INIT.read_bytes(), "vqeaf-init")
    print("Static x86_64 musl /init built: %s" % INIT)


def build_system_binary(output, sources, log_path, label):
    command = [
        str(ZIG), "cc", "-target", "x86_64-linux-musl", "-static", "-Os", "-s",
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-fno-ident",
        "-Wl,--build-id=none", "-ffunction-sections", "-fdata-sections",
        "-Wl,--gc-sections", "-D_GNU_SOURCE", "-DLV_CONF_INCLUDE_SIMPLE",
        "-DLV_LVGL_H_INCLUDE_SIMPLE",
        "-Iplatform/violet", "-Iplatform/violet/channel", "-Iplatform/violet/ui",
        "-Iplatform/violet/ui/fonts", "-Iplatform/lvgl", "-Iplatform/lvgl/src",
        "-Isystem/ui/include", "-Iframework/application_api",
        "-Isystem/core/include",
    ] + [str(source) for source in sources] + ["-o", str(output)]
    run_logged(command, log_path)
    elf_identity(output.read_bytes(), label)
    print("Static x86_64 musl %s built: %s" % (label, output))


def build_userspace():
    build_init()
    build_system_binary(SERVICES, [
        SERVICES_SOURCE,
        ROOT / "system" / "core" / "service_manager.c",
        ROOT / "platform" / "violet" / "ui" / "vqeaf_qeapp.c",
        ROOT / "platform" / "violet" / "ui" / "vqeaf_qeapp_native.c",
    ], SERVICES_LOG, "qeaf-services")
    build_system_binary(RUNNER, [
        RUNNER_SOURCE,
        ROOT / "platform" / "violet" / "ui" / "vqeaf_qeapp.c",
        ROOT / "platform" / "violet" / "ui" / "vqeaf_qeapp_native.c",
        ROOT / "platform" / "violet" / "ui" / "vqeaf_qeapp_device.c",
        ROOT / "platform" / "violet" / "ui" / "vqeaf_qeapp_sandbox.c",
    ], RUNNER_LOG, "qeapp-runner")
    package = PUZZLE.read_bytes()
    if len(package) > 512 * 1024 or not package.startswith(b"PK\x03\x04"):
        fail("Puzzle system package is missing, too large or not a ZIP")


def elf_identity(data, label="init"):
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2:
        fail("%s is not a 64-bit ELF" % label)
    if data[5] != 1:
        fail("%s is not a little-endian ELF" % label)
    etype = struct.unpack_from("<H", data, 16)[0]
    if etype not in (2, 3):
        fail("%s is not an executable ELF" % label)
    machine = struct.unpack_from("<H", data, 18)[0]
    if machine != EM_X86_64:
        fail("%s is built for machine 0x%04x, not x86_64" % (label, machine))
    ehsize = struct.unpack_from("<H", data, 52)[0]
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize = struct.unpack_from("<H", data, 54)[0]
    phnum = struct.unpack_from("<H", data, 56)[0]
    if (ehsize < 64 or phentsize < 56 or phoff < 64 or phoff > len(data) or
            phnum == 0 or phnum > (len(data) - phoff) // phentsize):
        fail("%s has an invalid ELF program-header table" % label)
    has_load = False
    for index in range(phnum):
        header = phoff + index * phentsize
        ptype = struct.unpack_from("<I", data, header)[0]
        if ptype == PT_INTERP:
            fail("%s is dynamically linked; Linux /init must be static" % label)
        offset = struct.unpack_from("<Q", data, header + 8)[0]
        filesz = struct.unpack_from("<Q", data, header + 32)[0]
        if offset > len(data) or filesz > len(data) - offset:
            fail("%s has an ELF segment outside the file" % label)
        if ptype == PT_LOAD:
            has_load = True
    if not has_load:
        fail("%s has no ELF load segment" % label)
    return {"machine": "x86_64", "static": True}


def cpio_entry(name, mode, body, ino):
    if not name or name.startswith("/") or ".." in name.split("/"):
        fail("unsafe initramfs path: %r" % name)
    name_data = name.encode("ascii") + b"\0"
    fields = [
        ino, mode, 0, 0, 2 if stat.S_ISDIR(mode) else 1, 0, len(body),
        0, 0, 0, 0, len(name_data), 0,
    ]
    header = b"070701" + b"".join(("%08x" % value).encode("ascii") for value in fields)
    if len(header) != 110:
        fail("internal newc header size error")
    out = bytearray(header + name_data)
    out.extend(b"\0" * (-len(out) % 4))
    out.extend(body)
    out.extend(b"\0" * (-len(out) % 4))
    return bytes(out)


def system_artifact_data():
    package = PUZZLE.read_bytes()
    if len(package) > 512 * 1024 or not package.startswith(b"PK\x03\x04"):
        fail("Puzzle system package is missing, too large or not a ZIP")
    return {
        "init": INIT.read_bytes(),
        "qeaf-services": SERVICES.read_bytes(),
        "qeapp-runner": RUNNER.read_bytes(),
        "system/apps/puzzle.qeapp": package,
    }


def write_initramfs():
    artifacts = system_artifact_data()
    entries = [(name, 0o040755, b"") for name in
               ("dev", "etc", "proc", "run", "sys", "tmp", "system", "system/apps")]
    entries.extend((
        ("init", 0o100755, artifacts["init"]),
        ("qeaf-services", 0o100755, artifacts["qeaf-services"]),
        ("qeapp-runner", 0o100755, artifacts["qeapp-runner"]),
        ("system/apps/puzzle.qeapp", 0o100444, artifacts["system/apps/puzzle.qeapp"]),
    ))
    plain = bytearray()
    for ino, (name, mode, body) in enumerate(entries, 1):
        plain.extend(cpio_entry(name, mode, body, ino))
    plain.extend(cpio_entry("TRAILER!!!", 0o100000, b"", len(entries) + 1))
    RAMDISK_PLAIN.write_bytes(plain)
    RAMDISK.write_bytes(gzip.compress(bytes(plain), compresslevel=9, mtime=0))
    validate_ramdisk(RAMDISK.read_bytes(), artifacts)
    print("Deterministic newc initramfs written: %s (%d bytes)" %
          (RAMDISK, RAMDISK.stat().st_size))


def ramdisk_entries(payload):
    try:
        decoder = zlib.decompressobj(31)
        plain = decoder.decompress(payload) + decoder.flush()
    except zlib.error as exc:
        fail("initramfs is not gzip: %s" % exc)
    if not decoder.eof or decoder.unused_data:
        fail("initramfs has trailing or incomplete gzip data")
    offset = 0
    files = {}
    modes = {}
    trailer_seen = False
    while offset + 110 <= len(plain):
        magic = plain[offset:offset + 6]
        if magic not in (b"070701", b"070702"):
            fail("initramfs cpio magic is invalid at offset %d" % offset)

        def field(index):
            value = plain[offset + 6 + index * 8:offset + 14 + index * 8]
            try:
                return int(value, 16)
            except ValueError:
                fail("initramfs cpio has a non-hex field")

        mode = field(1)
        filesize = field(6)
        namesize = field(11)
        checksum = field(12)
        if namesize < 2 or namesize > 4096 or filesize > len(plain):
            fail("initramfs cpio has an invalid entry header")
        name_start = offset + 110
        body_start = offset + ((110 + namesize + 3) & ~3)
        body_end = body_start + filesize
        if body_start > len(plain) or body_end > len(plain):
            fail("initramfs cpio entry overruns the archive")
        if plain[name_start + namesize - 1] != 0:
            fail("initramfs cpio entry name is not terminated")
        try:
            name = plain[name_start:name_start + namesize - 1].decode("ascii", "strict")
        except UnicodeDecodeError:
            fail("initramfs cpio entry name is not ASCII")
        while name.startswith("./"):
            name = name[2:]
        if not name or name.startswith("/") or ".." in name.split("/"):
            fail("unsafe initramfs path: %r" % name)
        body = plain[body_start:body_end]
        if magic == b"070702" and checksum != (zlib.crc32(body) & 0xffffffff):
            fail("initramfs cpio checksum mismatch: %s" % name)
        if name == "TRAILER!!!":
            trailer_seen = True
            if any(plain[body_end:]):
                fail("initramfs has data after TRAILER!!!")
            break
        if name in modes:
            fail("initramfs contains a duplicate entry: %s" % name)
        modes[name] = mode
        if stat.S_ISREG(mode):
            files[name] = body
        elif not stat.S_ISDIR(mode):
            fail("initramfs entry is not a regular file or directory: %s" % name)
        offset = body_start + ((filesize + 3) & ~3)
    if not trailer_seen:
        fail("initramfs cpio has no TRAILER!!! entry")
    return files, modes


def ramdisk_init(payload):
    files, modes = ramdisk_entries(payload)
    if "init" not in files:
        fail("initramfs has no /init entry")
    if not stat.S_ISREG(modes["init"]) or not modes["init"] & 0o111:
        fail("initramfs /init is not an executable regular file")
    return files["init"]


def validate_ramdisk(payload, expected=None):
    if expected is None:
        expected = system_artifact_data()
    files, modes = ramdisk_entries(payload)
    if set(files) != set(expected):
        missing = sorted(set(expected) - set(files))
        unexpected = sorted(set(files) - set(expected))
        fail("initramfs file set mismatch: missing=%s unexpected=%s" %
             (missing, unexpected))
    for name, data in expected.items():
        if files[name] != data:
            fail("initramfs content mismatch: %s" % name)
        mode = modes[name]
        if name in ("init", "qeaf-services", "qeapp-runner"):
            if not stat.S_ISREG(mode) or not mode & 0o111:
                fail("initramfs executable is invalid: %s" % name)
        elif not stat.S_ISREG(mode):
            fail("initramfs package is not a regular file: %s" % name)


def load_toolchain():
    if not TOOLCHAIN.is_file():
        fail("toolchain record is missing; run the full build first")
    return json.loads(TOOLCHAIN.read_text(encoding="utf-8"))


def build_manifest(toolchain, qemu_boot="NOT_RUN", boot_log_sha256=None,
                   qemu_exit_code=None):
    for path in (KERNEL, KERNEL_CONFIG, SYSTEM_MAP, KERNEL_RELEASE, INIT,
                 SERVICES, RUNNER, PUZZLE, RAMDISK):
        if not path.is_file():
            fail("missing baseline artifact: %s" % path)
    validate_config()
    artifacts = system_artifact_data()
    init_identity = elf_identity(artifacts["init"], "vqeaf-init")
    services_identity = elf_identity(artifacts["qeaf-services"], "qeaf-services")
    runner_identity = elf_identity(artifacts["qeapp-runner"], "qeapp-runner")
    validate_ramdisk(RAMDISK.read_bytes(), artifacts)
    if KERNEL_RELEASE.read_text(encoding="ascii").strip() != LINUX_VERSION:
        fail("kernel.release does not match the pinned Linux version")
    if toolchain.get("source_sha256") != SOURCE_SHA256:
        fail("toolchain record does not prove the pinned source hash")
    if qemu_boot == "PASS" and (not boot_log_sha256 or qemu_exit_code != 0):
        fail("a PASS QEMU gate requires a boot-log hash and zero exit code")
    return {
        "format": "vqeaf-linux-x86_64-qemu/v1",
        "target": "qemu-system-x86_64",
        "kernel": {
            "version": LINUX_VERSION,
            "source": "upstream Linux",
            "source_url": SOURCE_URL,
            "source_sha256": SOURCE_SHA256,
            "artifact": artifact(KERNEL),
            "config": artifact(KERNEL_CONFIG),
            "system_map": artifact(SYSTEM_MAP),
        },
        "toolchain": toolchain,
        "init": dict(init_identity, **artifact(INIT)),
        "services": dict(services_identity, **artifact(SERVICES)),
        "qeapp_runner": dict(runner_identity, **artifact(RUNNER)),
        "system_app": {
            "name": "Puzzle",
            "architecture": "x86_64",
            "package": artifact(PUZZLE),
        },
        "initramfs": artifact(RAMDISK),
        "verification": {
            "source_hash": "PASS",
            "config": "PASS",
            "static_init": "PASS",
            "static_services": "PASS",
            "static_qeapp_runner": "PASS",
            "cpio": "PASS",
            "qemu_boot": qemu_boot,
            "qemu_exit_code": qemu_exit_code,
            "boot_log_sha256": boot_log_sha256,
            "physical_boot_tested": False,
        },
        "limitations": [
            "Headless QEMU baseline: package-store and qeapp-preflight services plus a one-shot Puzzle run; system UI, rescue, persistence and updates are not wired.",
            "QEAPP arena remains RWX; RLIMIT_CORE, RLIMIT_NOFILE and chdir failures are fail-closed, but namespaces, chroot, device cgroups and LSM policy are not implemented.",
            "The embedded development HMAC detects image-relative tampering but is not publisher authentication.",
            "No physical x86_64 hardware was tested.",
            "Linux 4.14.215 requires a separate security backport and CVE review before release.",
        ],
    }


def write_manifest(manifest):
    MANIFEST.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")


def validate_boot_log(output):
    missing = [marker for marker in BOOT_MARKERS if marker not in output]
    if missing:
        fail("boot log is missing marker(s): " + ", ".join(missing))
    positions = [output.index(marker) for marker in BOOT_MARKERS]
    if positions != sorted(positions):
        fail("boot log markers are out of order")
    forbidden = [marker for marker in (
        "Kernel panic", "No working init found", "Attempted to kill init",
        "QEAF_LINUX_X86_64_INIT: FAIL", "QEAF_LINUX_X86_64_SYSTEM: FAIL",
        "QEAF_SERVICES: FAIL", "QEAF_SERVICE: FAIL", "QEAPP_RUN: FAIL",
        "QEAF_SYSTEM_APP: FAIL",
    ) if marker in output]
    if forbidden:
        fail("boot log contains failure marker(s): " + ", ".join(forbidden))


def boot_qemu(timeout=45):
    if not QEMU.is_file():
        fail("qemu-system-x86_64 is missing: %s" % QEMU)
    BOOT_LOG.unlink(missing_ok=True)
    QEMU_STDERR.unlink(missing_ok=True)
    command = [
        str(QEMU), "-machine", "pc", "-m", "256M",
        "-kernel", str(KERNEL), "-initrd", str(RAMDISK),
        "-append", "console=ttyS0 rdinit=/init panic=-1 qeaf.poweroff_ms=1000",
        "-display", "none", "-serial", "file:" + BOOT_LOG.as_posix(),
        "-monitor", "none", "-nic", "none", "-no-reboot",
    ]
    with open(QEMU_STDERR, "wb") as errors:
        process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL,
                                   stderr=errors)
        deadline = time.monotonic() + timeout
        timed_out = process.poll() is None and time.monotonic() >= deadline
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.1)
        if process.poll() is None:
            timed_out = True
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
    output = BOOT_LOG.read_text(encoding="utf-8", errors="replace") if BOOT_LOG.exists() else ""
    if timed_out:
        fail("QEMU gate failed: emulator did not exit before the deadline")
    if process.returncode != 0:
        fail("QEMU gate failed: exit=%s" % process.returncode)
    validate_boot_log(output)
    return output, process.returncode


def boot_and_record(timeout=45):
    toolchain = load_toolchain()
    try:
        _output, code = boot_qemu(timeout)
        status = "PASS"
    except SystemExit:
        code = None
        status = "FAIL"
        log_hash = sha256_file(BOOT_LOG) if BOOT_LOG.is_file() else None
        write_manifest(build_manifest(toolchain, qemu_boot=status,
                                      boot_log_sha256=log_hash, qemu_exit_code=code))
        raise
    write_manifest(build_manifest(toolchain, qemu_boot=status,
                                  boot_log_sha256=sha256_file(BOOT_LOG),
                                  qemu_exit_code=code))
    print("QEMU PASS: Linux %s, QEAF services, sandboxed Puzzle lifecycle, clean poweroff" %
          LINUX_VERSION)


def verify_artifacts():
    if not MANIFEST.is_file():
        fail("manifest is missing")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("format") != "vqeaf-linux-x86_64-qemu/v1":
        fail("unsupported Linux baseline manifest format")
    checks = (
        (manifest["kernel"]["artifact"], KERNEL),
        (manifest["kernel"]["config"], KERNEL_CONFIG),
        (manifest["kernel"]["system_map"], SYSTEM_MAP),
        (manifest["init"], INIT),
        (manifest["services"], SERVICES),
        (manifest["qeapp_runner"], RUNNER),
        (manifest["system_app"]["package"], PUZZLE),
        (manifest["initramfs"], RAMDISK),
    )
    for record, path in checks:
        if record["sha256"] != sha256_file(path) or record["size"] != path.stat().st_size:
            fail("artifact hash mismatch: %s" % path)
    if manifest["kernel"]["source_sha256"] != SOURCE_SHA256:
        fail("manifest source hash is not pinned")
    if manifest["toolchain"].get("source_sha256") != SOURCE_SHA256:
        fail("toolchain source hash is not pinned")
    artifacts = system_artifact_data()
    for name, label in (("init", "vqeaf-init"),
                        ("qeaf-services", "qeaf-services"),
                        ("qeapp-runner", "qeapp-runner")):
        identity = elf_identity(artifacts[name], label)
        record = {"init": manifest["init"],
                  "qeaf-services": manifest["services"],
                  "qeapp-runner": manifest["qeapp_runner"]}[name]
        if record.get("machine") != identity["machine"] or record.get("static") is not True:
            fail("static ELF identity mismatch: %s" % name)
    validate_ramdisk(RAMDISK.read_bytes(), artifacts)
    if KERNEL_RELEASE.read_text(encoding="ascii").strip() != LINUX_VERSION:
        fail("kernel.release does not match the pinned Linux version")
    validate_config()
    verification = manifest["verification"]
    for key in ("source_hash", "config", "static_init", "static_services",
                "static_qeapp_runner", "cpio"):
        if verification.get(key) != "PASS":
            fail("manifest verification is not PASS: %s" % key)
    if verification["qemu_boot"] == "PASS":
        if not BOOT_LOG.is_file() or sha256_file(BOOT_LOG) != verification["boot_log_sha256"]:
            fail("PASS manifest does not match boot.log")
        output = BOOT_LOG.read_text(encoding="utf-8", errors="replace")
        validate_boot_log(output)
        if verification["qemu_exit_code"] != 0:
            fail("PASS manifest has a nonzero QEMU exit code")
    elif verification["qemu_boot"] not in ("NOT_RUN", "FAIL"):
        fail("unknown QEMU gate state")
    if verification["physical_boot_tested"] is not False:
        fail("physical boot must remain false for this QEMU baseline")
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-kernel", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--timeout", type=int, default=45)
    args = parser.parse_args(argv)
    BUILD.mkdir(parents=True, exist_ok=True)
    if args.verify_only:
        print(json.dumps(verify_artifacts(), indent=2, sort_keys=True))
        return 0
    if args.skip_kernel:
        for path in (KERNEL, KERNEL_CONFIG, SYSTEM_MAP, KERNEL_RELEASE, TOOLCHAIN):
            if not path.is_file():
                fail("--skip-kernel requires a complete earlier kernel build")
    else:
        build_kernel()
        toolchain = read_toolchain()
        toolchain["source_sha256"] = wsl_capture([
            "sha256sum", WSL_WORK + "/linux-" + LINUX_VERSION + ".tar.xz"
        ]).split()[0]
        if toolchain["source_sha256"] != SOURCE_SHA256:
            fail("downloaded source hash differs from the pin")
        TOOLCHAIN.write_text(json.dumps(toolchain, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")
    build_userspace()
    write_initramfs()
    write_manifest(build_manifest(load_toolchain()))
    boot_and_record(args.timeout)
    verify_artifacts()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
