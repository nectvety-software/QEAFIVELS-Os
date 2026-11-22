#!/usr/bin/env python3
"""Build the QEAF PID 1 for MT6891/MT6893 (chopin / ares).

Same layout as scripts/build_violet.py: a single static aarch64-musl ELF that
is /init. The MediaTek platform reuses the violet DRM/KMS backend, scaler,
rescue channel, USB LUN and LVGL shell unchanged; only the board layer
(display geometry, capability census, UFS/firmware bring-up, Denial session)
comes from platform/mt689x.

Output: build/mt689x/vqeaf-init
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEAF = os.environ.get("QEAFDROPOS") or "D:\\QeafivelsOfficial\\QeafdropOs"
ZIG = os.environ.get("ZIG") or os.path.join(
    QEAF, "out", "toolchain", "zig-windows-x86_64-0.13.0", "zig.exe")
TARGET = os.path.join(ROOT, "build", "mt689x", "vqeaf-init")
OBJDIR = os.path.join(ROOT, "build", "mt689x", "obj")

VIOLET = os.path.join(ROOT, "platform", "violet")
MTK = os.path.join(ROOT, "platform", "mt689x")
CHANNEL = os.path.join(VIOLET, "channel")

# Shared with violet. The board-specific violet sources (main_violet.c,
# display_hal.c, vio_caps.c, wifi_bringup.c, qrtr_ns.c, boot_journal.c) are
# deliberately absent: their MTK counterparts are listed below instead.
SHARED = [
    os.path.join(CHANNEL, "core", "log.c"),
    os.path.join(CHANNEL, "core", "sysio.c"),
    os.path.join(CHANNEL, "fs", "fat16.c"),
    os.path.join(CHANNEL, "fs", "storage.c"),
    os.path.join(CHANNEL, "fs", "rescue.c"),
    os.path.join(VIOLET, "devnodes.c"),
    os.path.join(VIOLET, "drm_kms.c"),
    os.path.join(VIOLET, "panel_scale.c"),
    os.path.join(VIOLET, "splash.c"),
    os.path.join(VIOLET, "usb_lun.c"),
    os.path.join(VIOLET, "fat_ro.c"),
    os.path.join(VIOLET, "violet_input.c"),
    os.path.join(VIOLET, "violet_power.c"),
    os.path.join(VIOLET, "appstore.c"),
    os.path.join(VIOLET, "ui", "vqeaf_ui.c"),
    os.path.join(VIOLET, "ui", "vqeaf_shutdown.c"),
    os.path.join(VIOLET, "ui", "vqeaf_pages.c"),
    os.path.join(VIOLET, "ui", "vqeaf_theme.c"),
    os.path.join(VIOLET, "ui", "vqeaf_ime.c"),
    os.path.join(VIOLET, "ui", "vqeaf_state.c"),
    os.path.join(VIOLET, "ui", "vqeaf_wifi.c"),
    os.path.join(VIOLET, "ui", "vqeaf_files.c"),
    os.path.join(VIOLET, "ui", "vqeaf_sys_tools.c"),
    os.path.join(VIOLET, "ui", "vqeaf_tools_ui.c"),
    os.path.join(VIOLET, "ui", "vqeaf_media.c"),
    os.path.join(VIOLET, "ui", "vqeaf_video.c"),
    os.path.join(VIOLET, "ui", "vqeaf_qeapp.c"),
    os.path.join(VIOLET, "ui", "vqeaf_qeapp_native.c"),
    os.path.join(VIOLET, "ui", "vqeaf_qeapp_runtime.c"),
    os.path.join(VIOLET, "ui", "vqeaf_qeapp_device.c"),
    os.path.join(ROOT, "framework", "security", "qeaf_broker.c"),
    os.path.join(ROOT, "system", "core", "settings.c"),
    os.path.join(ROOT, "system", "core", "capability.c"),
    os.path.join(VIOLET, "ui", "icons", "vqeaf_app_icons.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_font_20.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_font_24.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_font_28.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_font_34.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_font_bold_20.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_font_bold_24.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_font_bold_28.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_font_bold_34.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_font_bold_56.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_font_bold_96.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_icon_28.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_icon_40.c"),
    os.path.join(VIOLET, "ui", "fonts", "vqeaf_emoji_28.c"),
    os.path.join(VIOLET, "ui", "wallpaper.c"),
    # Shared rescue verbs expose QRTR/host-service diagnostics; link providers
    # on the MTK board too (inert unless a rescue verb invokes them).
    os.path.join(VIOLET, "qrtr_ns.c"),
    os.path.join(VIOLET, "host_services.c"),
]

MTK_SOURCES = [
    os.path.join(MTK, "mt689x_display.c"),
    os.path.join(MTK, "mt689x_caps.c"),
    os.path.join(MTK, "mt689x_bringup.c"),
    os.path.join(MTK, "mt689x_wifi.c"),
    os.path.join(MTK, "mt689x_session.c"),
    os.path.join(MTK, "main_mt689x.c"),
]

SOURCES = SHARED + MTK_SOURCES

LVGL_SRC = os.path.join(ROOT, "platform", "lvgl", "src")
for dirpath, _dirs, filenames in os.walk(LVGL_SRC):
    rel = os.path.relpath(dirpath, LVGL_SRC).split(os.sep)
    if rel[0] == "drivers":
        continue
    if rel[0] == "libs" and (len(rel) < 2 or rel[1] not in ("bin_decoder", "lodepng", "tjpgd")):
        continue
    if rel[0] == "others" and (len(rel) < 2 or rel[1] not in ("observer", "sysmon")):
        continue
    if rel[0] == "draw" and len(rel) > 1 and rel[1] not in ("sw", "convert"):
        continue
    if rel[0] == "osal" and os.path.basename(dirpath) != "osal":
        continue
    for filename in filenames:
        if filename.endswith(".c"):
            SOURCES.append(os.path.join(dirpath, filename))

INCLUDES = [
    "-I", MTK,
    "-I", VIOLET,
    "-I", CHANNEL,
    "-I", os.path.join(CHANNEL, "core"),
    "-I", os.path.join(CHANNEL, "fs"),
    "-I", os.path.join(CHANNEL, "hal", "include"),
    "-I", os.path.join(ROOT, "system", "ui"),
    "-I", os.path.join(ROOT, "system", "ui", "include"),
    "-I", os.path.join(ROOT, "platform", "lvgl"),
    "-I", os.path.join(ROOT, "platform", "lvgl", "src"),
    "-I", os.path.join(VIOLET, "ui"),
    "-I", os.path.join(VIOLET, "ui", "fonts"),
    "-I", os.path.join(ROOT, "framework", "application_api"),
    "-I", os.path.join(ROOT, "framework", "security"),
    "-I", os.path.join(ROOT, "system", "core", "include"),
]

CFLAGS = ["-O2", "-g", "-Wall", "-Wextra", "-std=c11",
          "-D_GNU_SOURCE", "-DQEAF_OS_STRING_PLATFORM",
          "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_LVGL_H_INCLUDE_SIMPLE"]


def newest_header_mtime():
    newest = 0
    it = iter(INCLUDES)
    for inc in it:
        if inc != "-I":
            continue
        base = next(it, None)
        if base and os.path.isdir(base):
            for dirpath, _dirs, files in os.walk(base):
                for name in files:
                    if name.endswith(".h"):
                        newest = max(newest, os.path.getmtime(
                            os.path.join(dirpath, name)))
    return newest


def compile_one(src, verbose, header_mtime):
    rel = os.path.relpath(src, ROOT)
    base = rel.replace(os.sep, "_").replace(".", "_")
    obj = os.path.join(OBJDIR, base + ".o")
    if os.path.isfile(obj) and \
       os.path.getmtime(obj) >= max(os.path.getmtime(src), header_mtime):
        return obj, None
    cmd = [ZIG, "cc", "-target", "aarch64-linux-musl"] + CFLAGS + INCLUDES + \
          ["-c", src, "-o", obj]
    os.makedirs(OBJDIR, exist_ok=True)
    if verbose:
        print(" ".join(cmd))
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        return src, proc.stderr or proc.stdout
    return obj, None


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    if not os.path.isfile(ZIG):
        print("build_mt689x: zig not found at %s (set ZIG=...)" % ZIG,
              file=sys.stderr)
        return 2

    if args.clean:
        shutil.rmtree(OBJDIR, ignore_errors=True)
        if os.path.isfile(TARGET):
            os.remove(TARGET)

    os.makedirs(OBJDIR, exist_ok=True)
    failures = []
    objects = []
    header_mtime = newest_header_mtime()
    with ThreadPoolExecutor(max_workers=8) as pool:
        futures = {pool.submit(compile_one, src, args.verbose, header_mtime): src
                   for src in SOURCES}
        for future in as_completed(futures):
            result, err = future.result()
            if err is None:
                objects.append(result)
            else:
                failures.append((futures[future], err))

    for src, err in failures:
        print("build_mt689x: FAILED %s" % src, file=sys.stderr)
        print(err, file=sys.stderr)
    if failures:
        return 1

    objects.sort()
    cmd = [ZIG, "cc", "-target", "aarch64-linux-musl", "-static",
           "-o", TARGET] + objects
    print("build_mt689x: linking %d objects" % len(objects))
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        return 1

    with open(TARGET, "rb") as handle:
        head = handle.read(20)
    if head[:4] != b"\x7fELF" or head[4] != 2 or \
       struct.unpack_from("<H", head, 18)[0] != 0xB7:
        print("build_mt689x: output is not an aarch64 ELF", file=sys.stderr)
        return 1
    print("build_mt689x: %s (%d bytes)" % (TARGET, os.path.getsize(TARGET)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
