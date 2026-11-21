#!/usr/bin/env python3
"""Render the real violet boot splash from source into build/screens.

This is host-side visual evidence, not a device screenshot: it compiles
platform/violet/splash.c with the real font atlas and the boot lines used by
main_violet.c, then converts the PPM with the same converter used by
scripts/capture_screen.py. The real device path remains a separate gate.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from capture_screen import ppm_to_png  # noqa: E402

BUILD = ROOT / "build"
OUT = BUILD / "screens"
HARNESS = ROOT / "tests" / "vqeaf_splash_host.c"
SOURCE = ROOT / "platform" / "violet" / "splash.c"


def find_zig():
    env = os.environ.get("ZIG")
    if env and Path(env).is_file():
        return env
    base = os.environ.get("QEAFDROPOS") or r"D:\QeafivelsOfficial\QeafdropOs"
    zig = Path(base) / "out" / "toolchain" / "zig-windows-x86_64-0.13.0" / "zig.exe"
    if zig.is_file():
        return str(zig)
    raise SystemExit("zig toolchain not found; set ZIG=/path/to/zig")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = BUILD / "ui-host" / "vqeaf-splash-host.exe"
    exe.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([find_zig(), "cc", "-O2", "-std=c11",
                    "-I", str(ROOT / "system" / "ui"),
                    "-o", str(exe), str(HARNESS), str(SOURCE)], check=True)
    ppm = OUT / "boot-terminal.ppm"
    png = OUT / "boot-terminal.png"
    subprocess.run([str(exe), str(ppm), "720", "1560"], check=True)
    w, h, teal, total = ppm_to_png(ppm, png)
    if (w, h) != (720, 1560):
        raise SystemExit(f"unexpected violet canvas: {w}x{h}")
    if teal < 1000:
        raise SystemExit(f"no terminal boot log detected: {teal} teal pixels")
    print(f"CAPTURE boot-terminal (host render): {png}  {w}x{h}  "
          f"phosphor pixels={teal}/{total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
