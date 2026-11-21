#!/usr/bin/env python3
"""Build and test the CodeEditor QEAPP port on the host.

Checks four things:
  1. the portable core stays free of MRE/LVGL/ABI symbols;
  2. it compiles cleanly with a host C compiler;
  3. tests/host_test.c passes (buffer, tokenizer, store, app state machine);
  4. the project compiles to a real arm64 .qen and a valid QEAPP v2 package.

The package now carries compiled code. This script proves the portable core and
the arm64 module structure; running that module is covered separately by
scripts/test_codeeditor_native.py (x86_64 host simulator).
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "examples" / "codeeditor-qeapp"
SRC = PROJECT / "src"
TEST = PROJECT / "tests" / "host_test.c"
OUT = ROOT / "build" / "codeeditor-port"
SOURCES = ["qeaf_code_buffer.c", "qeaf_code_hl.c", "qeaf_code_store.c",
           "qeaf_code_app.c"]
PORTABLE = SOURCES + [
    "qeaf_code_app.h", "qeaf_code_buffer.h", "qeaf_code_hl.h",
    "qeaf_code_store.h", "qeaf_code_platform.h", "qeaf_code_types.h",
    "qeaf_code_util.h",
]
FORBIDDEN = ["vm_", "vmsys.h", "vmio.h", "vmgraph.h", "VxpRender2D",
             "lv_", "qeaf_app_api_v2"]


def find_compiler():
    candidates = [
        os.environ.get("CC"),
        r"C:\msys64\mingw64\bin\gcc.exe",
        shutil.which("gcc"),
        shutil.which("clang"),
        shutil.which("cc"),
        r"C:\msys64\mingw32\bin\gcc.exe",
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise SystemExit("test_codeeditor_port: no host C compiler found (set CC)")


def strip_comments(text):
    out = []
    i = 0
    while i < len(text):
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = len(text) if end < 0 else end + 2
            out.append(" ")
        elif text.startswith("//", i):
            end = text.find("\n", i + 2)
            i = len(text) if end < 0 else end
            out.append(" ")
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def check_portable_sources():
    problems = []
    for name in PORTABLE:
        file = SRC / name
        if not file.is_file():
            problems.append(f"{name}: missing")
            continue
        text = strip_comments(file.read_text(encoding="utf-8"))
        for token in FORBIDDEN:
            if token in text:
                problems.append(f"{file.name}: contains {token!r}")
    if problems:
        raise SystemExit("test_codeeditor_port: portable core is not isolated:\n  "
                         + "\n  ".join(problems))


def build_and_run_host_test(cc):
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "codeeditor_host_test.exe"
    cmd = [cc, "-std=c99", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-Werror", "-I", str(SRC)]
    cmd += [str(SRC / name) for name in SOURCES]
    cmd += [str(TEST), "-o", str(exe)]
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr or proc.stdout)
        raise SystemExit("test_codeeditor_port: host compile FAILED "
                         f"(exit {proc.returncode}, compiler {cc})")
    run = subprocess.run([str(exe)], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    sys.stdout.write(run.stdout)
    if run.returncode != 0 or "host tests: OK" not in run.stdout:
        sys.stderr.write(run.stderr)
        raise SystemExit("test_codeeditor_port: host tests FAILED")


def load_sdk():
    spec = importlib.util.spec_from_file_location("qeaf_cli",
                                                  ROOT / "sdk" / "cli" / "qeaf.py")
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    return sdk


def check_package(sdk):
    manifest = json.loads((PROJECT / "manifest.json").read_text(encoding="utf-8"))
    errors = sdk.validate_manifest(manifest)
    if errors:
        raise SystemExit("test_codeeditor_port: manifest invalid: " + "; ".join(errors))
    if manifest.get("runtime") != "native" or manifest.get("abi_version") != 2:
        raise SystemExit("test_codeeditor_port: project is not a native v2 QEAPP")

    spec = importlib.util.spec_from_file_location(
        "qeaf_compiler", ROOT / "sdk" / "compiler" / "qecompile.py")
    compiler = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compiler)
    package = compiler.compile_project(PROJECT, "release", sdk)
    errors = sdk.validate_package(package)
    if errors:
        raise SystemExit("test_codeeditor_port: package invalid: " + "; ".join(errors))
    with tempfile.TemporaryDirectory(prefix="codeeditor-package-") as temp:
        copy = Path(temp) / package.name
        shutil.copy2(package, copy)
        if copy.stat().st_size != package.stat().st_size:
            raise SystemExit("test_codeeditor_port: package copy mismatch")
    qen = compiler.load_qen()
    import zipfile
    with zipfile.ZipFile(package) as archive:
        module = archive.read(manifest["entry"])
    if len(module) <= 64:
        raise SystemExit("test_codeeditor_port: module is still the empty stub")
    errors = qen.validate_module(module, manifest["architecture"][0])
    if errors:
        raise SystemExit("test_codeeditor_port: module invalid: " + "; ".join(errors))
    imports, exports = qen.read_tables(module)
    names = {name for name, _ in exports}
    required = {"create", "start", "pause", "resume", "stop", "destroy",
                "on_input"}
    if not required <= names:
        raise SystemExit("test_codeeditor_port: missing exports: "
                         + ", ".join(sorted(required - names)))
    if imports != ["qeaf_app_api_v2"]:
        raise SystemExit("test_codeeditor_port: unexpected imports: %r" % (imports,))
    print(f"module: {len(module)} bytes, exports {len(names)}, imports {imports}")
    print(f"package: {package.relative_to(ROOT)} "
          f"({package.stat().st_size} bytes, validated)")


def main():
    check_portable_sources()
    build_and_run_host_test(find_compiler())
    check_package(load_sdk())
    print("codeeditor port: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
