#!/usr/bin/env python3
"""Draw and embed the original QEAF 32x32 VPEPixel system-icon set.

The editable .vpe files are the artwork source of truth. Running with
--redraw recreates the original drawings, while the default mode only exports
the checked-in ARGB8888 C descriptors with transparent corners. VPEPixel
supplies the VPE565 codec; no VPEPixel artwork or generator code is copied into
this repository.
"""
from __future__ import annotations

import argparse
import math
import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSET_DIR = ROOT / "assets" / "icons" / "vqeaf_os_pixel"
OUT_C = ROOT / "platform" / "violet" / "ui" / "icons" / "vqeaf_app_icons.c"
SIZE = 32


def rgb(hex_color: str) -> tuple[int, int, int]:
    value = hex_color.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def rgb_to_565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def load_vpe_module(vpe_root: Path):
    sys.path.insert(0, str(vpe_root))
    from vpx_editor import vpe
    return vpe


class Canvas:
    def __init__(self, fill: int):
        self.px = [fill] * (SIZE * SIZE)

    def set(self, x: int, y: int, color: int) -> None:
        if 0 <= x < SIZE and 0 <= y < SIZE:
            self.px[y * SIZE + x] = color

    def rect(self, x: int, y: int, w: int, h: int, color: int) -> None:
        for yy in range(max(0, y), min(SIZE, y + h)):
            for xx in range(max(0, x), min(SIZE, x + w)):
                self.px[yy * SIZE + xx] = color

    def frame(self, x: int, y: int, w: int, h: int, color: int,
              thickness: int = 1) -> None:
        self.rect(x, y, w, thickness, color)
        self.rect(x, y + h - thickness, w, thickness, color)
        self.rect(x, y, thickness, h, color)
        self.rect(x + w - thickness, y, thickness, h, color)

    def line(self, x0: int, y0: int, x1: int, y1: int, color: int,
             width: int = 1) -> None:
        dx, sx = abs(x1 - x0), 1 if x0 < x1 else -1
        dy, sy = -abs(y1 - y0), 1 if y0 < y1 else -1
        err = dx + dy
        half = width // 2
        while True:
            self.rect(x0 - half, y0 - half, width, width, color)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def poly(self, points: list[tuple[int, int]], color: int) -> None:
        if not points:
            return
        for y in range(min(p[1] for p in points), max(p[1] for p in points) + 1):
            crossings: list[float] = []
            for i, a in enumerate(points):
                b = points[(i + 1) % len(points)]
                if (a[1] <= y < b[1]) or (b[1] <= y < a[1]):
                    crossings.append(a[0] + (y - a[1]) * (b[0] - a[0]) /
                                     (b[1] - a[1]))
            crossings.sort()
            for i in range(0, len(crossings) - 1, 2):
                left = int(round(crossings[i]))
                right = int(round(crossings[i + 1]))
                self.rect(left, y, right - left + 1, 1, color)

    def outline(self, points: list[tuple[int, int]], color: int,
                width: int = 1) -> None:
        for i, a in enumerate(points):
            b = points[(i + 1) % len(points)]
            self.line(a[0], a[1], b[0], b[1], color, width)

    def ellipse(self, cx: int, cy: int, rx: int, ry: int, color: int,
                outline: int | None = None) -> None:
        for y in range(-ry, ry + 1):
            span = int(round(rx * math.sqrt(max(0.0, 1.0 - (y / ry) ** 2))))
            self.rect(cx - span, cy + y, span * 2 + 1, 1, color)
        if outline is not None:
            for y in range(-ry, ry + 1):
                span = int(round(rx * math.sqrt(max(0.0, 1.0 - (y / ry) ** 2))))
                self.set(cx - span, cy + y, outline)
                self.set(cx + span, cy + y, outline)
            for x in range(-rx, rx + 1):
                span = int(round(ry * math.sqrt(max(0.0, 1.0 - (x / rx) ** 2))))
                self.set(cx + x, cy - span, outline)
                self.set(cx + x, cy + span, outline)

    def path(self, points: list[tuple[int, int]], color: int,
             width: int = 1) -> None:
        for a, b in zip(points, points[1:]):
            self.line(a[0], a[1], b[0], b[1], color, width)


