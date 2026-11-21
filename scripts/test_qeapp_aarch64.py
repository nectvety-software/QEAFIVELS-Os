#!/usr/bin/env python3
"""Phase 4 AArch64 execution gate (EMULATED, QEMU virt - not physical hardware).

Builds the real arm64 Hello .qen with the SDK compiler/linker, validates it,
embeds it in a freestanding AArch64 harness, and runs it under
qemu-system-aarch64 -machine virt. The harness parses the module, maps sections
into a static arena, resolves exports, runs the lifecycle against a fake API v2
and prints a framebuffer hash over PL011 UART. The hash is compared against the
x86_64 host simulator hash for the same app: identical hashes prove the arm64
code executes and renders the same frame (cross-arch determinism).

This is emulated AArch64 evidence. It is NOT physical violet hardware and NOT
the full violet shell; those remain a device gate requiring an authorized board.
"""
import importlib.util
import os
import re
import subprocess
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "qeapp-aarch64"
HARNESS_DIR = ROOT / "tests" / "fixtures" / "qeapp-aarch64"
FIXTURES = ROOT / "tests" / "fixtures" / "qeapp-sim"
TEMPLATE = ROOT / "sdk" / "templates" / "hello-qeapp-native"
QEMU = os.environ.get("VQEAF_QEMU_AARCH64") or \
    r"C:\Program Files\QEMU\qemu-system-aarch64.exe"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sim_gate = load("sim_gate", ROOT / "scripts" / "test_qeapp_simulator.py")
qen = sim_gate.qen
qesim = sim_gate.qesim


def run(cmd, timeout=120):
    proc = subprocess.run([str(c) for c in cmd], capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)
    return proc


def host_hash(fixdir, runner, source, abi=2, ident="org.vqeaf.a64-host"):
    module = sim_gate.build_module_from_c(source, "x86_64", abi=abi)
    pkg = fixdir / ("host-%d.qeapp" % abi)
    sim_gate.assemble(pkg, sim_gate.manifest("x86_64", ["ui.display"],
                                             ident, "Host", abi=abi), module)
    proc = run([runner, pkg])
    m = re.search(r"hash=0x([0-9a-f]{8})", proc.stdout)
    if proc.returncode != 0 or not m:
        raise SystemExit("aarch64 gate: host reference run failed:\n" + proc.stdout)
    return m.group(1)


def build_harness(module):
    blob = OUT / "module_blob.c"
    lines = ["#include <stdint.h>", "const uint8_t qen_blob[] = {"]
    lines += [",".join(str(b) for b in module[i:i + 16]) + ","
              for i in range(0, len(module), 16)]
    lines.append("};")
    lines.append("const uint32_t qen_blob_size = %du;" % len(module))
    blob.write_text("\n".join(lines), encoding="utf-8")

    zig = qesim.find_zig()
    inc = ["-I", str(ROOT / "framework" / "application_api"),
           "-I", str(ROOT / "system" / "ui" / "include")]
    flags = ["-target", "aarch64-linux-musl", "-ffreestanding", "-fno-pie",
             "-fno-builtin", "-fno-stack-protector", "-mstrict-align",
             "-O2", "-std=c11", "-Wall", "-Wextra"]
    objs = []
    for src in (HARNESS_DIR / "harness.c", HARNESS_DIR / "start.S", blob):
        obj = OUT / (src.stem + ".o")
        proc = run([zig, "cc"] + flags + inc + ["-c", str(src), "-o", str(obj)])
        if proc.returncode != 0:
            raise SystemExit("aarch64 compile FAILED %s:\n%s"
                             % (src.name, proc.stderr or proc.stdout))
        objs.append(obj)
    elf = OUT / "harness.elf"
    proc = run([zig, "cc", "-target", "aarch64-linux-musl", "-nostdlib",
                "-ffreestanding", "-fno-pie", "-no-pie",
                "-Wl,-T," + str(HARNESS_DIR / "harness.ld"),
                "-Wl,--build-id=none", "-o", str(elf)] + objs)
    if proc.returncode != 0:
        raise SystemExit("aarch64 link FAILED:\n" + proc.stderr or proc.stdout)
    head = elf.read_bytes()[:20]
    if head[:4] != b"\x7fELF" or head[18:20] != b"\xb7\x00":
        raise SystemExit("aarch64 gate: harness is not an AArch64 ELF")
    return elf


def run_harness(module, label):
    elf = build_harness(module)
    uart_log = OUT / ("uart-%s.log" % label)
    if uart_log.exists():
        uart_log.unlink()
    err = ""
    try:
        proc = run([QEMU, "-machine", "virt", "-cpu", "cortex-a57", "-m", "128M",
                    "-display", "none", "-serial", "file:" + str(uart_log),
                    "-monitor", "none", "-no-reboot", "-kernel", str(elf)],
                   timeout=30)
        err = proc.stderr
    except subprocess.TimeoutExpired:
        err = "(qemu timeout)"
    out = uart_log.read_text(errors="replace") if uart_log.exists() else ""
    print("== QEMU virt AArch64 UART (%s) ==" % label)
    print(out, end="")
    if err:
        print(err, end="", file=sys.stderr)
    return out


