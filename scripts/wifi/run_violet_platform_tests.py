#!/usr/bin/env python3
"""Build and run the Violet platform backend host gate against the real DTB."""
import os, subprocess, sys
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import render_phone_ui as r

ROOT = Path(r.ROOT)
DTB = ROOT / "platform/violet/device-tree/build/violet-reference.dtb"
OUT = ROOT / "build/violet-platform-test"
EXE = OUT / "vqeaf-violet-platform-test.exe"
SOURCES = [
    ROOT / "kernel/drivers/wifi/wcn3990/violet_platform.c",
    ROOT / "kernel/drivers/wcn3990_snoc.c",
    ROOT / "kernel/drivers/wcn3990_phase2.c",
    ROOT / "system/core/driver.c",
    ROOT / "system/core/capability.c",
    ROOT / "tests/vqeaf_violet_platform_test.c",
]

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu"] + r.CFLAGS + r.INCLUDES + \
          [str(s) for s in SOURCES] + ["-I", str(ROOT / "kernel/include"),
                                      "-I", str(ROOT / "tests"),
                                      "-o", str(EXE)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        print(p.stderr or p.stdout, file=sys.stderr)
        return p.returncode
    p = subprocess.run([str(EXE), str(DTB)], capture_output=True, text=True)
    print(p.stdout, end="")
    if p.stderr: print(p.stderr, end="", file=sys.stderr)
    return p.returncode

if __name__ == "__main__":
    raise SystemExit(main())