def unpack(color: int) -> tuple[int, int, int]:
    return ((color >> 11) & 31) * 255 // 31, \
           ((color >> 5) & 63) * 255 // 63, \
           (color & 31) * 255 // 31


def mix(a: int, b: int, amount: float) -> int:
    ar, ag, ab = unpack(a)
    br, bg, bb = unpack(b)
    return rgb_to_565(round(ar * amount + br * (1.0 - amount)),
                      round(ag * amount + bg * (1.0 - amount)),
                      round(ab * amount + bb * (1.0 - amount)))


BADGE_OUTER = [(7, 1), (24, 1), (28, 4), (30, 7), (30, 24),
               (27, 28), (24, 30), (7, 30), (4, 27), (1, 24),
               (1, 7), (4, 4)]
BADGE_INNER = [(7, 3), (23, 3), (26, 6), (28, 9), (28, 22),
               (25, 26), (22, 28), (9, 28), (6, 25), (3, 22),
               (3, 9), (6, 6)]


def badge(c: Canvas, accent: int) -> None:
    outer_mask = Canvas(0)
    inner_mask = Canvas(0)
    outer_mask.poly(BADGE_OUTER, 1)
    inner_mask.poly(BADGE_INNER, 1)
    top = mix(accent, LIGHT, 0.22)
    bottom = mix(accent, BG, 0.58)
    for y in range(SIZE):
        amount = y / (SIZE - 1)
        row_color = mix(top, bottom, amount)
        for x in range(SIZE):
            index = y * SIZE + x
            if outer_mask.px[index]: c.px[index] = INK
            if inner_mask.px[index]: c.px[index] = row_color
    c.line(7, 4, 24, 4, mix(accent, LIGHT, 0.52))
    c.line(4, 8, 4, 22, mix(accent, LIGHT, 0.30))
    c.line(25, 27, 27, 25, mix(accent, BG, 0.24))


def draw_home(c: Canvas, a: int, s: int) -> None:
    c.rect(21, 7, 3, 8, INK)
    c.rect(22, 8, 1, 5, mix(a, LIGHT, 0.35))
    body = [(6, 15), (16, 6), (26, 15), (26, 26), (6, 26)]
    c.poly(body, mix(a, INK, 0.42))
    c.outline(body, INK)
    roof = [(5, 15), (16, 5), (27, 15), (25, 17), (16, 8), (7, 17)]
    c.poly(roof, a)
    c.outline(roof, INK)
    c.line(16, 8, 16, 25, mix(a, LIGHT, 0.45))
    c.frame(14, 19, 5, 7, INK)
    c.rect(15, 20, 3, 6, s)
    for x in (9, 20):
        c.frame(x, 17, 3, 3, INK)
        c.rect(x + 1, 18, 1, 1, LIGHT)


def draw_apps(c: Canvas, a: int, s: int) -> None:
    for x, y, color in ((6, 6, a), (18, 6, s), (6, 18, s), (18, 18, a)):
        c.rect(x, y, 8, 8, INK)
        c.rect(x + 1, y + 1, 6, 6, color)
        c.line(x + 2, y + 2, x + 5, y + 2, LIGHT)
    c.rect(9, 9, 2, 2, INK)
    c.rect(21, 9, 2, 2, INK)
    c.rect(9, 21, 2, 2, INK)
    c.rect(21, 21, 2, 2, INK)


def draw_control(c: Canvas, a: int, s: int) -> None:
    for y, knob in ((9, 11), (16, 21), (23, 15)):
        c.line(6, y, 26, y, INK, 3)
        c.line(7, y, 25, y, mix(a, LIGHT, 0.32))
        c.rect(knob - 2, y - 2, 5, 5, INK)
        c.rect(knob - 1, y - 1, 3, 3, s if y == 16 else a)
        c.set(knob - 1, y - 1, LIGHT)


def draw_settings(c: Canvas, a: int, s: int) -> None:
    points = []
    for i in range(32):
        angle = 2 * math.pi * i / 32
        radius = 11 if i % 4 == 0 else 8
        points.append((round(16 + math.cos(angle) * radius),
                       round(16 + math.sin(angle) * radius)))
    c.poly(points, a)
    c.outline(points, INK)
    c.ellipse(16, 16, 5, 5, mix(a, INK, 0.48), INK)
    c.ellipse(16, 16, 2, 2, s)
    c.line(11, 11, 13, 13, LIGHT)


