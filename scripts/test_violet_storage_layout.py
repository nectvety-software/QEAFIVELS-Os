#!/usr/bin/env python3
"""Build and run the real storage layout gate on a host FAT16 volume."""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_phone_ui as r

SRC = [
    os.path.join(r.ROOT, "platform", "violet", "channel", "fs", "fat16.c"),
    os.path.join(r.ROOT, "platform", "violet", "channel", "fs", "storage.c"),
    os.path.join(r.ROOT, "platform", "violet", "channel", "core", "log.c"),
    os.path.join(r.ROOT, "tests", "vqeaf_storage_layout_test.c"),
]
OUT = os.path.join(r.ROOT, "build", "storage-layout")
EXE = os.path.join(OUT, "vqeaf-storage-layout-test.exe")


def main():
    os.makedirs(OUT, exist_ok=True)
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu"] + r.CFLAGS + \
          r.INCLUDES + SRC + ["-o", EXE]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        print(p.stderr or p.stdout, file=sys.stderr)
        return p.returncode
    p = subprocess.run([EXE], capture_output=True, text=True)
    print(p.stdout, end="")
    if p.stderr:
        print(p.stderr, end="", file=sys.stderr)
    return p.returncode


if __name__ == "__main__":
    raise SystemExit(main())
