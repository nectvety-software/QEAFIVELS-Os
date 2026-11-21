#!/usr/bin/env python3
"""Package raw RGB565 bytes into a C lv_image_dsc_t for the violet shell."""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "build", "violet", "wallpaper.rgb565")
OUT = os.path.join(ROOT, "platform", "violet", "ui", "wallpaper.c")
HEAD = os.path.join(ROOT, "platform", "violet", "ui", "wallpaper.h")
W, H = 720, 1560

data = open(RAW, "rb").read()
assert len(data) == W * H * 2, (len(data), W * H * 2)

lines = [
    "/* SPDX-License-Identifier: MIT */",
    "/* Generated from a real photo by scripts/make_wallpaper.ps1 + make_wallpaper_c.py.",
    " * 720x1560 RGB565, exactly the violet UI canvas. Do not edit by hand. */",
    '#include "wallpaper.h"',
    "",
    "static const uint8_t qeaf_wallpaper_data[] = {",
]
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    lines.append("    " + ",".join(str(b) for b in chunk) + ",")
lines += [
    "};",
    "",
    "const lv_image_dsc_t qeaf_wallpaper = {",
    "    .header = {",
    "        .magic = LV_IMAGE_HEADER_MAGIC,",
    "        .cf = LV_COLOR_FORMAT_RGB565,",
    "        .w = %d, .h = %d, .stride = %d," % (W, H, W * 2),
    "    },",
    "    .data = qeaf_wallpaper_data,",
    "    .data_size = sizeof(qeaf_wallpaper_data),",
    "};",
]
open(OUT, "w", encoding="ascii", newline="\n").write("\n".join(lines) + "\n")
open(HEAD, "w", encoding="ascii", newline="\n").write(
    "/* Generated wallpaper descriptor; do not edit. */\n"
    "#ifndef VQEAF_WALLpaper_H\n#define VQEAF_WALLpaper_H\n"
    "#include \"lvgl.h\"\n"
    "extern const lv_image_dsc_t qeaf_wallpaper;\n"
    "#endif\n")
print("wrote %s (%d bytes data, %d lines)" % (OUT, len(data), len(lines)))