def draw_phone(c: Canvas, a: int, s: int) -> None:
    path = [(9, 8), (11, 13), (16, 19), (21, 16), (23, 10)]
    c.path(path, INK, 7)
    c.path(path, a, 4)
    c.line(10, 9, 12, 13, LIGHT, 1)
    for x, y in ((6, 5), (21, 5)):
        c.rect(x, y, 6, 6, INK)
        c.rect(x + 1, y + 1, 4, 4, s)
        c.rect(x + 1, y + 1, 3, 1, LIGHT)
    c.ellipse(16, 19, 2, 2, mix(a, LIGHT, 0.5))


def draw_messages(c: Canvas, a: int, s: int) -> None:
    bubble = [(7, 6), (25, 6), (27, 8), (27, 20), (25, 22),
              (13, 22), (9, 27), (9, 22), (7, 20), (5, 18), (5, 8)]
    c.poly(bubble, a)
    c.outline(bubble, INK)
    c.line(8, 8, 23, 8, LIGHT)
    for x in (10, 16, 22):
        c.rect(x, 13, 2, 2, INK)
        c.set(x, 13, s)


def draw_contacts(c: Canvas, a: int, s: int) -> None:
    c.rect(6, 5, 20, 22, INK)
    c.rect(7, 6, 18, 20, mix(a, INK, 0.38))
    c.rect(7, 6, 4, 20, a)
    c.line(11, 7, 11, 25, LIGHT)
    c.ellipse(18, 12, 3, 3, s, INK)
    c.poly([(13, 21), (14, 18), (17, 16), (19, 16), (22, 18), (23, 21)], a)
    c.outline([(13, 21), (14, 18), (17, 16), (19, 16), (22, 18), (23, 21)], INK)
    for y in (9, 13, 17):
        c.line(8, y, 9, y, INK)


def draw_browser(c: Canvas, a: int, s: int) -> None:
    c.ellipse(16, 16, 11, 11, mix(a, INK, 0.42), INK)
    c.ellipse(16, 16, 10, 10, mix(a, INK, 0.42), a)
    c.ellipse(16, 16, 5, 10, mix(a, INK, 0.42), LIGHT)
    c.line(7, 16, 25, 16, LIGHT)
    c.path([(8, 11), (12, 9), (20, 9), (24, 11)], s)
    c.path([(8, 21), (12, 23), (20, 23), (24, 21)], s)
    c.ellipse(23, 9, 2, 2, s, INK)


def draw_camera(c: Canvas, a: int, s: int) -> None:
    c.rect(11, 7, 10, 4, INK)
    c.rect(12, 8, 8, 2, a)
    body = [(6, 11), (10, 9), (22, 9), (26, 11), (27, 24),
            (25, 26), (6, 26), (4, 24), (4, 13)]
    c.poly(body, mix(a, INK, 0.40))
    c.outline(body, INK)
    c.line(6, 12, 25, 12, LIGHT)
    c.ellipse(16, 18, 6, 6, INK, a)
    c.ellipse(16, 18, 4, 4, s, LIGHT)
    c.ellipse(15, 17, 1, 1, LIGHT)
    c.frame(23, 13, 3, 2, INK)
    c.rect(24, 13, 1, 1, LIGHT)


def draw_gallery(c: Canvas, a: int, s: int) -> None:
    c.rect(5, 6, 22, 21, INK)
    c.rect(6, 7, 20, 19, a)
    c.rect(8, 9, 16, 15, mix(a, INK, 0.30))
    c.ellipse(20, 12, 2, 2, s)
    c.poly([(8, 23), (14, 15), (18, 20), (21, 17), (24, 23)], LIGHT)
    c.outline([(8, 23), (14, 15), (18, 20), (21, 17), (24, 23)], INK)
    c.rect(8, 23, 16, 2, mix(a, INK, 0.45))


