#!/usr/bin/env python3
"""Convert a PNG into the 64x64 BGRA icon QEAPP carries (assets/icon.rgba).

No Pillow dependency: this decodes 8-bit non-interlaced RGB/RGBA PNG with
zlib, applies the five PNG filters and resamples nearest-neighbour. The output
is exactly 64*64*4 bytes in BGRA order, which is how LVGL's
LV_COLOR_FORMAT_ARGB8888 is laid out little-endian (0xAARRGGBB).
"""
import struct
import sys
import zlib
from pathlib import Path

SIZE = 64


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a PNG: %s" % path)
    pos = 8
    width = height = 0
    color = depth = interlace = 0
    idat = bytearray()
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, color, _comp, _filter, interlace = \
                struct.unpack(">IIBBBBB", payload)
        elif tag == b"IDAT":
            idat += payload
        elif tag == b"IEND":
            break
    if depth != 8 or color not in (2, 6) or interlace != 0:
        raise SystemExit("need 8-bit non-interlaced RGB/RGBA PNG")
    channels = 4 if color == 6 else 3
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    rows = []
    prev = bytearray(stride)
    offset = 0
    for _ in range(height):
        ftype = raw[offset]
        offset += 1
        line = bytearray(raw[offset:offset + stride])
        offset += stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                line[i] = (line[i] + paeth(a, b, c)) & 0xFF
        rows.append(line)
        prev = line
    return width, height, channels, rows


def main(argv):
    if len(argv) < 3:
        print("usage: png_to_qeapp_icon.py input.png output.rgba")
        return 2
    src, dst = argv[1], argv[2]
    width, height, channels, rows = decode_png(src)
    out = bytearray()
    for y in range(SIZE):
        sy = min(height - 1, (y * height) // SIZE)
        line = rows[sy]
        for x in range(SIZE):
            sx = min(width - 1, (x * width) // SIZE)
            p = sx * channels
            r = line[p]
            g = line[p + 1]
            b = line[p + 2]
            a = line[p + 3] if channels == 4 else 255
            out += bytes((b, g, r, a))
    if len(out) != SIZE * SIZE * 4:
        raise SystemExit("bad output size")
    Path(dst).parent.mkdir(parents=True, exist_ok=True)
    Path(dst).write_bytes(bytes(out))
    print("wrote %s (%d bytes, %dx%d)" % (dst, len(out), SIZE, SIZE))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
