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


def build_and_run(name, srcs, args):
    exe = OUT / (name + (".exe" if os.name == "nt" else ""))
    run(compiler() + ["-std=c11", "-Wall", "-Wextra", "-O1",
                      "-I", str(ROOT / "kernel" / "include")] +
        [str(s) for s in srcs] + ["-o", str(exe)], "compile " + name)
    p = run([str(exe)] + args, "run " + name)
    sys.stdout.write(p.stdout)


def main():
    run([sys.executable, str(TESTS / "gen_dtb_fixture.py")], "generate DTB FIXTURE")
    run([sys.executable, str(TESTS / "gen_qrtr_qmi_fixtures.py")],
        "generate QRTR/QMI FIXTUREs")
    OUT.mkdir(parents=True, exist_ok=True)
    drv = ROOT / "kernel" / "drivers" / "wifi" / "wcn3990"
    build_and_run("test_wifi_hal", [
        drv / "wifi_device.c", drv / "wifi_firmware.c",
        TESTS / "test_wifi_hal.c"], [str(FIXTURE)])
    build_and_run("test_qrtr_qmi", [
        drv / "qrtr_transport.c", drv / "qmi_service.c",
        TESTS / "test_qrtr_qmi.c"], [str(TESTS / "fixtures")])
    build_and_run("test_firmware_boot", [
        drv / "firmware_boot.c", TESTS / "test_firmware_boot.c"], [])
    # Container validation is checked against synthetic images, then against
    # the real WCN3990 blobs staged into the violet image when present.
    real_fw = ROOT / "build" / "violet-fw" / "image"
    fw_args = []
    if (real_fw / "wlanmdsp.mbn").is_file():
        fw_args = [str(real_fw)]
    build_and_run("test_wifi_firmware", [
        drv / "wifi_firmware.c", TESTS / "test_wifi_firmware.c"], fw_args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