def draw_music(c: Canvas, a: int, s: int) -> None:
    c.line(11, 8, 11, 23, INK, 5)
    c.line(21, 5, 21, 20, INK, 5)
    c.line(11, 8, 11, 23, a, 3)
    c.line(21, 5, 21, 20, s, 3)
    c.poly([(11, 8), (21, 5), (21, 10), (11, 13)], LIGHT)
    c.outline([(11, 8), (21, 5), (21, 10), (11, 13)], INK)
    c.ellipse(8, 23, 4, 2, a, INK)
    c.ellipse(18, 20, 4, 2, s, INK)


def draw_video(c: Canvas, a: int, s: int) -> None:
    c.rect(6, 12, 20, 14, INK)
    c.rect(7, 13, 18, 12, mix(a, INK, 0.38))
    top = [(5, 7), (26, 7), (24, 12), (7, 12)]
    c.poly(top, a)
    c.outline(top, INK)
    for x in (9, 14, 19, 24):
        c.line(x, 8, x - 2, 11, s, 2)
    c.poly([(14, 15), (14, 23), (22, 19)], LIGHT)
    c.outline([(14, 15), (14, 23), (22, 19)], INK)


def draw_files(c: Canvas, a: int, s: int) -> None:
    folder = [(5, 10), (13, 10), (15, 13), (27, 13), (27, 25),
              (24, 27), (5, 27), (3, 25), (3, 13)]
    c.poly(folder, a)
    c.outline(folder, INK)
    c.poly([(5, 11), (13, 11), (15, 14), (26, 14), (26, 16), (5, 16)], LIGHT)
    c.line(5, 18, 25, 18, mix(a, INK, 0.42))
    c.rect(8, 20, 7, 5, s)
    c.line(9, 21, 14, 21, INK)


def draw_store(c: Canvas, a: int, s: int) -> None:
    c.rect(6, 13, 20, 13, INK)
    c.rect(7, 14, 18, 11, mix(a, INK, 0.40))
    for i in range(5):
        color = a if i % 2 == 0 else s
        x = 4 + i * 5
        c.rect(x, 7, 5, 7, INK)
        c.rect(x + 1, 8, 3, 5, color)
        c.ellipse(x + 2, 13, 2, 2, color, INK)
    c.line(5, 7, 27, 7, LIGHT)
    c.frame(13, 18, 6, 7, INK)
    c.rect(14, 19, 4, 6, s)
    c.frame(8, 18, 3, 4, LIGHT)
    c.frame(22, 18, 3, 4, LIGHT)


def draw_games(c: Canvas, a: int, s: int) -> None:
    pad = [(8, 10), (12, 7), (20, 7), (24, 10), (27, 18),
           (25, 24), (20, 25), (16, 21), (12, 25), (7, 24), (5, 18)]
    c.poly(pad, a)
    c.outline(pad, INK)
    c.line(8, 11, 23, 11, LIGHT)
    c.rect(9, 14, 7, 2, INK)
    c.rect(11, 12, 2, 6, INK)
    c.ellipse(22, 14, 2, 2, s, INK)
    c.ellipse(25, 17, 2, 2, LIGHT, INK)
    c.rect(14, 18, 2, 2, INK)
    c.rect(18, 18, 2, 2, INK)


def draw_calculator(c: Canvas, a: int, s: int) -> None:
    c.rect(7, 5, 18, 23, INK)
    c.rect(8, 6, 16, 21, mix(a, INK, 0.38))
    c.frame(10, 8, 12, 6, INK)
    c.rect(11, 9, 10, 4, mix(s, INK, 0.55))
    c.line(18, 10, 20, 10, LIGHT)
    for row in range(3):
        for col in range(3):
            x, y = 10 + col * 5, 17 + row * 4
            c.rect(x, y, 3, 2, INK)
            c.rect(x + 1, y, 1, 1, s if (row + col) % 2 else a)


def draw_recorder(c: Canvas, a: int, s: int) -> None:
    c.line(16, 18, 16, 24, INK, 4)
    c.line(16, 18, 16, 24, a, 2)
    c.line(10, 25, 22, 25, INK, 3)
    c.line(11, 25, 21, 25, s)
    c.ellipse(16, 11, 5, 7, INK)
    c.ellipse(16, 11, 4, 6, a)
    for y in (9, 12, 15):
        c.line(13, y, 19, y, mix(a, INK, 0.45))
    c.line(13, 8, 15, 7, LIGHT)
    c.path([(8, 9), (6, 12), (6, 16), (8, 19)], s)
    c.path([(24, 9), (26, 12), (26, 16), (24, 19)], s)