def main():
    if not Path(QEMU).is_file():
        raise SystemExit("aarch64 gate: qemu-system-aarch64 not found at %s "
                         "(set VQEAF_QEMU_AARCH64)" % QEMU)
    OUT.mkdir(parents=True, exist_ok=True)
    fixdir = OUT / "fixtures"
    fixdir.mkdir(parents=True, exist_ok=True)
    runner = qesim.build_simulator(OUT)

    ok = True
    def check(cond, msg):
        nonlocal ok
        print(("ok   " if cond else "FAIL ") + msg)
        ok = ok and cond

    # --- v5 Hello: cross-arch determinism (current default) ---
    module = sim_gate.build_module_from_c(TEMPLATE / "src" / "main.c", "arm64",
                                          abi=5)
    errors = qen.validate_module(module, "arm64")
    if errors:
        raise SystemExit("aarch64 gate: arm64 module invalid: %s" % errors)
    print("arm64 hello module: %d bytes, validated" % len(module))
    expected = host_hash(fixdir, runner, TEMPLATE / "src" / "main.c", abi=5,
                         ident="org.vqeaf.a64-host")
    print("host x86_64 hello reference hash: 0x%s" % expected)
    out = run_harness(module, "hello")
    check("AARCH64 HARNESS: start" in out, "harness booted on QEMU virt")
    check("lifecycle: create start pause resume stop destroy" in out,
          "arm64 module ran full lifecycle")
    check("Hello Native QEAPP: create" in out, "arm64 create logged")
    check("HARNESS OK" in out, "harness completed")
    m = re.search(r"hash=0x([0-9a-f]{8})", out)
    check(m is not None, "arm64 framebuffer hash printed")
    if m:
        check(m.group(1) == expected,
              "arm64 hash matches x86_64 host (0x%s)" % expected)
    check("outstanding allocs: 0" in out, "no leaked allocations on arm64")

    # --- v4 weight: bold raster executes identically on arm64 ---
    wmodule = sim_gate.build_module_from_c(FIXTURES / "weight.c", "arm64", abi=4)
    werrors = qen.validate_module(wmodule, "arm64")
    if werrors:
        raise SystemExit("aarch64 gate: arm64 weight module invalid: %s" % werrors)
    print("arm64 weight module: %d bytes, validated (abi 4)" % len(wmodule))
    wexpected = host_hash(fixdir, runner, FIXTURES / "weight.c", abi=4,
                          ident="org.vqeaf.a64-weight")
    print("host x86_64 weight reference hash: 0x%s" % wexpected)
    wout = run_harness(wmodule, "weight")
    check("AARCH64 HARNESS: start" in wout, "weight harness booted on QEMU virt")
    check("lifecycle: create start pause resume stop destroy" in wout,
          "arm64 weight module ran full lifecycle")
    check("weight fixture: invalid weight rejected" in wout,
          "arm64 runtime rejected unknown weight")
    check("HARNESS OK" in wout, "weight harness completed")
    wm = re.search(r"hash=0x([0-9a-f]{8})", wout)
    check(wm is not None, "arm64 weight framebuffer hash printed")
    if wm:
        check(wm.group(1) == wexpected,
              "arm64 weight hash matches x86_64 host (0x%s)" % wexpected)
    check("outstanding allocs: 0" in wout, "no leaked allocations on arm64 weight")

    # --- v5 layout: proportional split executes identically on arm64 ---
    lmodule = sim_gate.build_module_from_c(FIXTURES / "layout.c", "arm64", abi=5)
    lerrors = qen.validate_module(lmodule, "arm64")
    if lerrors:
        raise SystemExit("aarch64 gate: arm64 layout module invalid: %s" % lerrors)
    print("arm64 layout module: %d bytes, validated (abi 5)" % len(lmodule))
    lexpected = host_hash(fixdir, runner, FIXTURES / "layout.c", abi=5,
                          ident="org.vqeaf.a64-layout")
    print("host x86_64 layout reference hash: 0x%s" % lexpected)
    lout = run_harness(lmodule, "layout")
    check("AARCH64 HARNESS: start" in lout, "layout harness booted on QEMU virt")
    check("lifecycle: create start pause resume stop destroy" in lout,
          "arm64 layout module ran full lifecycle")
    check("layout fixture: invalid splits rejected" in lout,
          "arm64 runtime rejected invalid splits")
    check("layout fixture: splits ok" in lout, "arm64 runtime computed valid splits")
    check("HARNESS OK" in lout, "layout harness completed")
    lm = re.search(r"hash=0x([0-9a-f]{8})", lout)
    check(lm is not None, "arm64 layout framebuffer hash printed")
    if lm:
        check(lm.group(1) == lexpected,
              "arm64 layout hash matches x86_64 host (0x%s)" % lexpected)
    check("outstanding allocs: 0" in lout, "no leaked allocations on arm64 layout")

    print("test_qeapp_aarch64: %s" % ("ALL OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())