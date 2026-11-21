#!/usr/bin/env python3
"""Deterministic host-simulator gate for QEAPP framework API v2 (Phase 3).

Builds real x86_64 .qen modules from freestanding PIC C fixtures with the SDK
compiler/linker, signs them into .qeapp v2 packages, then compiles the simulator
core (the real device inspector + framework/simulator/qeaf_sim.c) twice: once
with the CLI driver and once with tests/vqeaf_sim_test.c. Runs the deterministic
C gate (lifecycle, trace, determinism, storage cleanup, permission denial, leak
detection, structural rejections, framebuffer pixels) and the CLI acceptance run
of the native Hello. arm64 is built and rejected on the host (device execution is
a Phase 4 gate, never claimed here).

Output: build/qeapp-simulator/
"""
import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "qeapp-simulator"
FIXTURES = ROOT / "tests" / "fixtures" / "qeapp-sim"
TEMPLATE = ROOT / "sdk" / "templates" / "hello-qeapp-native"
TEST_C = ROOT / "tests" / "vqeaf_sim_test.c"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


qeaf = load("qeaf", ROOT / "sdk" / "cli" / "qeaf.py")
qen = load("qen", ROOT / "sdk" / "compiler" / "qen.py")
compiler = load("qecompile", ROOT / "sdk" / "compiler" / "qecompile.py")
qesim = load("qesim", ROOT / "sdk" / "simulator" / "qesim.py")

ICON = (TEMPLATE / "assets" / "icon.rgba").read_bytes()


def manifest(arch, perms, ident, name, abi=2):
    return {
        "format": "qeapp", "format_version": 2, "abi_version": abi,
        "runtime": "native", "id": ident, "name": name, "version": "1.0.0",
        "min_os": "0.3.0", "entry": "native/%s/app.qen" % arch,
        "icon": "assets/icon.rgba", "architecture": [arch],
        "permissions": perms, "dependencies": [],
    }


def build_module_from_c(source, arch, abi=2):
    with tempfile.TemporaryDirectory(prefix="simfix-") as td:
        project = Path(td) / "proj"
        (project / "src").mkdir(parents=True)
        entry = "native/%s/app.qen" % arch
        return compiler.compile_native(qen, project, arch, [Path(source)], entry,
                                       abi_version=abi)


def assemble(path, mani, module):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("manifest.json", json.dumps(mani))
        archive.writestr(mani["entry"], module)
        archive.writestr("assets/icon.rgba", ICON)
    qeaf.sign_package(path)


def split_module(data):
    code_size = struct.unpack_from("<I", data, 21)[0]
    rodata_size = struct.unpack_from("<I", data, 25)[0]
    data_size = struct.unpack_from("<I", data, 29)[0]
    bss_size = struct.unpack_from("<I", data, 33)[0]
    entry_offset = struct.unpack_from("<I", data, 37)[0]
    off = 64
    code = data[off:off + code_size]; off += code_size
    rodata = data[off:off + rodata_size]; off += rodata_size
    data_sec = data[off:off + data_size]
    _, exports = qen.read_tables(data)
    return {"code": code, "rodata": rodata, "data": data_sec,
            "bss": bss_size, "exports": exports, "entry": entry_offset}


