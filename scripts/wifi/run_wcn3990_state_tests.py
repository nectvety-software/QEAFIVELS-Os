#!/usr/bin/env python3
"""Build and run the phase2 state-machine rollback test (test doubles only)."""
import subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import render_phone_ui as r

OUT = ROOT / "build/wcn3990-sm-test"
EXE = OUT / "vqeaf-wcn3990-sm-test.exe"
SOURCES = [
    ROOT / "kernel/drivers/wcn3990_phase2.c",
    ROOT / "system/core/driver.c",
    ROOT / "system/core/capability.c",
    ROOT / "tests/vqeaf_wcn3990_sm_test.c",
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
    p = subprocess.run([str(EXE)], capture_output=True, text=True)
    print(p.stdout, end="")
    if p.stderr: print(p.stderr, end="", file=sys.stderr)
    return p.returncode

if __name__ == "__main__":
    raise SystemExit(main())
