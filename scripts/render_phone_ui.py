#!/usr/bin/env python3
"""Render the VQEAF violet phone shell to PNG with a host LVGL build.

The pages are the real platform/violet/ui sources; only vqeaf_state.c is
replaced by tests/vqeaf_ui_host.c, which supplies a fixed provider set instead
of reading sysfs. Two profiles are rendered:

  unknown   - no clock, battery or Wi-Fi hardware reporting (the truthful
              state of a device without those providers)
  providers - every value the shell can display

Both 720x1560 portrait and 1560x720 landscape are rendered for the nine
shell pages (including the keyboard hub and its theme gallery). The script fails
if an image is missing, uniformly blank, or if two pages render identically
(which would mean the page switch did not build anything).

Output: build/screens/phone-ui/*.png
"""
import os
import struct
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
import zlib
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEAF = os.environ.get("QEAFDROPOS") or "D:\\QeafivelsOfficial\\QeafdropOs"
ZIG = os.environ.get("ZIG") or os.path.join(
    QEAF, "out", "toolchain", "zig-windows-x86_64-0.13.0", "zig.exe")
UI = os.path.join(ROOT, "platform", "violet", "ui")
OUTDIR = os.path.abspath(os.environ.get(
    "VQEAF_RENDER_OUTDIR",
    os.path.join(ROOT, "build", "screens", "phone-ui")))
OBJDIR = os.path.join(ROOT, "build", "ui-host", "obj")
EXE = os.path.join(ROOT, "build", "ui-host", "vqeaf-ui-host.exe")

PAGES = ["lock", "home", "home-page-2", "apps", "apps-edit", "apps-edit-page-2",
         "control", "settings", "storage", "about",
         "keyboard", "keyboard-themes", "wifi", "drivers", "sync", "lock-theme", "files",
         "installer", "qeapp", "qeapp-native", "video", "music",
         "ntfs-search", "task-manager", "disk-cleanup", "terminal",
         "security", "wifi-audit", "advisories", "advisories-search", "advisory-detail",
         "m3"]
# The .qeapp installer overlay states, rendered over the FILES page.
INSTALL_STATES = ["install-confirm", "install-busy", "install-done",
                  "install-error", "install-session"]
# Bold-text accessibility mode renders of real pages (weight system).
BOLD_STATES = ["settings-bold", "home-bold"]
# Independent IME window rendered over the real Notes page in each adaptive mode.
IME_STATES = ["ime-normal", "ime-split", "ime-one-hand", "ime-floating",
              "ime-extract", "ime-emoji"]
# Music app surfaces beyond the plain player.
MUSIC_STATES = ["music-canvas", "music-queue"]
# Control Center with an injected real playback session.
MEDIA_STATES = ["control-media-audio"]
# Windows 11 (Fluent) skin: the pages the skin changes, rendered with it on.
FLUENT_STATES = ["lock-fluent", "home-fluent", "apps-fluent", "settings-fluent"]
# M3 reference page scrolled to its reusable Liquid Glass showcase.
M3_STATES = ["m3-liquid", "m3-liquid-overlay", "m3-liquid-actions"]
# First-boot setup steps, captured with the completion marker cleared.
SETUP_STATES = ["setup-welcome", "setup-security", "setup-network",
                "setup-appearance", "setup-ready"]
ALL_STATES = (PAGES + INSTALL_STATES + BOLD_STATES + IME_STATES + MUSIC_STATES +
              MEDIA_STATES + FLUENT_STATES + M3_STATES + SETUP_STATES)
GEOMETRIES = [(720, 1560), (1560, 720)]
PROFILES = ["unknown", "providers"]

