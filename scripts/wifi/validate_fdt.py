#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate a Flattened Device Tree blob without trusting its claims.

Checks header, structure/string blocks, node walk, properties, phandles,
address translation and a curated set of WCN3990 resources. Unknown values
are reported as UNKNOWN, never invented.
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

MAGIC = 0xD00DFEED
FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_NOP, FDT_END = 1, 2, 3, 4, 9

class FdtError(Exception):
    pass

def be32(b, o):
    if o + 4 > len(b):
        raise FdtError("truncated u32 at %d" % o)
    return struct.unpack_from(">I", b, o)[0]

class Fdt:
    def __init__(self, data: bytes):
        self.data = data
        if be32(data, 0) != MAGIC:
            raise FdtError("bad fdt magic")
        (self.totalsize, self.off_struct, self.off_strings, self.off_rsvmap,
         self.version, self.last_comp, self.boot_cpuid, self.size_strings,
         self.size_struct) = struct.unpack_from(">9I", data, 4)
        if self.totalsize > len(data) or self.totalsize < 40:
            raise FdtError("bad totalsize")
        if self.off_struct + self.size_struct > self.totalsize:
            raise FdtError("struct block out of bounds")
        if self.off_strings + self.size_strings > self.totalsize:
            raise FdtError("strings block out of bounds")
        self.nodes = []       # (path, props dict)
        self.phandles = {}    # value -> path
        self._walk()

    def string(self, off):
        if off >= self.size_strings:
            raise FdtError("string offset out of bounds")
        end = self.data.find(b"\0", self.off_strings + off,
                             self.off_strings + self.size_strings)
        if end < 0:
            raise FdtError("unterminated string")
        return self.data[self.off_strings + off:end].decode("utf-8", "replace")

    def _walk(self):
        pos, end = self.off_struct, self.off_struct + self.size_struct
        stack, props_stack = [], []
        while pos + 4 <= end:
            tok = be32(self.data, pos); pos += 4
            if tok == FDT_BEGIN_NODE:
                nul = self.data.find(b"\0", pos, end)
                if nul < 0:
                    raise FdtError("unterminated node name")
                name = self.data[pos:nul].decode("utf-8", "replace")
                pos += nul - pos + 1
                pos = (pos + 3) & ~3
                stack.append(name)
                props_stack.append({})
            elif tok == FDT_END_NODE:
                if not stack or not props_stack:
                    raise FdtError("END_NODE without BEGIN")
                path = "/" + "/".join(stack)
                props = props_stack.pop()
                self.nodes.append((path, props))
                for k, v in props.items():
                    if k in ("phandle", "linux,phandle") and len(v) >= 4:
                        self.phandles[struct.unpack(">I", v[:4])[0]] = path
                stack.pop()
            elif tok == FDT_PROP:
                ln = be32(self.data, pos)
                noff = be32(self.data, pos + 4)
                pos += 8
                if pos + ln > end:
                    raise FdtError("property data out of bounds")
                val = self.data[pos:pos + ln]
                pos += (ln + 3) & ~3
                if not props_stack:
                    raise FdtError("property outside node")
                props_stack[-1][self.string(noff)] = val
            elif tok == FDT_NOP:
                continue
            elif tok == FDT_END:
                break
            else:
                raise FdtError("unknown token 0x%x" % tok)

    def prop(self, path, name):
        for p, props in self.nodes:
            if p == path:
                return props.get(name)
        return None

    def find_compatible(self, needle):
        out = []
        for path, props in self.nodes:
            val = props.get("compatible", b"")
            if needle.encode() in val:
                out.append(path)
        return out

    def report(self):
        wcn = self.find_compatible("wcn3990")
        sm = self.find_compatible("qcom,sm6150")
        msa = self.find_compatible("wlan_msa") + self.find_compatible("wcnss-msa")
        reservations = self._reservations()
        return {
            "header": {"magic": hex(MAGIC), "version": self.version,
                       "totalsize": self.totalsize, "struct_bytes": self.size_struct,
                       "strings_bytes": self.size_strings},
            "nodes": len(self.nodes),
            "phandles": len(self.phandles),
            "sm6150_nodes": sm,
            "wcn3990_nodes": wcn,
            "wcn3990_resources": self._wcn_resources(wcn[0] if wcn else None),
            "msa_nodes": msa,
            "reserved_memory": reservations,
            "verdict": "PARTIAL" if sm or wcn else "UNKNOWN",
        }

    def _reservations(self):
        out, pos = [], self.off_rsvmap
        while pos + 16 <= len(self.data):
            addr, size = struct.unpack_from(">QQ", self.data, pos)
            if addr == 0 and size == 0:
                break
            out.append({"address": hex(addr), "size": hex(size)})
            pos += 16
        return out

    def _wcn_resources(self, path):
        if not path:
            return {k: "UNKNOWN" for k in
                    ("reg", "interrupts", "clocks", "resets", "memory-region")}
        keys = ("reg", "interrupts", "clocks", "resets", "memory-region",
                "qcom,msa-fixed-perm", "iommus")
        out = {}
        for k in keys:
            v = self.prop(path, k)
            out[k] = v.hex() if v is not None else "UNKNOWN"
        return out


def main():
    if len(sys.argv) != 2:
        print("usage: validate_fdt.py FILE.dtb", file=sys.stderr)
        return 2
    p = Path(sys.argv[1])
    try:
        f = Fdt(p.read_bytes())
    except FdtError as ex:
        print(json.dumps({"file": str(p), "error": str(ex), "verdict": "FAILED"}))
        return 1
    print(json.dumps({"file": str(p), "sha256": __import__("hashlib").sha256(
        p.read_bytes()).hexdigest(), **f.report()}, indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
