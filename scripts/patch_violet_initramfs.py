#!/usr/bin/env python3
"""Replace only ./init in a known Violet QEAF initramfs, preserving other entries."""
from __future__ import annotations
import argparse
import gzip
import hashlib
from pathlib import Path
import struct
import sys


def u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def parse_newc(blob: bytes):
    pos = 0
    rows = []
    while pos + 110 <= len(blob):
        hdr = blob[pos:pos + 110]
        if hdr[:6] not in (b"070701", b"070702"):
            raise ValueError(f"bad newc magic at {pos}")
        fields = [int(hdr[6 + i * 8:14 + i * 8], 16) for i in range(13)]
        size, namesz = fields[6], fields[11]
        name_off = pos + 110
        if namesz < 1 or name_off + namesz > len(blob):
            raise ValueError("invalid cpio name length")
        name = blob[name_off:name_off + namesz - 1].decode("utf-8", "strict")
        data_off = (name_off + namesz + 3) & ~3
        end = data_off + size
        if end > len(blob):
            raise ValueError(f"truncated cpio member {name}")
        data = blob[data_off:end]
        rows.append((bytearray(hdr), name, data, fields))
        if name == "TRAILER!!!":
            return rows
        pos = (end + 3) & ~3
    raise ValueError("newc archive has no TRAILER!!!")


def pack_newc(rows) -> bytes:
    out = bytearray()
    for hdr, name, data, fields in rows:
        name_bytes = name.encode("utf-8") + b"\0"
        hdr[6 + 11 * 8:6 + 12 * 8] = f"{len(name_bytes):08x}".encode("ascii")
        hdr[6 + 6 * 8:6 + 7 * 8] = f"{len(data):08x}".encode("ascii")
        out.extend(hdr)
        out.extend(name_bytes)
        out.extend(b"\0" * ((-len(out)) & 3))
        out.extend(data)
        out.extend(b"\0" * ((-len(out)) & 3))
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--base-image", type=Path, required=True)
    ap.add_argument("--new-init", type=Path, required=True)
    ap.add_argument("--ramdisk-out", type=Path, required=True)
    a = ap.parse_args()
    try:
        image = a.base_image.read_bytes()
        if image[:8] != b"ANDROID!":
            raise ValueError("base image lacks ANDROID! magic")
        page, kernel_size, ramdisk_size = u32(image, 0x24), u32(image, 0x08), u32(image, 0x10)
        if page != 4096 or not kernel_size or not ramdisk_size:
            raise ValueError("base image header is not the expected Violet v1 layout")
        ram_off = page + ((kernel_size + page - 1) // page) * page
        compressed = image[ram_off:ram_off + ramdisk_size]
        if len(compressed) != ramdisk_size:
            raise ValueError("base ramdisk is truncated")
        cpio = gzip.decompress(compressed)
        rows = parse_newc(cpio)
        new_init = a.new_init.read_bytes()
        hits = [i for i, row in enumerate(rows) if row[1] == "./init"]
        if len(hits) != 1:
            raise ValueError(f"expected exactly one ./init, found {len(hits)}")
        idx = hits[0]
        hdr, name, old_data, fields = rows[idx]
        mode = fields[1]
        if (mode & 0o170000) != 0o100000 or not (mode & 0o111):
            raise ValueError(f"./init is not an executable regular file: mode={mode:o}")
        rows[idx] = (hdr, name, new_init, fields)
        rebuilt = pack_newc(rows)
        a.ramdisk_out.parent.mkdir(parents=True, exist_ok=True)
        a.ramdisk_out.write_bytes(gzip.compress(rebuilt, 9, mtime=0))
        check = parse_newc(gzip.decompress(a.ramdisk_out.read_bytes()))
        init_rows = [r for r in check if r[1] == "./init"]
        if len(init_rows) != 1 or init_rows[0][2] != new_init:
            raise ValueError("rebuilt ramdisk ./init readback mismatch")
        print(f"entries={len(rows)}")
        print(f"old_init_bytes={len(old_data)} new_init_bytes={len(new_init)}")
        print(f"old_ramdisk_bytes={ramdisk_size} new_ramdisk_gzip_bytes={a.ramdisk_out.stat().st_size}")
        print(f"new_init_sha256={hashlib.sha256(new_init).hexdigest()}")
        print(f"ramdisk={a.ramdisk_out}")
        return 0
    except (OSError, ValueError, gzip.BadGzipFile) as exc:
        print(f"patch_initramfs_image: REFUSED: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
