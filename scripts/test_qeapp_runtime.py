#!/usr/bin/env python3
"""Execute the real QEAPP runtime install path on the host.

Builds a signed package from the SDK template and a tampered copy, compiles
platform/violet/ui/vqeaf_qeapp.c with __linux__ forced (the SHA-256/HMAC, ZIP,
manifest and icon parsing code that the violet image builds) together with
tests/vqeaf_qeapp_runtime.c, and runs both cases.
"""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zipfile

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_phone_ui as r  # noqa: E402

ROOT = r.ROOT
OUT = os.path.join(ROOT, "build", "qeapp-runtime")
TEST = os.path.join(ROOT, "tests", "vqeaf_qeapp_runtime.c")
RUNTIME = os.path.join(ROOT, "platform", "violet", "ui", "vqeaf_qeapp.c")
NATIVE = os.path.join(ROOT, "platform", "violet", "ui", "vqeaf_qeapp_native.c")
PACKAGE_API = os.path.join(ROOT, "platform", "violet", "qeapp_package.c")
APPSTORE = os.path.join(ROOT, "platform", "violet", "appstore.c")
# install_buffer persists through the p77 appstore, which the host does not
# have; the simulator stubs report honest "persistence unavailable" so the
# real parse/verify/commit path still links and runs on the desktop.
STUBS = os.path.join(ROOT, "framework", "simulator", "qeaf_sim_stubs.c")
EXE = os.path.join(OUT, "vqeaf-qeapp-runtime.exe")
DURABLE_EXE = os.path.join(OUT, "vqeaf-qeapp-durable")
STORE = os.path.join(OUT, "appstore-test.bin")


def wsl_path(path):
    if os.name != "nt":
        return path
    drive, tail = os.path.splitdrive(os.path.abspath(path))
    return "/mnt/%s%s" % (drive[0].lower(), tail.replace("\\", "/"))


def build_packages():
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "qeaf", os.path.join(ROOT, "sdk", "cli", "qeaf.py"))
    qeaf = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(qeaf)

    import shutil
    import tempfile
    os.makedirs(OUT, exist_ok=True)
    good = os.path.join(OUT, "hello.qeapp")
    with tempfile.TemporaryDirectory(prefix="qeapp-runtime-") as temp:
        project = Path(temp) / "hello-qeapp"
        shutil.copytree(Path(ROOT) / "sdk" / "templates" / "hello-qeapp", project,
                        ignore=shutil.ignore_patterns("build", "__pycache__"))
        manifest_path = project / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["permissions"] = ["ui.display"]
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False),
                                 encoding="utf-8")
        package = qeaf.load_compiler().compile_project(project, "release", qeaf)
        shutil.copyfile(package, good)

    alternate = os.path.join(OUT, "hello-alternate.qeapp")
    with zipfile.ZipFile(good) as archive:
        entry = json.loads(archive.read("manifest.json"))["entry"]
        entries = [(n, archive.read(n)) for n in archive.namelist()]
    alternate_entries = []
    for name, data in entries:
        if name == "manifest.json":
            manifest = json.loads(data)
            manifest["id"] = "org.vqeaf.alternate"
            manifest["name"] = "Alternate QEAPP"
            data = json.dumps(manifest).encode("utf-8")
        alternate_entries.append((name, data))
    with zipfile.ZipFile(alternate, "w", zipfile.ZIP_STORED) as archive:
        for name, data in alternate_entries:
            archive.writestr(name, data)
    qeaf.sign_package(alternate)

    tampered = os.path.join(OUT, "hello-tampered.qeapp")
    with zipfile.ZipFile(tampered, "w", zipfile.ZIP_STORED) as archive:
        for name, data in entries:
            if name == entry:
                data = data + b"\n# tampered after signing\n"
            archive.writestr(name, data)
    boundary = os.path.join(OUT, "hello-print-limit.qeapp")
    with zipfile.ZipFile(boundary, "w", zipfile.ZIP_STORED) as archive:
        for name, data in entries:
            if name == entry:
                data = b'print "' + b"A" * 510 + b'"\nprint "B"\n'
            archive.writestr(name, data)
    qeaf.sign_package(boundary)

    legacy_icon = os.path.join(OUT, "hello-legacy-icon.qeapp")
    with zipfile.ZipFile(legacy_icon, "w", zipfile.ZIP_STORED) as archive:
        for name, data in entries:
            if name == "assets/icon.rgba":
                data = bytearray(data)
                edge = (32 * 64 + 32) * 4
                data[edge:edge + 3] = b"\0\0\0"
                data[edge + 3] = 128
                data = bytes(data)
            archive.writestr(name, data)
    qeaf.sign_package(legacy_icon)

    spec = importlib.util.spec_from_file_location(
        "qen", os.path.join(ROOT, "sdk", "compiler", "qen.py"))
    qen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(qen)
    native_manifest = {
        "format": "qeapp", "format_version": 2, "abi_version": 5,
        "runtime": "native", "id": "org.vqeaf.hello-native",
        "name": "Hello Native", "version": "1.0.0", "min_os": "0.3.0",
        "entry": "native/arm64/app.qen", "icon": "assets/icon.rgba",
        "architecture": ["arm64"], "permissions": ["ui.display"],
        "dependencies": [],
    }
    native_good = os.path.join(OUT, "hello-native.qeapp")
    native_bad = os.path.join(OUT, "hello-native-bad.qeapp")
    native_abi_mismatch = os.path.join(OUT, "hello-native-abi-mismatch.qeapp")
    native_min_api = os.path.join(OUT, "hello-native-min-api.qeapp")
    native_import_mismatch = os.path.join(OUT, "hello-native-import-mismatch.qeapp")
    native_multi_arch = os.path.join(OUT, "hello-native-multi-arch.qeapp")

    def write_native(path, module, manifest=None):
        with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
            archive.writestr("manifest.json", json.dumps(
                native_manifest if manifest is None else manifest))
            archive.writestr(native_manifest["entry"], module)
            archive.writestr("assets/icon.rgba", b"\0" * (64 * 64 * 4))
        qeaf.sign_package(path)

    stub = qen.build_stub("arm64", min_api=5, abi_version=5)
    write_native(native_good, stub)
    tampered_module = bytearray(stub)
    tampered_module[49] ^= 0xFF  # payload_hash
    write_native(native_bad, bytes(tampered_module))
    write_native(native_abi_mismatch,
                 qen.build_stub("arm64", min_api=4, abi_version=4))
    bad_min_api = bytearray(qen.build_module(b"\0", abi_version=5, min_api=5))
    bad_min_api[53:55] = struct.pack("<H", 4)
    write_native(native_min_api, bytes(bad_min_api))
    write_native(native_import_mismatch,
                 qen.build_module(b"\0", imports=(qen.IMPORT_API_V4,),
                                  abi_version=5, min_api=5))
    write_native(native_multi_arch, stub,
                 dict(native_manifest, architecture=["arm64", "x86_64"]))
    return (good, alternate, tampered, boundary, legacy_icon, native_good,
            native_bad, native_abi_mismatch, native_min_api,
            native_import_mismatch, native_multi_arch)