def draw_calendar(c: Canvas, a: int, s: int) -> None:
    c.rect(6, 8, 20, 19, INK)
    c.rect(7, 9, 18, 17, mix(a, INK, 0.38))
    c.rect(7, 9, 18, 5, a)
    c.line(8, 10, 24, 10, LIGHT)
    for x in (10, 22):
        c.rect(x, 5, 2, 7, INK)
        c.rect(x, 6, 1, 4, s)
    for row in range(3):
        for col in range(4):
            x, y = 9 + col * 4, 17 + row * 3
            c.rect(x, y, 2, 2, s if row == 1 and col == 2 else LIGHT)


def draw_notes(c: Canvas, a: int, s: int) -> None:
    paper = [(7, 5), (21, 5), (26, 10), (26, 27), (7, 27)]
    c.poly(paper, LIGHT)
    c.outline(paper, INK)
    c.poly([(21, 5), (21, 10), (26, 10)], mix(a, LIGHT, 0.45))
    c.outline([(21, 5), (21, 10), (26, 10)], INK)
    for y in (13, 17, 21):
        c.line(10, y, 22, y, mix(a, INK, 0.38))
    c.line(10, 24, 18, 24, s)
    c.line(12, 24, 23, 13, INK, 3)
    c.line(12, 24, 23, 13, a, 1)


def draw_weather(c: Canvas, a: int, s: int) -> None:
    c.ellipse(20, 10, 5, 5, s, INK)
    for x0, y0, x1, y1 in ((20, 3, 20, 5), (20, 15, 20, 17),
                           (13, 10, 15, 10), (25, 10, 27, 10),
                           (15, 5, 17, 7), (23, 13, 25, 15),
                           (25, 5, 23, 7), (15, 15, 17, 13)):
        c.line(x0, y0, x1, y1, s)
    cloud = [(8, 18), (10, 14), (14, 14), (16, 11), (21, 11),
             (24, 15), (25, 20), (23, 24), (10, 24), (7, 21)]
    c.poly(cloud, LIGHT)
    c.outline(cloud, INK)
    c.line(10, 21, 22, 21, mix(a, LIGHT, 0.55))
    c.line(12, 26, 11, 28, a, 2)
    c.line(18, 26, 17, 28, a, 2)


def draw_security(c: Canvas, a: int, s: int) -> None:
    shield = [(16, 5), (26, 9), (24, 20), (20, 24), (16, 27),
              (12, 24), (8, 20), (6, 9)]
    c.poly(shield, a)
    c.outline(shield, INK)
    c.poly([(16, 7), (24, 10), (22, 19), (16, 24), (10, 19), (8, 10)],
           mix(a, INK, 0.38))
    c.path([(11, 16), (15, 20), (22, 12)], INK, 4)
    c.path([(11, 16), (15, 20), (22, 12)], s, 2)
    c.line(12, 15, 15, 18, LIGHT)


def draw_clock(c: Canvas, a: int, s: int) -> None:
    c.ellipse(16, 16, 11, 11, mix(a, INK, 0.40), INK)
    c.ellipse(16, 16, 10, 10, mix(a, INK, 0.40), a)
    for x0, y0, x1, y1 in ((16, 7, 16, 9), (16, 23, 16, 25),
                           (7, 16, 9, 16), (23, 16, 25, 16)):
        c.line(x0, y0, x1, y1, LIGHT)
    c.line(16, 16, 16, 10, INK, 3)
    c.line(16, 16, 21, 19, INK, 3)
    c.line(16, 16, 16, 10, s)
    c.line(16, 16, 21, 19, LIGHT)
    c.ellipse(16, 16, 1, 1, a)