def build_fixtures(fixdir):
    fixdir.mkdir(parents=True, exist_ok=True)
    hello = build_module_from_c(TEMPLATE / "src" / "main.c", "x86_64", abi=5)
    probe = build_module_from_c(FIXTURES / "probe.c", "x86_64")
    input_probe = build_module_from_c(FIXTURES / "input_probe.c", "x86_64")
    leak = build_module_from_c(FIXTURES / "leak.c", "x86_64")
    weight = build_module_from_c(FIXTURES / "weight.c", "x86_64", abi=4)
    layout = build_module_from_c(FIXTURES / "layout.c", "x86_64", abi=5)
    arm64 = build_module_from_c(TEMPLATE / "src" / "main.c", "arm64", abi=5)

    real = {
        "hello.qeapp": (manifest("x86_64", ["ui.display"],
                                 "org.vqeaf.sim-hello", "Sim Hello", abi=5), hello),
        "probe.qeapp": (manifest("x86_64", ["ui.display", "storage.read", "input.receive"],
                                 "org.vqeaf.sim-probe", "Sim Probe"), probe),
        "probe-noperm.qeapp": (manifest("x86_64", ["ui.display"],
                                        "org.vqeaf.sim-probe-noperm", "Sim Probe NoPerm"), probe),
        "inputprobe.qeapp": (manifest("x86_64", ["ui.display", "input.receive"],
                                      "org.vqeaf.sim-inputprobe", "Sim Input Probe"), input_probe),
        "leak.qeapp": (manifest("x86_64", ["ui.display", "storage.read"],
                                "org.vqeaf.sim-leak", "Sim Leak"), leak),
        "weight.qeapp": (manifest("x86_64", ["ui.display"],
                                  "org.vqeaf.sim-weight", "Sim Weight", abi=4), weight),
        "weight-noperm.qeapp": (manifest("x86_64", [],
                                         "org.vqeaf.sim-weight-noperm", "Sim Weight NoPerm",
                                         abi=4), weight),
        "layout.qeapp": (manifest("x86_64", ["ui.display"],
                                  "org.vqeaf.sim-layout", "Sim Layout", abi=5), layout),
        "layout-noperm.qeapp": (manifest("x86_64", [],
                                         "org.vqeaf.sim-layout-noperm", "Sim Layout NoPerm",
                                         abi=5), layout),
        "arm64.qeapp": (manifest("arm64", ["ui.display"],
                                 "org.vqeaf.sim-arm64", "Sim Arm64", abi=5), arm64),
    }
    for name, (mani, module) in real.items():
        assemble(fixdir / name, mani, module)
        errors = qeaf.validate_package(fixdir / name)
        if errors:
            raise SystemExit("fixture %s invalid: %s" % (name, errors))

    parts = split_module(hello)
    xmani = manifest("x86_64", ["ui.display"], "org.vqeaf.sim-variant", "Sim Variant",
                     abi=5)
    stub = qen.build_stub("x86_64")
    badimport = qen.build_module(parts["code"], parts["rodata"], parts["data"],
                                 parts["bss"], parts["exports"],
                                 imports=["unknown.import"], arch="x86_64",
                                 min_api=2, entry_offset=parts["entry"])
    missingexport = qen.build_module(parts["code"], parts["rodata"], parts["data"],
                                     parts["bss"],
                                     [e for e in parts["exports"] if e[0] != "destroy"],
                                     arch="x86_64", min_api=2,
                                     entry_offset=parts["entry"])
    assemble(fixdir / "stub.qeapp", xmani, stub)
    assemble(fixdir / "badimport.qeapp", xmani, badimport)
    assemble(fixdir / "missingexport.qeapp", xmani, missingexport)

    # Tampered: flip a code byte in the signed hello package, keep the old
    # SIGNATURE so the HMAC no longer matches (signature is checked first).
    with zipfile.ZipFile(fixdir / "hello.qeapp") as archive:
        entries = [(n, archive.read(n)) for n in archive.namelist()]
    entry_name = json.loads(dict(entries)["manifest.json"])["entry"]
    with zipfile.ZipFile(fixdir / "tampered.qeapp", "w", zipfile.ZIP_STORED) as archive:
        for name, data in entries:
            if name == entry_name:
                blob = bytearray(data)
                blob[64 + 10] ^= 0xFF
                data = bytes(blob)
            archive.writestr(name, data)
    return sorted(p.name for p in fixdir.glob("*.qeapp"))


def run(exe, args, label):
    proc = subprocess.run([str(exe)] + [str(a) for a in args],
                          capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print("== %s ==" % label)
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    return proc.returncode, proc.stdout


def main():
    try:
        qesim.find_zig()
    except ValueError as exc:
        raise SystemExit("test_qeapp_simulator: %s" % exc)
    OUT.mkdir(parents=True, exist_ok=True)
    fixdir = OUT / "fixtures"
    names = build_fixtures(fixdir)
    print("fixtures: %s" % ", ".join(names))

    test_exe = qesim.build(OUT, TEST_C, "vqeaf-sim-test.exe")
    cli_exe = qesim.build_simulator(OUT)

    rc, _ = run(test_exe, [fixdir], "deterministic simulator gate")
    cli_rc, cli_out = run(cli_exe, [fixdir / "hello.qeapp"], "CLI acceptance: native Hello")
    rc |= cli_rc
    accepted = ("lifecycle: create start pause resume stop destroy" in cli_out
                and "outstanding allocs: 0" in cli_out and cli_rc == 0)
    print("acceptance (Hello runs in simulator): %s" % ("PASS" if accepted else "FAIL"))
    if not accepted:
        rc |= 1

    _, listed = run(cli_exe, ["--list-devices"], "CLI device list")
    listing_ok = "violet: Redmi Note 7 Pro panel=1080x2340 canvas=720x1560" in listed
    print("device list: %s" % ("PASS" if listing_ok else "FAIL"))
    if not listing_ok:
        rc |= 1

    panel_raw = OUT / "violet-panel.raw"
    dev_rc, dev_out = run(
        cli_exe,
        ["--device", "violet", fixdir / "hello.qeapp", "--panel-fb", panel_raw],
        "CLI acceptance: Redmi Note 7 Pro device VM",
    )
    device_ok = (dev_rc == 0
                 and "device: Redmi Note 7 Pro (violet) panel=1080x2340 "
                     "canvas=720x1560 format=XRGB8888" in dev_out
                 and "framebuffer: 720x1560" in dev_out
                 and panel_raw.is_file()
                 and panel_raw.stat().st_size == 1080 * 2340 * 4)
    print("acceptance (violet panel scanout): %s" % ("PASS" if device_ok else "FAIL"))
    if not device_ok:
        rc |= 1

    print("test_qeapp_simulator: %s" % ("ALL OK" if rc == 0 else "FAILED"))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())