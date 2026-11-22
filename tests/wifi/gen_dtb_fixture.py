#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate a *labelled FIXTURE* Flattened Device Tree blob for HAL tests.

The values mirror the Violet WLAN platform device that was observed on the
real device (icnss 0x18800000, WCN3990 SNOC, MSA 0x93900000). This is a test
fixture only: production code reads the DTB supplied at boot and never
imports this file or these constants.
"""
from __future__ import annotations

import struct
from pathlib import Path

BE = lambda v: struct.pack(">I", v & 0xFFFFFFFF)


def cells(*vals):
    return b"".join(BE(v) for v in vals)


class FdtBuilder:
    def __init__(self):
        self.strings = bytearray()
        self.offsets = {}

    def stroff(self, name):
        if name not in self.offsets:
            self.offsets[name] = len(self.strings)
            self.strings += name.encode() + b"\x00"
        return self.offsets[name]

    def node(self, name, props, children):
        out = bytearray(BE(1) + name.encode() + b"\x00")
        out += b"\x00" * ((-len(out)) % 4)
        for pname, value in props:
            out += BE(3) + BE(len(value)) + BE(self.stroff(pname)) + value
            out += b"\x00" * ((-len(value)) % 4)
        for ch in children:
            out += ch
        out += BE(2)
        return bytes(out)

    def build(self, root, boot_cpuid=0):
        struct_block = bytearray(root)
        struct_block += BE(9)
        struct_block += b"\x00" * ((-len(struct_block)) % 4)
        header_size = 40
        rsvmap_size = 16  # terminator only
        off_struct = header_size + rsvmap_size
        off_strings = off_struct + len(struct_block)
        total = off_strings + len(self.strings)
        hdr = b"".join([
            BE(0xD00DFEED), BE(total), BE(off_struct), BE(off_strings),
            BE(header_size), BE(17), BE(16), BE(boot_cpuid),
            BE(len(self.strings)), BE(len(struct_block)),
        ])
        return hdr + b"\x00" * rsvmap_size + bytes(struct_block) + bytes(self.strings)


def build_violet_wifi_fdt():
    b = FdtBuilder()
    msa = b.node("wlan_msa_region@93900000", [
        ("compatible", b"qcom,wcnss-msa\x00"),
        ("reg", cells(0, 0x93900000, 0, 0x200000)),
    ], [])
    reserved = b.node("reserved-memory", [
        ("#address-cells", cells(2)),
        ("#size-cells", cells(2)),
        ("ranges", b""),
    ], [msa])
    irqs = b"".join(cells(0, 130 + i, 0) for i in range(12))
    wifi = b.node("wifi@18800000", [
        ("compatible", b"qcom,wcn3990-wifi\x00"),
        ("reg", cells(0, 0x18800000, 0, 0x800000)),
        ("#interrupt-cells", cells(3)),
        ("interrupts", irqs),
    ], [])
    soc = b.node("soc", [
        ("#address-cells", cells(2)),
        ("#size-cells", cells(2)),
        ("compatible", b"simple-bus\x00"),
    ], [wifi])
    root = b.node("", [
        ("compatible", b"qcom,violet\x00"),
        ("#address-cells", cells(2)),
        ("#size-cells", cells(2)),
    ], [reserved, soc])
    return b.build(root)


def main():
    out = Path(__file__).resolve().parent / "fixtures" / "FIXTURE_violet_wifi.dtb"
    out.parent.mkdir(exist_ok=True)
    data = build_violet_wifi_fdt()
    out.write_bytes(data)
    print("wrote %s (%d bytes)" % (out, len(data)))


if __name__ == "__main__":
    main()