def draw_update(c: Canvas, a: int, s: int) -> None:
    top = [(8, 13), (9, 9), (12, 7), (16, 6), (21, 7), (24, 10), (25, 14)]
    bottom = [(24, 19), (23, 23), (20, 25), (16, 26), (11, 25), (8, 22), (7, 18)]
    c.path(top, INK, 5)
    c.path(bottom, INK, 5)
    c.path(top, a, 3)
    c.path(bottom, s, 3)
    c.poly([(6, 8), (11, 7), (8, 13)], a)
    c.outline([(6, 8), (11, 7), (8, 13)], INK)
    c.poly([(26, 24), (21, 25), (24, 19)], s)
    c.outline([(26, 24), (21, 25), (24, 19)], INK)
    c.line(16, 11, 16, 21, LIGHT, 2)
    c.poly([(13, 18), (16, 22), (19, 18)], LIGHT)
    c.outline([(13, 18), (16, 22), (19, 18)], INK)


def draw_storage(c: Canvas, a: int, s: int) -> None:
    c.rect(7, 9, 18, 15, INK)
    c.rect(8, 10, 16, 13, a)
    c.ellipse(16, 10, 8, 4, s, INK)
    c.ellipse(16, 10, 6, 2, mix(s, LIGHT, 0.35))
    c.line(8, 16, 24, 16, INK, 2)
    c.line(8, 21, 24, 21, INK, 2)
    c.ellipse(16, 24, 8, 4, mix(a, INK, 0.35), INK)
    c.line(10, 12, 12, 12, LIGHT)


def draw_about(c: Canvas, a: int, s: int) -> None:
    c.ellipse(16, 16, 11, 11, mix(a, INK, 0.40), INK)
    c.ellipse(16, 16, 10, 10, mix(a, INK, 0.40), a)
    c.ellipse(16, 11, 2, 2, s)
    c.rect(15, 15, 3, 8, INK)
    c.rect(15, 15, 2, 7, LIGHT)
    c.line(10, 9, 12, 7, mix(a, LIGHT, 0.45))


def draw_share(c: Canvas, a: int, s: int) -> None:
    c.line(9, 11, 23, 9, INK, 4)
    c.line(9, 11, 16, 23, INK, 4)
    c.line(23, 9, 16, 23, INK, 4)
    c.line(9, 11, 23, 9, a, 2)
    c.line(9, 11, 16, 23, s, 2)
    c.line(23, 9, 16, 23, a, 2)
    for x, y, color in ((8, 11, a), (24, 9, s), (16, 24, a)):
        c.ellipse(x, y, 4, 4, color, INK)
        c.ellipse(x - 1, y - 1, 1, 1, LIGHT)


def draw_tools(c: Canvas, a: int, s: int) -> None:
    c.line(9, 25, 21, 8, INK, 6)
    c.line(9, 25, 21, 8, a, 3)
    c.ellipse(22, 7, 4, 4, INK)
    c.poly([(25, 4), (27, 7), (24, 10), (22, 8), (25, 6)], a)
    c.outline([(25, 4), (27, 7), (24, 10), (22, 8), (25, 6)], INK)
    c.line(23, 25, 12, 8, INK, 5)
    c.line(23, 25, 12, 8, s, 2)
    c.rect(8, 23, 6, 5, INK)
    c.rect(9, 24, 4, 3, s)
    c.line(10, 10, 14, 7, LIGHT, 2)


def draw_vqeaf(c: Canvas, a: int, s: int) -> None:
    left = [(5, 7), (11, 7), (16, 18), (21, 7), (27, 7), (18, 26), (14, 26)]
    c.poly(left, INK)
    c.outline(left, INK)
    c.poly([(7, 9), (10, 9), (16, 21), (13, 21)], CYAN)
    c.poly([(12, 9), (16, 9), (18, 14), (20, 9), (24, 9), (19, 20)], BLUE)
    c.poly([(20, 12), (25, 9), (22, 9), (18, 20)], VIOLET)
    c.line(7, 8, 10, 8, LIGHT)
    c.line(22, 8, 25, 8, mix(VIOLET, LIGHT, 0.35))


