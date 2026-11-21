#!/usr/bin/env python3
"""Compare a device SCREEN.RAW capture with the host render of the same page.

The rescue channel's `snap` verb writes SCREEN.RAW as 240x520 RGB565 of the
live scanout (RESCUE_SCREEN_W x RESCUE_SCREEN_H). The phone shell renders a
720x1560 canvas that panel_scale maps onto the 1080x2340 panel, so one snapshot
pixel covers a 3x3 block of the canvas. This tool

  * loads build/screens/phone-ui/<page>-720x1560-<profile>.ppm (a landscape
    1560x720 render is rotated to portrait first),
  * downsamples it to the 240x520 snapshot grid and rounds through RGB565,
  * reports the share of pixels inside the tolerance and the mean error,
  * writes a side-by-side PNG (render | device) for human review,
  * exits 1 when the capture does not plausibly show that page.

`--self-test` synthesises a capture from a render, so this path is verified
before there is a phone at hand.

Usage:
    python scripts/ui_snapshot_compare.py --self-test
    python scripts/ui_snapshot_compare.py --raw build/screens/device/home.raw --page home
    python scripts/ui_snapshot_compare.py --capture-dir build/screens/device
    python scripts/ui_snapshot_compare.py --raw ... --page lock --rotate 90
"""
import argparse
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RENDER_DIR = os.path.join(ROOT, "build", "screens", "phone-ui")
RAW_W, RAW_H = 240, 520
CANVAS_W, CANVAS_H = 720, 1560

PAGES = ["lock", "home", "apps", "control", "settings", "storage", "about",
         "wifi", "drivers", "sync", "lock-theme", "files", "qeapp", "keyboard", "keyboard-themes"]


def read_ppm(path):
    with open(path, "rb") as handle:
        data = handle.read()
    parts = data.split(b"\n", 3)
    if len(parts) != 4 or parts[0] != b"P6":
        raise SystemExit("snapshot_compare: %s is not a P6 PPM" % path)
    width, height = (int(v) for v in parts[1].split())
    rgb = parts[3]
    if len(rgb) != width * height * 3:
        raise SystemExit("snapshot_compare: %s has a truncated body" % path)
    return width, height, rgb


def rotate_rgb(width, height, rgb, degrees):
    """Rotate clockwise by 0/90/180/270 into a new buffer."""
    degrees %= 360
    if degrees == 0:
        return width, height, rgb
    out = bytearray(len(rgb))
    for y in range(height):
        for x in range(width):
            i = (y * width + x) * 3
            if degrees == 90:
                nx, ny = height - 1 - y, x
                nw, nh = height, width
            elif degrees == 180:
                nx, ny = width - 1 - x, height - 1 - y
                nw, nh = width, height
            else:
                nx, ny = y, width - 1 - x
                nw, nh = height, width
            j = (ny * nw + nx) * 3
            out[j:j + 3] = rgb[i:i + 3]
    if degrees == 90 or degrees == 270:
        return height, width, bytes(out)
    return width, height, bytes(out)


def quantize565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def expand565(value):
    r5 = (value >> 11) & 0x1F
    g6 = (value >> 5) & 0x3F
    b5 = value & 0x1F
    return ((r5 << 3) | (r5 >> 2),
            (g6 << 2) | (g6 >> 4),
            (b5 << 3) | (b5 >> 2))


def read_raw(path):
    with open(path, "rb") as handle:
        data = handle.read()
    expected = RAW_W * RAW_H * 2
    if len(data) < expected:
        raise SystemExit("snapshot_compare: %s is %d bytes, expected at least %d"
                         % (path, len(data), expected))
    # `snap` appends a frame/fnv trailer; only the pixel block is compared.
    return data[:expected]


def expected_grid(width, height, rgb):
    """Downsample the canvas by 3 and round through RGB565."""
    if (width, height) != (CANVAS_W, CANVAS_H):
        raise SystemExit("snapshot_compare: render is %dx%d, expected %dx%d"
                         % (width, height, CANVAS_W, CANVAS_H))
    grid = bytearray(RAW_W * RAW_H * 3)
    for y in range(RAW_H):
        sy = min(y * 3, height - 1)
        for x in range(RAW_W):
            sx = min(x * 3, width - 1)
            i = (sy * width + sx) * 3
            q = quantize565(rgb[i], rgb[i + 1], rgb[i + 2])
            r, g, b = expand565(q)
            j = (y * RAW_W + x) * 3
            grid[j] = r
            grid[j + 1] = g
            grid[j + 2] = b
    return bytes(grid)


