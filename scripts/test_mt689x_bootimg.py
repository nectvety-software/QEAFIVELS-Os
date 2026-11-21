#!/usr/bin/env python3
"""Round-trip check for package_mt689x's boot v2 writer.

Builds an image from synthetic kernel/ramdisk/dtb data using the same header
layout as the stock violet boot image, then parses it back and compares every
field. This is a host test; it never touches a device.

    python scripts/test_mt689x_bootimg.py
"""
import os
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import package_mt689x as P  # noqa: E402

REFERENCE = os.path.join(
    os.environ.get("QEAFDROPOS", r"D:\QeafivelsOfficial\QeafdropOs"),
    "out", "reference", "boot.img")


def main():
    if not os.path.isfile(REFERENCE):
        print("skip: no reference boot.img at %s" % REFERENCE)
        return 0
    ref = P.read_boot_header(REFERENCE)
    print("reference: page=%d kernel=0x%x ramdisk=0x%x tags=0x%x dtb=0x%x"
          % (ref["page_size"], ref["kernel_addr"], ref["ramdisk_addr"],
             ref["tags_addr"], ref["dtb_addr"]))

    with tempfile.TemporaryDirectory() as tmp:
        kernel = os.path.join(tmp, "Image")
        ramdisk = os.path.join(tmp, "ramdisk")
        dtb = os.path.join(tmp, "board.dtb")
        out = os.path.join(tmp, "boot.img")
        open(kernel, "wb").write(b"\x00" * 12345)
        open(ramdisk, "wb").write(b"R" * 4096)
        open(dtb, "wb").write(b"D" * 777)

        addrs = {k: ref[k] for k in ("kernel_addr", "ramdisk_addr",
                                     "second_addr", "tags_addr", "dtb_addr")}
        P.make_bootimg(kernel, ramdisk, dtb, out, ref["page_size"], addrs,
                       "console=tty0")

        back = P.read_boot_header(out)
        want = dict(addrs)
        want["page_size"] = ref["page_size"]
        bad = [k for k, v in want.items() if back[k] != v]
        if bad:
            print("FAIL fields: %s" % ", ".join(bad))
            return 1

        page = ref["page_size"]
        with open(out, "rb") as fh:
            data = fh.read()
        header_len = 1660
        off = (header_len + page - 1) // page * page
        if data[off:off + 12345] != b"\x00" * 12345:
            print("FAIL kernel payload offset")
            return 1
        off += (12345 + page - 1) // page * page
        if data[off:off + 4096] != b"R" * 4096:
            print("FAIL ramdisk payload offset")
            return 1
        off += (4096 + page - 1) // page * page
        if data[off:off + 777] != b"D" * 777:
            print("FAIL dtb payload offset")
            return 1
        if back["dtb_addr"] != addrs["dtb_addr"]:
            print("FAIL dtb addr")
            return 1
    print("test_mt689x_bootimg: OK (header fields and payload offsets)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
