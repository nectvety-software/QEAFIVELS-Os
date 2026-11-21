#!/usr/bin/env python3
"""Compile and run the shell video page core test.

Generates the real clip.qvf fixture (same generator as the host media tree),
compiles platform/violet/ui/vqeaf_video.c against it, and asserts the scan
classification, the honest failure codes, and byte-exact frame decodes
against Python-decoded reference dumps.
"""
import os
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_phone_ui as r  # noqa: E402

ROOT = r.ROOT
OUT = os.path.join(ROOT, "build", "media-fixtures")
TEST = os.path.join(ROOT, "tests", "vqeaf_video_test.c")
SOURCES = [
    os.path.join(ROOT, "platform", "violet", "ui", "vqeaf_video.c"),
    os.path.join(ROOT, "platform", "violet", "ui", "vqeaf_files.c"),
]
EXE = os.path.join(OUT, "vqeaf-video-test.exe")


def compile_and_run():
    import gen_host_media
    rc = gen_host_media.main()
    if rc != 0:
        raise SystemExit("test_vqeaf_video: fixture generation FAILED")
    objects = []
    flags = r.CFLAGS + r.INCLUDES
    for src in SOURCES + [TEST]:
        obj = os.path.join(OUT, os.path.basename(src).replace(".", "_") + ".o")
        cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu"] + flags + \
              ["-c", src, "-o", obj]
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        if proc.returncode != 0:
            print(proc.stderr or proc.stdout, file=sys.stderr)
            raise SystemExit("test_vqeaf_video: compile FAILED %s" % src)
        objects.append(obj)
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu", "-o", EXE] + objects
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        raise SystemExit("test_vqeaf_video: link FAILED")
    proc = subprocess.run([EXE, OUT], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    return proc.returncode


def main():
    if not os.path.isfile(r.ZIG):
        raise SystemExit("test_vqeaf_video: zig not found at %s" % r.ZIG)
    rc = compile_and_run()
    if rc == 0:
        print("test_vqeaf_video: ALL OK")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