def compile_and_link():
    objects = []
    flags = r.CFLAGS + ["-D__linux__"] + r.INCLUDES
    for src in (RUNTIME, NATIVE, STUBS, PACKAGE_API, TEST):
        obj = os.path.join(OUT, os.path.basename(src).replace(".", "_") + ".o")
        cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu"] + flags + \
              ["-c", src, "-o", obj]
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        if proc.returncode != 0:
            print(proc.stderr or proc.stdout, file=sys.stderr)
            raise SystemExit("test_qeapp_runtime: compile FAILED %s" % src)
        objects.append(obj)
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu", "-o", EXE] + objects
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        raise SystemExit("test_qeapp_runtime: link FAILED")


def compile_durable():
    objects = []
    flags = r.CFLAGS + ["-D__linux__", "-D_GNU_SOURCE",
                        "-DQEAPP_REAL_APPSTORE"] + r.INCLUDES
    store_define = '-DAPPSTORE_PATH="%s"' % wsl_path(STORE).replace("\\", "/")
    for src in (RUNTIME, NATIVE, APPSTORE, PACKAGE_API, TEST):
        obj = os.path.join(OUT, "durable_" + os.path.basename(src).replace(".", "_") + ".o")
        cmd = [r.ZIG, "cc", "-target", "x86_64-linux-musl"] + flags + \
              [store_define, "-c", src, "-o", obj]
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        if proc.returncode != 0:
            print(proc.stderr or proc.stdout, file=sys.stderr)
            raise SystemExit("test_qeapp_runtime: durable compile FAILED %s" % src)
        objects.append(obj)
    cmd = [r.ZIG, "cc", "-target", "x86_64-linux-musl", "-o", DURABLE_EXE] + objects
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        raise SystemExit("test_qeapp_runtime: durable link FAILED")
    with open(STORE, "wb") as store:
        store.truncate(64 * 1024 * 1024)


def run(package, mode, extra=None, executable=EXE):
    command = [executable, package, mode]
    if extra is not None:
        command.append(extra)
    proc = subprocess.run(command, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print("== %s (%s) ==" % (os.path.basename(package), mode))
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    return proc.returncode


def durable_command(package, mode, *packages):
    if os.name != "nt":
        return [DURABLE_EXE, package, mode] + list(packages)
    return ["wsl.exe", "-e", wsl_path(DURABLE_EXE), wsl_path(package), mode] + \
        [wsl_path(path) for path in packages]


def main():
    if not os.path.isfile(r.ZIG):
        raise SystemExit("test_qeapp_runtime: zig not found at %s" % r.ZIG)
    (good, alternate, tampered, boundary, legacy_icon, native_good, native_bad,
     native_abi_mismatch, native_min_api, native_import_mismatch,
     native_multi_arch) = build_packages()
    compile_and_link()
    compile_durable()
    rc = run(good, "ok")
    rc |= run(tampered, "tampered")
    rc |= run(boundary, "limit")
    rc |= run(native_good, "native")
    rc |= run(native_good, "native-install")
    rc |= run(native_bad, "native-bad")
    rc |= run(native_abi_mismatch, "native-abi-mismatch")
    rc |= run(native_min_api, "native-min-api")
    rc |= run(native_import_mismatch, "native-import-mismatch")
    rc |= run(native_multi_arch, "native-multi-arch")
    rc |= run(good, "preview")
    rc |= run(tampered, "preview-tampered")
    rc |= run(good, "api")
    rc |= run(tampered, "api-tampered")
    rc |= run(good, "api-verified", alternate)
    rc |= run(legacy_icon, "icon-slots", alternate)
    write = subprocess.run(
        durable_command(good, "durable-write", good, alternate),
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    print("== durable catalog write ==")
    print(write.stdout, end="")
    if write.stderr:
        print(write.stderr, end="", file=sys.stderr)
    rc |= write.returncode
    if write.returncode == 0:
        restore = subprocess.run(
            durable_command(good, "durable-restore", good, alternate),
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        print("== durable catalog fresh-process restore ==")
        print(restore.stdout, end="")
        if restore.stderr:
            print(restore.stderr, end="", file=sys.stderr)
        rc |= restore.returncode
    if rc == 0:
        print("test_qeapp_runtime: ALL OK")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())