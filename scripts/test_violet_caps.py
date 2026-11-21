#!/usr/bin/env python3
"""Host gate for the violet capability census (vio_caps).

Compiles system/core/capability.c + platform/violet/vio_caps.c +
tests/vqeaf_violet_caps.c with the host zig toolchain (no LVGL, no device) and
runs the synthetic-probe state assertions. Proves the honest capability mapping
and the BLOCKED_FIRMWARE -> READY transition without hardware.
"""
import os
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEAF = os.environ.get("QEAFDROPOS") or "D:\\QeafivelsOfficial\\QeafdropOs"
ZIG = os.environ.get("ZIG") or os.path.join(
    QEAF, "out", "toolchain", "zig-windows-x86_64-0.13.0", "zig.exe")
OUTDIR = os.path.join(ROOT, "build", "ui-host")
EXE = os.path.join(OUTDIR, "vqeaf-violet-caps.exe")

SOURCES = [
    os.path.join(ROOT, "system", "core", "capability.c"),
    os.path.join(ROOT, "platform", "violet", "vio_caps.c"),
    os.path.join(ROOT, "platform", "violet", "vqeaf_camera.c"),
    os.path.join(ROOT, "tests", "vqeaf_violet_caps.c"),
]
INCLUDES = [
    "-I", os.path.join(ROOT, "system", "core", "include"),
    "-I", os.path.join(ROOT, "platform", "violet"),
]
CFLAGS = ["-O2", "-g", "-Wall", "-Wextra", "-std=c11"]


def main():
    if not os.path.isfile(ZIG):
        print("test_violet_caps: zig not found at %s (set ZIG=...)" % ZIG,
              file=sys.stderr)
        return 2
    os.makedirs(OUTDIR, exist_ok=True)
    cmd = [ZIG, "cc", "-target", "x86_64-windows-gnu"] + CFLAGS + INCLUDES + \
          ["-o", EXE] + SOURCES
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        return 1
    print("test_violet_caps: built %s" % EXE)
    proc = subprocess.run([EXE], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())