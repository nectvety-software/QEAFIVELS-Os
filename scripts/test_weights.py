#!/usr/bin/env python3
"""Compile and run the host gate for the VQEAF weight system.

Links the real vqeaf_theme.c + the rasterised Ubuntu regular/bold faces against
the same LVGL subset the shell uses, then runs tests/vqeaf_weights.c. Proves the
typography weights (distinct faces, bold-text mode, more ink) and the layout
weights (proportional row/column split, weight-0 keeps content) with real LVGL
layout and real glyph rasterisation. No device state, no native execution.
"""
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_phone_ui as r  # noqa: E402

ROOT = r.ROOT
UI = r.UI
OUT = os.path.join(ROOT, "build", "weights")
OBJDIR = os.path.join(OUT, "obj")
EXE = os.path.join(OUT, "vqeaf-weights.exe")
TEST = os.path.join(ROOT, "tests", "vqeaf_weights.c")

LVGL = os.path.join(ROOT, "platform", "lvgl")
FONTS = os.path.join(UI, "fonts")
THEME = os.path.join(UI, "vqeaf_theme.c")
SETTINGS = os.path.join(ROOT, "system", "core", "settings.c")

SOURCES = [s for s in r.SOURCES
           if s.startswith(LVGL) or s.startswith(FONTS) or s == THEME
           or s == SETTINGS]
SOURCES.append(TEST)


def compile_one(src):
    rel = os.path.relpath(src, ROOT)
    obj = os.path.join(OBJDIR, rel.replace(os.sep, "_").replace(".", "_") + ".o")
    if os.path.isfile(obj) and os.path.getmtime(obj) >= os.path.getmtime(src):
        return obj, None
    os.makedirs(OBJDIR, exist_ok=True)
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu"] + r.CFLAGS + \
          r.INCLUDES + ["-c", src, "-o", obj]
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        return src, (proc.stderr or proc.stdout)
    return obj, None


def main():
    objects = []
    failures = []
    with ThreadPoolExecutor(max_workers=8) as pool:
        futures = {pool.submit(compile_one, s): s for s in SOURCES}
        for future in as_completed(futures):
            obj, err = future.result()
            if err is None:
                objects.append(obj)
            else:
                failures.append((futures[future], err))
    for src, err in failures:
        print("test_weights: FAILED %s" % src, file=sys.stderr)
        print(err, file=sys.stderr)
    if failures:
        raise SystemExit(1)

    objects.sort()
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu", "-o", EXE] + objects
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        raise SystemExit("test_weights: link FAILED")

    run = subprocess.run([EXE], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    sys.stdout.write(run.stdout)
    if run.returncode != 0:
        sys.stderr.write(run.stderr)
        raise SystemExit("test_weights: FAILED")
    print("test_weights: ALL OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())