SOURCES = [
    os.path.join(UI, "vqeaf_ui.c"),
    os.path.join(UI, "vqeaf_shutdown.c"),
    os.path.join(UI, "vqeaf_pages.c"),
    os.path.join(UI, "vqeaf_theme.c"),
    os.path.join(UI, "vqeaf_components.c"),
    os.path.join(UI, "vqeaf_drag.c"),
    os.path.join(UI, "vqeaf_sound.c"),
    os.path.join(UI, "sounds", "vqeaf_sound_data.c"),
    os.path.join(UI, "vqeaf_ime.c"),
    os.path.join(UI, "vqeaf_wifi.c"),
    os.path.join(UI, "vqeaf_security.c"),
    os.path.join(UI, "vqeaf_files.c"),
    os.path.join(UI, "vqeaf_sys_tools.c"),
    os.path.join(UI, "vqeaf_tools_ui.c"),
    os.path.join(UI, "vqeaf_media.c"),
    os.path.join(UI, "vqeaf_video.c"),
    os.path.join(UI, "vqeaf_qeapp.c"),
    os.path.join(ROOT, "platform", "violet", "qeapp_package.c"),
    os.path.join(ROOT, "system", "core", "settings.c"),
    os.path.join(ROOT, "system", "core", "clipboard.c"),
    os.path.join(UI, "icons", "vqeaf_app_icons.c"),
    os.path.join(UI, "fonts", "vqeaf_font_20.c"),
    os.path.join(UI, "fonts", "vqeaf_font_24.c"),
    os.path.join(UI, "fonts", "vqeaf_font_28.c"),
    os.path.join(UI, "fonts", "vqeaf_font_34.c"),
    os.path.join(UI, "fonts", "vqeaf_emoji_28.c"),
    os.path.join(UI, "fonts", "vqeaf_font_bold_20.c"),
    os.path.join(UI, "fonts", "vqeaf_font_bold_24.c"),
    os.path.join(UI, "fonts", "vqeaf_font_bold_28.c"),
    os.path.join(UI, "fonts", "vqeaf_font_bold_34.c"),
    os.path.join(UI, "fonts", "vqeaf_font_bold_56.c"),
    os.path.join(UI, "fonts", "vqeaf_font_bold_96.c"),
    os.path.join(UI, "fonts", "vqeaf_font_bold_160.c"),
    os.path.join(UI, "fonts", "vqeaf_icon_28.c"),
    os.path.join(UI, "fonts", "vqeaf_icon_40.c"),
    os.path.join(UI, "fonts", "vqeaf_emoji_28.c"),
    os.path.join(UI, "wallpaper.c"),
    os.path.join(ROOT, "tests", "vqeaf_ui_host.c"),
]

# Same LVGL subset the initramfs builds: no drivers, no optional libraries.
LVGL_SRC = os.path.join(ROOT, "platform", "lvgl", "src")
for _dirpath, _dirs, _filenames in os.walk(LVGL_SRC):
    _rel = os.path.relpath(_dirpath, LVGL_SRC).split(os.sep)
    if _rel[0] == "drivers":
        continue
    if _rel[0] == "libs" and (len(_rel) < 2 or _rel[1] not in ("bin_decoder", "lodepng", "tjpgd")):
        continue
    if _rel[0] == "others" and (len(_rel) < 2 or _rel[1] not in ("observer", "sysmon")):
        continue
    if _rel[0] == "draw" and len(_rel) > 1 and _rel[1] not in ("sw", "convert"):
        continue
    if _rel[0] == "osal" and os.path.basename(_dirpath) != "osal":
        continue
    for _filename in _filenames:
        if _filename.endswith(".c"):
            SOURCES.append(os.path.join(_dirpath, _filename))

INCLUDES = [
    "-I", os.path.join(ROOT, "platform", "violet"),
    "-I", os.path.join(ROOT, "platform", "violet", "channel"),
    "-I", UI,
    "-I", os.path.join(UI, "fonts"),
    "-I", os.path.join(ROOT, "platform", "lvgl"),
    "-I", os.path.join(ROOT, "platform", "lvgl", "src"),
    "-I", os.path.join(ROOT, "framework", "application_api"),
    "-I", os.path.join(ROOT, "system", "ui", "include"),
    "-I", os.path.join(ROOT, "system", "core", "include"),
]
CFLAGS = ["-O1", "-g", "-Wall", "-Wextra", "-std=c11",
          "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_LVGL_H_INCLUDE_SIMPLE"]


def compile_one(src):
    rel = os.path.relpath(src, ROOT)
    obj = os.path.join(OBJDIR, rel.replace(os.sep, "_").replace(".", "_") + ".o")
    if os.path.isfile(obj) and os.path.getmtime(obj) >= os.path.getmtime(src):
        return obj, None
    os.makedirs(OBJDIR, exist_ok=True)
    cmd = [ZIG, "cc", "-target", "x86_64-windows-gnu"] + CFLAGS + INCLUDES + \
          ["-c", src, "-o", obj]
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        return src, (proc.stderr or proc.stdout)
    return obj, None


