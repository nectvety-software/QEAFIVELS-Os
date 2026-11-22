#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and run the host-side Wi-Fi HAL C tests against FIXTURE inputs."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tests" / "wifi"
FIXTURE = TESTS / "fixtures" / "FIXTURE_violet_wifi.dtb"
OUT = ROOT / "build" / "wifi-hal-tests"


def run(cmd, label):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        print("FAIL %s:\n%s%s" % (label, p.stdout, p.stderr))
        raise SystemExit(p.returncode)
    return p


def compiler():
    env = os.environ.get("ZIG")
    if env and Path(env).is_file():
        return [env, "cc"]
    base = os.environ.get("QEAFDROPOS") or "D:\\QeafivelsOfficial\\QeafdropOs"
    z = Path(base) / "out" / "toolchain" / "zig-windows-x86_64-0.13.0" / "zig.exe"
    if z.is_file():
        return [str(z), "cc"]
    gcc = shutil.which("gcc")
    if gcc:
        return [gcc]
    raise SystemExit("no C compiler found (set ZIG or install gcc)")


def main():
    run([sys.executable, str(TESTS / "gen_dtb_fixture.py")], "generate FIXTURE")
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / ("test_wifi_hal.exe" if os.name == "nt" else "test_wifi_hal")
    cc = compiler()
    srcs = [
        ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" / "wifi_device.c",
        ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" / "wifi_firmware.c",
        TESTS / "test_wifi_hal.c",
    ]
    run(cc + ["-std=c11", "-Wall", "-Wextra", "-O1",
              "-I", str(ROOT / "kernel" / "include")] +
        [str(s) for s in srcs] + ["-o", str(exe)], "compile HAL tests")
    p = run([str(exe), str(FIXTURE)], "run HAL tests")
    sys.stdout.write(p.stdout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
