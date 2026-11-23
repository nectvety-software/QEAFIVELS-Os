#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Unit tests for scripts/wifi/validate_fdt.py using synthetic FDT blobs.

No real hardware values are invented: these blobs only exercise parser logic
(header bounds, phandle collection, address translation, reservations, and
UNKNOWN reporting for absent WCN3990 resources).
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "wifi"))
from validate_fdt import Fdt, FdtError, MAGIC  # noqa: E402

BEGIN, END, PROP, NOP, ENDTOK = 1, 2, 3, 4, 9


def build_fdt(nodes, reservations=()):
    """nodes: list of (name, [(propname, bytes), ...], phandle|None, children)."""
    strings = bytearray()
    nameoffs = {}

    def noff(name):
        if name not in nameoffs:
            nameoffs[name] = len(strings)
            strings.extend(name.encode() + b"\0")
        return nameoffs[name]

    blob = bytearray()

    def emit(node):
        name, props, phandle, children = node
        blob.extend(struct.pack(">I", BEGIN))
        blob.extend(name.encode() + b"\0")
        blob.extend(b"\0" * ((4 - len(blob) % 4) % 4))
        if phandle is not None:
            props = [("phandle", struct.pack(">I", phandle))] + list(props)
        for pname, val in props:
            blob.extend(struct.pack(">II", PROP, len(val)))
            blob.extend(struct.pack(">I", noff(pname)))
            blob.extend(val)
            blob.extend(b"\0" * ((4 - len(val) % 4) % 4))
        for c in children:
            emit(c)
        blob.extend(struct.pack(">I", END))

    for node in nodes:
        emit(node)
    blob.extend(struct.pack(">I", ENDTOK))
    rsv = bytearray()
    for addr, size in reservations:
        rsv.extend(struct.pack(">QQ", addr, size))
    rsv.extend(struct.pack(">QQ", 0, 0))

    off_rsv = 40
    off_struct = off_rsv + len(rsv)
    off_strings = off_struct + len(blob)
    total = off_strings + len(strings)
    hdr = struct.pack(">10I", MAGIC, total, off_struct, off_strings, off_rsv,
                      17, 0, 0, len(strings), len(blob))
    return bytes(hdr + rsv + blob + strings)


def base_tree(**kw):
    icnss = ("qcom,icnss@18800000",
             [("compatible", b"qcom,icnss\0"),
              ("reg", struct.pack(">IIII", 0x18800000, 0, 0x800000, 0)),
              ("interrupts", struct.pack(">III", 0, 414, 0) +
               struct.pack(">III", 0, 415, 0))],
             None, [])
    soc = ("soc", [("compatible", b"qcom,sm6150\0"),
                   ("#address-cells", struct.pack(">I", 2)),
                   ("#size-cells", struct.pack(">I", 2))], None, [icnss])
    return build_fdt([("", [], None, [soc])], **kw)


def test_header_and_nodes():
    f = Fdt(base_tree())
    r = f.report()
    assert r["header"]["magic"] == hex(MAGIC)
    assert r["header"]["version"] == 17
    assert r["nodes"] >= 2
    assert r["sm6150_nodes"] == ["//soc"]


def test_property_and_resource_read():
    f = Fdt(base_tree())
    reg = f.prop("//soc/qcom,icnss@18800000", "reg")
    assert reg == struct.pack(">IIII", 0x18800000, 0, 0x800000, 0)
    assert f.find_compatible("qcom,icnss") == ["//soc/qcom,icnss@18800000"]
    # WCN3990 legacy resources absent -> UNKNOWN, never guessed.
    assert f.report()["wcn3990_resources"]["reg"] == "UNKNOWN"


def test_phandle_collection():
    icnss = ("icnss", [("compatible", b"qcom,icnss\0")], 0x51, [])
    tree = build_fdt([("", [], None, [icnss])])
    f = Fdt(tree)
    assert f.phandles[0x51] == "//icnss"


def test_address_translation_cells():
    # Parent declares 2 address + 2 size cells; child reg must be 16 bytes.
    child = ("dev@10000000", [("reg", struct.pack(">IIII", 0, 0x10000000, 0, 0x1000))],
             None, [])
    parent = ("soc", [("#address-cells", struct.pack(">I", 2)),
                      ("#size-cells", struct.pack(">I", 2))], None, [child])
    f = Fdt(build_fdt([("", [], None, [parent])]))
    addr = f.prop("//soc/dev@10000000", "reg")
    assert len(addr) == 16 == 2 * 4 + 2 * 4
    assert struct.unpack(">I", addr[4:8])[0] == 0x10000000


def test_reserved_memory():
    f = Fdt(base_tree(reservations=[(0x93300000, 0x200000)]))
    assert f.report()["reserved_memory"] == [
        {"address": hex(0x93300000), "size": hex(0x200000)}]


def test_reject_bad_header():
    for bad in (b"", b"not-an-fdt" * 5, base_tree()[:8] + b"\0" * 32):
        try:
            Fdt(bad)
            raise AssertionError("bad header accepted")
        except FdtError:
            pass


def test_missing_resource_detection():
    tree = build_fdt([("", [], None, [("soc", [], None, [])])])
    f = Fdt(tree)
    assert f.prop("//soc", "reg") is None
    assert f.find_compatible("qcom,icnss") == []
    assert f.report()["verdict"] == "UNKNOWN"


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
        print("PASS", t.__name__)
    print("test_fdt_validate: ALL OK (%d tests)" % len(tests))


if __name__ == "__main__":
    main()