def draw_qeapp(c: Canvas, a: int, s: int) -> None:
    cube = [(7, 10), (16, 6), (25, 10), (25, 23), (16, 27), (7, 23)]
    c.poly(cube, a)
    c.outline(cube, INK)
    c.poly([(7, 10), (16, 6), (25, 10), (16, 14)], s)
    c.outline([(7, 10), (16, 6), (25, 10), (16, 14)], INK)
    c.line(16, 14, 16, 26, INK, 2)
    c.line(9, 12, 16, 16, LIGHT)
    c.poly([(17, 15), (13, 21), (16, 21), (14, 25), (20, 18), (17, 18)], LIGHT)
    c.outline([(17, 15), (13, 21), (16, 21), (14, 25), (20, 18), (17, 18)], INK)


BG = rgb_to_565(*rgb("#071D4A"))
INK = rgb_to_565(*rgb("#020B22"))
LIGHT = rgb_to_565(*rgb("#F4FAFF"))
CYAN = rgb_to_565(*rgb("#78D9FF"))
BLUE = rgb_to_565(*rgb("#5EA0FF"))
VIOLET = rgb_to_565(*rgb("#B48CFF"))

SPECS = [
    ("home", "#78D9FF", "#5EA0FF", draw_home),
    ("apps", "#78D9FF", "#B48CFF", draw_apps),
    ("control", "#78D9FF", "#5EA0FF", draw_control),
    ("settings", "#B8D4FF", "#78D9FF", draw_settings),
    ("phone", "#35E0C8", "#5EA0FF", draw_phone),
    ("messages", "#78D9FF", "#B48CFF", draw_messages),
    ("contacts", "#F5B34A", "#FF8FB4", draw_contacts),
    ("browser", "#5EA0FF", "#78D9FF", draw_browser),
    ("camera", "#B48CFF", "#FF8FB4", draw_camera),
    ("gallery", "#FF8FB4", "#F5B34A", draw_gallery),
    ("music", "#4BE08A", "#78D9FF", draw_music),
    ("video", "#FF7A7A", "#B48CFF", draw_video),
    ("files", "#F3C64E", "#35E0C8", draw_files),
    ("store", "#35E0C8", "#B48CFF", draw_store),
    ("games", "#B48CFF", "#78D9FF", draw_games),
    ("calculator", "#5EA0FF", "#F5B34A", draw_calculator),
    ("recorder", "#FF8FB4", "#B48CFF", draw_recorder),
    ("calendar", "#FF7A7A", "#F5B34A", draw_calendar),
    ("notes", "#F3C64E", "#5EA0FF", draw_notes),
    ("weather", "#78D9FF", "#F5B34A", draw_weather),
    ("security", "#35E0C8", "#5EA0FF", draw_security),
    ("clock", "#B48CFF", "#78D9FF", draw_clock),
    ("update", "#5EA0FF", "#35E0C8", draw_update),
    ("storage", "#F3C64E", "#78D9FF", draw_storage),
    ("about", "#78D9FF", "#B48CFF", draw_about),
    ("share", "#B48CFF", "#78D9FF", draw_share),
    ("tools", "#B8D4FF", "#F5B34A", draw_tools),
    ("vqeaf", "#78D9FF", "#B48CFF", draw_vqeaf),
    ("qeapp", "#5EA0FF", "#78D9FF", draw_qeapp),
]


def redraw(vpe) -> None:
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    staged = []
    try:
        for name, accent_hex, secondary_hex, draw in SPECS:
            accent = vpe.rgb_to_565(*rgb(accent_hex))
            secondary = vpe.rgb_to_565(*rgb(secondary_hex))
            canvas = Canvas(BG)
            badge(canvas, accent)
            draw(canvas, accent, secondary)
            if 0xFFFF in canvas.px:
                raise SystemExit(f"{name}: pure white is reserved as the VPE transparency key")
            encoded = vpe.encode_vpe(SIZE, SIZE, canvas.px)
            if vpe.decode_vpe(encoded) != (SIZE, SIZE, canvas.px):
                raise SystemExit(f"{name}: VPEPixel codec round-trip failed")
            path = ASSET_DIR / f"{name}.vpe"
            tmp = path.with_suffix(".vpe.tmp")
            vpe.save_vpe(tmp, SIZE, SIZE, canvas.px)
            if tmp.read_bytes() != encoded or vpe.load_vpe(tmp) != (SIZE, SIZE, canvas.px):
                raise SystemExit(f"{name}: staged VPEPixel source did not verify")
            staged.append((tmp, path))
        for tmp, path in staged:
            tmp.replace(path)
    finally:
        for tmp, _path in staged:
            if tmp.exists(): tmp.unlink()
    print(f"drew {len(SPECS)} original 32x32 VPEPixel sprites in {ASSET_DIR}")


