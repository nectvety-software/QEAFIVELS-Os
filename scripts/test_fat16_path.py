#!/usr/bin/env python3
"""Build and run the FAT16 cluster/path browsing test on a real in-RAM volume.

Compiles platform/violet/channel/fs/fat16.c (plus the log ring it calls) with
tests/vqeaf_fat16_path_test.c and asserts directory listing, nested reads and
path-traversal rejection - the device side of the file manager rescue tab.
"""
import os
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_phone_ui as r  # noqa: E402

OUT = os.path.join(r.ROOT, "build", "fat16-path")
EXE = os.path.join(OUT, "vqeaf-fat16-path-test.exe")
SOURCES = [
    os.path.join(r.ROOT, "platform", "violet", "channel", "fs", "fat16.c"),
    os.path.join(r.ROOT, "platform", "violet", "channel", "core", "log.c"),
    os.path.join(r.ROOT, "tests", "vqeaf_fat16_path_test.c"),
]


def main():
    if not os.path.isfile(r.ZIG):
        raise SystemExit("test_fat16_path: zig not found at %s" % r.ZIG)
    os.makedirs(OUT, exist_ok=True)
    objects = []
    for src in SOURCES:
        obj = os.path.join(OUT, os.path.basename(src).replace(".", "_") + ".o")
        cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu"] + r.CFLAGS + \
              r.INCLUDES + ["-c", src, "-o", obj]
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        if proc.returncode != 0:
            print(proc.stderr or proc.stdout, file=sys.stderr)
            raise SystemExit("test_fat16_path: compile FAILED %s" % src)
        objects.append(obj)
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu", "-o", EXE] + objects
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        raise SystemExit("test_fat16_path: link FAILED")
    proc = subprocess.run([EXE], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    if proc.returncode == 0:
        print("test_fat16_path: ALL OK")
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())