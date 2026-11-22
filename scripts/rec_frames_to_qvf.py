#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Assemble QEAFRESCUE screen-recorder PNG frames into an OS-native QVF1 clip.

The device records real scanout frames as REC0001.PNG... on the RAM rescue
drive. This host tool decodes them losslessly, converts to RGB565 and writes
the QVF1 container defined in docs/QEAF_VIDEO_FORMAT.md. No fake frames.
"""
from __future__ import annotations

import argparse
import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))


def _load(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


_qvf = _load("qeaf_qvf", "mp4_to_qvf.py")
_png = _load("qeaf_png", "png_to_qeapp_icon.py")


def rgb565(rgba: bytes, width: int, height: int) -> bytes:
    out = bytearray(width * height * 2)
    ch = 4 if len(rgba) == width * height * 4 else 3
    for i in range(width * height):
        r, g, b = rgba[i * ch], rgba[i * ch + 1], rgba[i * ch + 2]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out[i * 2] = v & 0xFF
        out[i * 2 + 1] = (v >> 8) & 0xFF
    return bytes(out)


def convert(src_dir: Path, out: Path, fps: int) -> dict:
    frames = sorted(src_dir.glob("REC*.PNG"))
    if not frames:
        raise SystemExit("no REC*.PNG frames found in %s" % src_dir)
    sizes = set()
    payload = bytearray()
    for path in frames:
        width, height, channels, rows = _png.decode_png(str(path))
        rgba = b"".join(bytes(r) for r in rows)
        sizes.add((width, height))
        raw = rgb565(rgba, width, height)
        comp = _qvf.lz4_compress(raw)
        if _qvf.lz4_decompress_check(comp, len(raw)) != raw:
            raise SystemExit("LZ4 verification failed for %s" % path.name)
        payload += struct.pack("<II", len(raw), len(comp)) + comp
    if len(sizes) != 1:
        raise SystemExit("frames have inconsistent dimensions: %s" % sizes)
    width, height = sizes.pop()
    if not 1 <= fps <= 30:
        raise SystemExit("fps must be 1..30")
    header = (_qvf.QVF_MAGIC + struct.pack(
        "<HHIIHHIII", _qvf.QVF_VERSION, _qvf.QVF_CODEC_LZ4_RGB565,
        width, height, fps, 0, len(frames), 0, 0))
    assert len(header) == _qvf.HEADER_SIZE
    out.write_bytes(header + bytes(payload))
    return {"clip": str(out), "frames": len(frames),
            "width": width, "height": height, "fps": fps,
            "bytes": out.stat().st_size}


def main(argv=None) -> int:
    import json
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--frames", type=Path,
                   default=ROOT / "build" / "screens" / "device" / "recording")
    p.add_argument("--out", type=Path, default=ROOT / "dist" / "screen-record.qvf")
    p.add_argument("--fps", type=int, default=8)
    a = p.parse_args(argv)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    print(json.dumps(convert(a.frames, a.out, a.fps), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
