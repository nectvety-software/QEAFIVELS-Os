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
# install_buffer persists through the p77 appstore, which the host does not
# have; the simulator stubs report honest "persistence unavailable" so the
# real parse/verify/commit path still links and runs on the desktop.
STUBS = os.path.join(ROOT, "framework", "simulator", "qeaf_sim_stubs.c")
EXE = os.path.join(OUT, "vqeaf-qeapp-runtime.exe")


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
        package = qeaf.load_compiler().compile_project(project, "release", qeaf)
        shutil.copyfile(package, good)

    tampered = os.path.join(OUT, "hello-tampered.qeapp")
    with zipfile.ZipFile(good) as archive:
        entry = json.loads(archive.read("manifest.json"))["entry"]
        entries = [(n, archive.read(n)) for n in archive.namelist()]
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

    def write_native(path, module):
        with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
            archive.writestr("manifest.json", json.dumps(native_manifest))
            archive.writestr(native_manifest["entry"], module)
            archive.writestr("assets/icon.rgba", b"\0" * (64 * 64 * 4))
        qeaf.sign_package(path)

    stub = qen.build_stub("arm64", min_api=5, abi_version=5)
    write_native(native_good, stub)
    tampered_module = bytearray(stub)
    tampered_module[49] ^= 0xFF  # payload_hash
    write_native(native_bad, bytes(tampered_module))
    return good, tampered, boundary, native_good, native_bad


def compile_and_link():
    objects = []
    flags = r.CFLAGS + ["-D__linux__"] + r.INCLUDES
    for src in (RUNTIME, NATIVE, STUBS, TEST):
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


def run(package, mode):
    proc = subprocess.run([EXE, package, mode], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print("== %s (%s) ==" % (os.path.basename(package), mode))
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    return proc.returncode


def main():
    if not os.path.isfile(r.ZIG):
        raise SystemExit("test_qeapp_runtime: zig not found at %s" % r.ZIG)
    good, tampered, boundary, native_good, native_bad = build_packages()
    compile_and_link()
    rc = run(good, "ok")
    rc |= run(tampered, "tampered")
    rc |= run(boundary, "limit")
    rc |= run(native_good, "native")
    rc |= run(native_good, "native-install")
    rc |= run(native_bad, "native-bad")
    rc |= run(good, "preview")
    rc |= run(tampered, "preview-tampered")
    if rc == 0:
        print("test_qeapp_runtime: ALL OK")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())