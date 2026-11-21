#!/usr/bin/env python3
"""Build and run the violet shell interaction test (tap / long-press contract).

Reuses render_phone_ui's source list and compile cache, swapping the render
harness for tests/vqeaf_shell_interaction.c. The test drives a real LVGL
pointer indev, asserts the info-sheet / dock-move behaviour and dumps PPM
snapshots; this script converts them to PNG next to the page renders.
"""
import glob
import os
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_phone_ui as r  # noqa: E402

TEST = os.path.join(r.ROOT, "tests", "vqeaf_shell_interaction.c")
EXE = os.path.join(r.ROOT, "build", "ui-host", "vqeaf-shell-interact.exe")


def main():
    sources = [s for s in r.SOURCES
               if os.path.basename(s) != "vqeaf_ui_host.c"] + [TEST]
    objects = []
    failures = []
    with ThreadPoolExecutor(max_workers=8) as pool:
        futures = {pool.submit(r.compile_one, s): s for s in sources}
        for future in as_completed(futures):
            obj, err = future.result()
            if err is None:
                objects.append(obj)
            else:
                failures.append((futures[future], err))
    for src, err in failures:
        print("test_violet_shell: FAILED %s" % src, file=sys.stderr)
        print(err, file=sys.stderr)
    if failures:
        return 1
    objects.sort()
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu"] + r.CFLAGS + \
          ["-o", EXE] + objects
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        return 1
    print("test_violet_shell: built %s" % EXE)

    os.makedirs(r.OUTDIR, exist_ok=True)
    # Start from an empty snapshot set. A PPM left by an older run (for a
    # snapshot the test no longer takes) would otherwise be converted again and
    # keep publishing a PNG of an old UI next to the fresh ones.
    for stale in (glob.glob(os.path.join(r.OUTDIR, "interaction-*.ppm")) +
                  glob.glob(os.path.join(r.OUTDIR, "interaction-*.png"))):
        os.remove(stale)
    proc = subprocess.run([EXE, r.OUTDIR], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    if proc.returncode != 0:
        return 1

    for name in ("info-app", "info-qeapp", "info-dock", "move-armed",
                 "move-done", "view-png", "view-jpg", "view-bmp", "view-mp3",
                 "rescue-root", "rescue-text", "rescue-back",
                 "restart", "poweroff-refused", "swipe-right-apps",
                 "keyboard-themes", "settings-timeout",
                 "settings-dynamic-auto", "material-ripple",
                 "material-components", "material-light",
                 "material-dialog", "material-snackbar",
                 "keyboard-theme-applied", "settings-usb",
                 "chrome-home", "chrome-settings", "chrome-native",
                 "notes-ime-typing", "notes-ime-special", "search-ime-typing", "lock-widget", "apps-edit",
                 "control-tiles", "control-edit", "power-menu-2x2",
                 "music-player", "music-more", "music-play-snack",
                 "music-next", "music-canvas", "music-queue",
                 "music-queue-reorder", "music-queue-removed",
                 "m3-layout", "m3-layout-dialog", "recents-carousel",
                 "recents-empty", "usb-sheet"):
        ppm = os.path.join(r.OUTDIR, "interaction-%s.ppm" % name)
        if not os.path.isfile(ppm):
            print("test_violet_shell: missing %s" % ppm, file=sys.stderr)
            return 1

    # Convert every snapshot this run produced, not only the names above.
    # The tuple exists to catch a snapshot that silently stopped being taken;
    # using it as the conversion list meant a snapshot added to the test kept
    # a stale PNG (or none at all) until someone converted it by hand.
    converted = 0
    for ppm in sorted(glob.glob(os.path.join(r.OUTDIR, "interaction-*.ppm"))):
        w, h, rgb = r.read_ppm(ppm)
        r.write_png(ppm[:-4] + ".png", w, h, rgb)
        converted += 1
    if converted == 0:
        print("test_violet_shell: no snapshots to convert", file=sys.stderr)
        return 1
    print("test_violet_shell: %d snapshots converted" % converted)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())