def device_grid(raw):
    grid = bytearray(RAW_W * RAW_H * 3)
    for pixel in range(RAW_W * RAW_H):
        value = raw[pixel * 2] | (raw[pixel * 2 + 1] << 8)
        r, g, b = expand565(value)
        grid[pixel * 3] = r
        grid[pixel * 3 + 1] = g
        grid[pixel * 3 + 2] = b
    return bytes(grid)


def compare(expected, device, tolerance):
    total = RAW_W * RAW_H
    matched = 0
    total_error = 0
    worst = 0
    for pixel in range(total):
        i = pixel * 3
        dr = abs(expected[i] - device[i])
        dg = abs(expected[i + 1] - device[i + 1])
        db = abs(expected[i + 2] - device[i + 2])
        error = max(dr, dg, db)
        total_error += error
        if error > worst:
            worst = error
        if error <= tolerance:
            matched += 1
    return matched / total, total_error / total, worst


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


def side_by_side(path, expected, device):
    gap = 6
    width = RAW_W * 2 + gap
    rgb = bytearray(width * RAW_H * 3)
    for y in range(RAW_H):
        row = y * width * 3
        src = y * RAW_W * 3
        rgb[row:row + RAW_W * 3] = expected[src:src + RAW_W * 3]
        start = (RAW_W + gap) * 3
        rgb[row + start:row + start + RAW_W * 3] = device[src:src + RAW_W * 3]
    write_png(path, width, RAW_H, bytes(rgb))


def render_path(page, profile, size):
    name = "%s-%dx%d-%s.ppm" % (page, size[0], size[1], profile)
    return os.path.join(RENDER_DIR, name)


def run_one(page, profile, raw_path, tolerance, min_match, max_mean,
            rotate=None, out=None, quiet=False, size=(CANVAS_W, CANVAS_H)):
    ppm = render_path(page, profile, size)
    if not os.path.isfile(ppm):
        raise SystemExit("snapshot_compare: missing render %s "
                         "(run scripts/render_phone_ui.py)" % ppm)
    if not os.path.isfile(raw_path):
        raise SystemExit("snapshot_compare: missing capture %s" % raw_path)

    width, height, rgb = read_ppm(ppm)
    if rotate is None:
        rotate = 90 if (width, height) == (1560, 720) else 0
    width, height, rgb = rotate_rgb(width, height, rgb, rotate)
    if (width, height) != (CANVAS_W, CANVAS_H):
        raise SystemExit("snapshot_compare: %s rotated to %dx%d, expected "
                         "%dx%d" % (ppm, width, height, CANVAS_W, CANVAS_H))

    expected = expected_grid(width, height, rgb)
    device = device_grid(read_raw(raw_path))
    match, mean, worst = compare(expected, device, tolerance)

    if out is None:
        out = os.path.join(RENDER_DIR, "compare-%s.png" % page)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    side_by_side(out, expected, device)

    passed = match >= min_match and mean <= max_mean
    if not quiet:
        print("%-9s %-9s match %.1f%%  mean %.1f  worst %3d  %s  -> %s"
              % (page, profile, match * 100.0, mean, worst,
                 "PASS" if passed else "FAIL", os.path.relpath(out, ROOT)))
    return passed, match, mean


