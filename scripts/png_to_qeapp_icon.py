#!/usr/bin/env python3
"""Convert a PNG into the 64x64 BGRA icon QEAPP carries (assets/icon.rgba).

No Pillow dependency: this decodes 8-bit non-interlaced RGB/RGBA PNG with
zlib, applies the five PNG filters and resamples nearest-neighbour. The output
is exactly 64*64*4 bytes in BGRA order, which is how LVGL's
LV_COLOR_FORMAT_ARGB8888 is laid out little-endian (0xAARRGGBB).
"""
from collections import deque
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


def bleed_alpha_edges(width, height, channels, rows):
    if channels != 4 or width == 0 or height == 0:
        return rows
    max_alpha = max(row[index] for row in rows for index in range(3, len(row), 4))
    if max_alpha == 0:
        return rows
    unseen = bytearray(b"\xFF") * (width * height)
    queue = deque()
    for y, row in enumerate(rows):
        for x in range(width):
            index = y * width + x
            if row[x * 4 + 3] == max_alpha:
                unseen[index] = 0
                queue.append((x, y))
    while queue:
        x, y = queue.popleft()
        source = rows[y]
        source_offset = x * 4
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if not (0 <= nx < width and 0 <= ny < height):
                continue
            index = ny * width + nx
            if not unseen[index]:
                continue
            unseen[index] = 0
            target = rows[ny]
            target_offset = nx * 4
            target[target_offset:target_offset + 3] = \
                source[source_offset:source_offset + 3]
            queue.append((nx, ny))
    return rows


def main(argv):
    if len(argv) < 3:
        print("usage: png_to_qeapp_icon.py input.png output.rgba")
        return 2
    src, dst = argv[1], argv[2]
    width, height, channels, rows = decode_png(src)
    rows = bleed_alpha_edges(width, height, channels, rows)
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