def build():
    if not os.path.isfile(ZIG):
        raise SystemExit("render_phone_ui: zig not found at %s (set ZIG=...)" % ZIG)
    objects = []
    failures = []
    with ThreadPoolExecutor(max_workers=8) as pool:
        futures = {pool.submit(compile_one, src): src for src in SOURCES}
        for future in as_completed(futures):
            result, err = future.result()
            if err is None:
                objects.append(result)
            else:
                failures.append((futures[future], err))
    for src, err in failures:
        print("render_phone_ui: FAILED %s" % src, file=sys.stderr)
        print(err, file=sys.stderr)
    if failures:
        raise SystemExit(1)

    objects.sort()
    cmd = [ZIG, "cc", "-target", "x86_64-windows-gnu", "-o", EXE] + objects
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        raise SystemExit("render_phone_ui: link FAILED")
    print("render_phone_ui: built %s (%d objects)" % (EXE, len(objects)))


def read_ppm(path):
    with open(path, "rb") as handle:
        data = handle.read()
    parts = data.split(b"\n", 3)
    if len(parts) != 4 or parts[0] != b"P6":
        raise SystemExit("render_phone_ui: %s is not a P6 PPM" % path)
    width, height = (int(v) for v in parts[1].split())
    rgb = parts[3]
    if len(rgb) != width * height * 3:
        raise SystemExit("render_phone_ui: %s has %d rgb bytes, expected %d"
                         % (path, len(rgb), width * height * 3))
    return width, height, rgb


def write_png(path, width, height, rgb):
    stride = width * 3
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def distinct_colours(rgb, width, height):
    seen = set()
    for y in range(0, height, 11):
        base = y * width * 3
        for x in range(0, width, 11):
            i = base + x * 3
            seen.add(rgb[i:i + 3])
    return len(seen)


def main():
    build()
    os.makedirs(OUTDIR, exist_ok=True)
    produced = {}
    for width, height in GEOMETRIES:
        for profile in PROFILES:
            cmd = [EXE, OUTDIR, "--width", str(width), "--height", str(height),
                   "--profile", profile]
            proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
            if proc.returncode != 0:
                print(proc.stdout, file=sys.stderr)
                print(proc.stderr, file=sys.stderr)
                raise SystemExit("render_phone_ui: renderer FAILED (%dx%d %s)"
                                 % (width, height, profile))

    total = 0
    for width, height in GEOMETRIES:
        hashes = {}
        for profile in PROFILES:
            for page in ALL_STATES:
                ppm = os.path.join(OUTDIR, "%s-%dx%d-%s.ppm"
                                   % (page, width, height, profile))
                if not os.path.isfile(ppm):
                    raise SystemExit("render_phone_ui: missing %s" % ppm)
                w, h, rgb = read_ppm(ppm)
                if (w, h) != (width, height):
                    raise SystemExit("render_phone_ui: %s is %dx%d" % (ppm, w, h))
                colours = distinct_colours(rgb, w, h)
                # IME frames are partial-screen overlays (extract mode is a
                # large flat editor above the keys), so they are legitimately
                # sparser than a full page; a truly blank frame has 1-3 colours.
                min_colours = 20 if page == "setup-welcome" else (
                    12 if page in IME_STATES else 30)
                if colours < min_colours:
                    raise SystemExit("render_phone_ui: %s looks blank (%d colours)"
                                     % (ppm, colours))
                key = "%s-%s" % (profile, page)
                hashes[key] = zlib.crc32(rgb)
                png = os.path.join(OUTDIR, "%s-%dx%d-%s.png"
                                   % (page, width, height, profile))
                write_png(png, w, h, rgb)
                produced[png] = colours
                total += 1
        # Each frame must differ: an identical frame means the rebuild failed.
        for profile in PROFILES:
            frames = {hashes["%s-%s" % (profile, page)]
                      for page in ALL_STATES}
            if len(frames) != len(ALL_STATES):
                raise SystemExit(
                    "render_phone_ui: %dx%d %s renders duplicate pages"
                    % (width, height, profile))
        print("render_phone_ui: %dx%d ok (%d frames x %d profiles)"
              % (width, height, len(ALL_STATES), len(PROFILES)))

    print("render_phone_ui: wrote %d PNGs to %s" % (total, os.path.relpath(OUTDIR, ROOT)))
    for path in sorted(produced)[:3]:
        print("  %s (%d sampled colours)" % (os.path.relpath(path, ROOT),
                                             produced[path]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