def self_test(tolerance, min_match, max_mean):
    """Synthesise a capture from a render and prove the gate works both ways."""
    ppm = render_path("lock", "providers", (CANVAS_W, CANVAS_H))
    width, height, rgb = read_ppm(ppm)
    grid = expected_grid(width, height, rgb)
    raw = bytearray()
    for pixel in range(RAW_W * RAW_H):
        i = pixel * 3
        raw += struct.pack("<H", quantize565(grid[i], grid[i + 1], grid[i + 2]))
    synth = os.path.join(RENDER_DIR, "selftest-lock.raw")
    with open(synth, "wb") as handle:
        handle.write(raw)

    ok, match, mean = run_one("lock", "providers", synth, tolerance,
                              min_match, max_mean, quiet=True)
    print("self-test: identical frame -> match %.1f%%, mean %.1f (%s)"
          % (match * 100.0, mean, "expected PASS" if ok else "unexpected FAIL"))
    if not ok:
        return False

    # A different page must be rejected, otherwise the gate proves nothing.
    bad, match2, mean2 = run_one("home", "providers", synth, tolerance,
                                 min_match, max_mean, quiet=True)
    print("self-test: mismatched page -> match %.1f%%, mean %.1f (%s)"
          % (match2 * 100.0, mean2, "expected FAIL" if not bad else "BAD GATE"))
    if bad:
        return False

    # Landscape: the panel snapshot stays a 240x520 portrait frame, so the
    # 1560x720 render has to be rotated before sampling.
    ppm = render_path("lock", "providers", (1560, 720))
    if os.path.isfile(ppm):
        w, h, rgb = read_ppm(ppm)
        w, h, rgb = rotate_rgb(w, h, rgb, 90)
        grid = expected_grid(w, h, rgb)
        raw = bytearray()
        for pixel in range(RAW_W * RAW_H):
            i = pixel * 3
            raw += struct.pack("<H", quantize565(grid[i], grid[i + 1],
                                                 grid[i + 2]))
        synth_l = os.path.join(RENDER_DIR, "selftest-lock-landscape.raw")
        with open(synth_l, "wb") as handle:
            handle.write(raw)
        ok_l, match_l, mean_l = run_one("lock", "providers", synth_l,
                                        tolerance, min_match, max_mean,
                                        quiet=True, size=(1560, 720))
        print("self-test: landscape rotate -> match %.1f%%, mean %.1f (%s)"
              % (match_l * 100.0, mean_l, "expected PASS" if ok_l else "FAIL"))
        return ok_l
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raw", help="device SCREEN.RAW capture")
    ap.add_argument("--page", choices=PAGES, help="page shown in the capture")
    ap.add_argument("--profile", default="unknown",
                    help="render profile: unknown (default) or providers")
    ap.add_argument("--capture-dir",
                    help="compare <page>.raw for every page found in this directory")
    ap.add_argument("--rotate", type=int, choices=[0, 90, 180, 270], default=None,
                    help="rotate the render before sampling (auto for landscape)")
    ap.add_argument("--landscape", action="store_true",
                    help="the capture was taken after `rotate 90`: compare it "
                         "with the 1560x720 render (rotated back to the panel)")
    ap.add_argument("--out", help="side-by-side PNG path")
    ap.add_argument("--tolerance", type=int, default=40,
                    help="per-pixel channel tolerance (default 40)")
    ap.add_argument("--min-match", type=float, default=0.90,
                    help="minimum share of pixels inside tolerance (default 0.90)")
    ap.add_argument("--max-mean", type=float, default=12.0,
                    help="maximum mean absolute channel error (default 12)")
    ap.add_argument("--self-test", action="store_true",
                    help="verify the comparison path without a device")
    args = ap.parse_args()

    if args.self_test:
        return 0 if self_test(args.tolerance, args.min_match, args.max_mean) else 1

    if args.capture_dir:
        found = 0
        failures = 0
        for page in PAGES:
            raw = os.path.join(args.capture_dir, page + ".raw")
            if not os.path.isfile(raw):
                continue
            found += 1
            passed, _match, _mean = run_one(
                page, args.profile, raw, args.tolerance, args.min_match,
                args.max_mean, args.rotate,
                size=(1560, 720) if args.landscape else (CANVAS_W, CANVAS_H))
            if not passed:
                failures += 1
        if found == 0:
            raise SystemExit("snapshot_compare: no <page>.raw in %s"
                             % args.capture_dir)
        print("snapshot_compare: %d capture(s), %d passed, %d failed"
              % (found, found - failures, failures))
        return 1 if failures else 0

    if not args.raw or not args.page:
        ap.error("--raw and --page are required (or use --capture-dir / --self-test)")
    passed, _match, _mean = run_one(
        args.page, args.profile, args.raw, args.tolerance, args.min_match,
        args.max_mean, args.rotate, args.out,
        size=(1560, 720) if args.landscape else (CANVAS_W, CANVAS_H))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
