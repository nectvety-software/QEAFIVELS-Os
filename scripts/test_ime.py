#!/usr/bin/env python3
"""Compile and run the host gate for the responsive independent IME.

Links the real vqeaf_ime.c + vqeaf_theme.c + the rasterised Ubuntu faces
against the same LVGL subset the shell uses, then runs tests/vqeaf_ime.c.
Proves the pure responsive metrics across phone/tablet/fold geometries and the
five adaptive modes, plus the independent top-layer window, layout_weight key
stretching, adjustResize insets sync and the real key->textarea engine.
No device state, no native execution.
"""
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_phone_ui as r  # noqa: E402

ROOT = r.ROOT
UI = r.UI
OUT = os.path.join(ROOT, "build", "ime")
OBJDIR = os.path.join(OUT, "obj")
EXE = os.path.join(OUT, "vqeaf-ime.exe")
TEST = os.path.join(ROOT, "tests", "vqeaf_ime.c")

LVGL = os.path.join(ROOT, "platform", "lvgl")
FONTS = os.path.join(UI, "fonts")
THEME = os.path.join(UI, "vqeaf_theme.c")
SETTINGS = os.path.join(ROOT, "system", "core", "settings.c")
IME = os.path.join(UI, "vqeaf_ime.c")

WALLPAPER = os.path.join(UI, "wallpaper.c")
SOURCES = [s for s in r.SOURCES
           if s.startswith(LVGL) or s.startswith(FONTS) or s == THEME
           or s == SETTINGS or s == WALLPAPER]
SOURCES.append(IME)
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
        print("test_ime: FAILED %s" % src, file=sys.stderr)
        print(err, file=sys.stderr)
    if failures:
        raise SystemExit(1)

    objects.sort()
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu", "-o", EXE] + objects
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        raise SystemExit("test_ime: link FAILED")

    run = subprocess.run([EXE], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    sys.stdout.write(run.stdout)
    if run.returncode != 0:
        sys.stderr.write(run.stderr)
        raise SystemExit("test_ime: FAILED")
    print("test_ime: ALL OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())