def c_source(vpe) -> str:
    header = (ROOT / "platform" / "violet" / "ui" / "icons" /
              "vqeaf_app_icons.h").read_text(encoding="ascii")
    enum_names = re.findall(r"VQEAF_APP_ICON_([A-Z0-9_]+)\s*(?:=\s*\d+)?\s*,",
                            header)
    enum_names = [name for name in enum_names if name != "COUNT"]
    expected_names = [name.upper() for name, _a, _s, _draw in SPECS]
    if enum_names != expected_names:
        raise SystemExit("icon generator order does not match vqeaf_app_icons.h")
    lines = [
        "/* SPDX-License-Identifier: MIT */",
        "/* Generated by scripts/make_vqeaf_pixel_icons.py from VPEPixel VPE565 sources.",
        " * Original QEAF artwork; 32x32 ARGB8888, row-major little-endian. Do not edit. */",
        '#include "vqeaf_app_icons.h"',
        "",
    ]
    mask = Canvas(0)
    mask.poly(BADGE_OUTER, 1)
    for name, _accent, _secondary, _draw in SPECS:
        width, height, pixels = vpe.load_vpe(ASSET_DIR / f"{name}.vpe")
        if (width, height) != (SIZE, SIZE):
            raise SystemExit(f"{name}: expected {SIZE}x{SIZE}, got {width}x{height}")
        if 0xFFFF in pixels:
            raise SystemExit(f"{name}: pure white is reserved as the VPE transparency key")
        payload = bytearray()
        for index, pixel in enumerate(pixels):
            r, g, b = unpack(pixel)
            payload += bytes((b, g, r, 255 if mask.px[index] else 0))
        lines.append(f"static const uint8_t app_icon_{name}_data[] = {{")
        for offset in range(0, len(payload), 16):
            lines.append("    " + ",".join(str(b) for b in payload[offset:offset + 16]) + ",")
        lines += [
            "};",
            "",
            f"static const lv_image_dsc_t app_icon_{name} = {{",
            "    .header = {",
            "        .magic = LV_IMAGE_HEADER_MAGIC,",
            "        .cf = LV_COLOR_FORMAT_ARGB8888,",
            f"        .w = {SIZE}, .h = {SIZE}, .stride = {SIZE * 4},",
            "    },",
            f"    .data = app_icon_{name}_data,",
            f"    .data_size = sizeof(app_icon_{name}_data),",
            "};",
            "",
        ]
    lines.append("static const lv_image_dsc_t *const app_icons[VQEAF_APP_ICON_COUNT] = {")
    for name, _accent, _secondary, _draw in SPECS:
        lines.append(f"    &app_icon_{name},")
    lines += [
        "};",
        "",
        "const lv_image_dsc_t *vqeaf_app_icon(int icon)",
        "{",
        "    if(icon < 0 || icon >= VQEAF_APP_ICON_COUNT) return NULL;",
        "    return app_icons[icon];",
        "}",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vpe-root", type=Path,
                        default=Path(os.environ.get("VPEPIXEL", r"D:\desktop-webapps\VPEPixel")))
    parser.add_argument("--redraw", action="store_true",
                        help="recreate the original .vpe drawings before exporting")
    parser.add_argument("--check", action="store_true",
                        help="verify the checked-in C file matches the .vpe sources")
    args = parser.parse_args()
    vpe = load_vpe_module(args.vpe_root)
    if args.redraw:
        redraw(vpe)
    source = c_source(vpe)
    if args.check:
        if not OUT_C.is_file() or OUT_C.read_bytes() != source.encode("ascii"):
            print("icon C descriptors are stale; run with --redraw", file=sys.stderr)
            return 1
        print(f"OK: {len(SPECS)} VPEPixel sprites match {OUT_C}")
        return 0
    OUT_C.write_text(source, encoding="ascii", newline="\n")
    print(f"wrote {OUT_C} ({len(SPECS)} icons, {len(SPECS) * SIZE * SIZE * 4} pixel bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
