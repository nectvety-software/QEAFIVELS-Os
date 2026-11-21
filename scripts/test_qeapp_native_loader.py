#!/usr/bin/env python3
"""Compile and run the host test for the QEAPP native module validator.

This exercises platform/violet/ui/vqeaf_qeapp_native.c with the same Zig
toolchain the other host tests use. It proves validation and rejection only;
no native module is executed.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_phone_ui as r  # noqa: E402

ROOT = r.ROOT
OUT = os.path.join(ROOT, "build", "qeapp-native")
SOURCE = os.path.join(ROOT, "platform", "violet", "ui", "vqeaf_qeapp_native.c")
TEST = os.path.join(ROOT, "tests", "vqeaf_qen_validate.c")
EXE = os.path.join(OUT, "vqeaf-qen-validate.exe")


def main():
    os.makedirs(OUT, exist_ok=True)
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu", "-O1", "-g",
           "-Wall", "-Wextra", "-std=c11", "-Werror",
           "-I", os.path.join(ROOT, "platform", "violet", "ui"),
           SOURCE, TEST, "-o", EXE]
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        raise SystemExit("test_qeapp_native_loader: compile FAILED")
    run = subprocess.run([EXE], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    sys.stdout.write(run.stdout)
    if run.returncode != 0:
        sys.stderr.write(run.stderr)
        raise SystemExit("test_qeapp_native_loader: FAILED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
