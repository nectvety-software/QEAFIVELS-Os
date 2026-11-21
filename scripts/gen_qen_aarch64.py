#!/usr/bin/env python3
"""Build the AArch64 .qen demo app and embed it for the independent kernel.

Compiles user/qen_app_aarch64.c as a position-independent image (zig -fPIC
-pie, linked at base 0), then packs it into the shipping .qen format using
sdk/compiler/qen.py (the single source of truth for the header). An aarch64
relocation table is appended AFTER the exports table; the frozen 64-byte header
and the structural validator are unchanged (trailing payload bytes are allowed
and covered by the payload hash). The kernel loader (kernel/core/qen.c) reads
that table and applies R_AARCH64_RELATIVE fixups by the load base.

The flat .qen layout (code | rodata | data | bss) is built to preserve link-time
virtual addresses, so a relocation r_offset is a byte offset into the loaded
image. Emits user/qen_app_aarch64_qen.h.
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "kernel-aarch64"
sys.path.insert(0, str(ROOT / "sdk" / "compiler"))
import qen  # noqa: E402

R_AARCH64_RELATIVE = 1027
SHF_ALLOC = 0x2
SHT_PROGBITS = 1
SHT_NOBITS = 8
SHT_RELA = 4


def find_zig():
    env = os.environ.get("ZIG")
    if env and Path(env).is_file():
        return env
    base = os.environ.get("QEAFDROPOS") or "D:\\QeafivelsOfficial\\QeafdropOs"
    candidate = Path(base) / "out" / "toolchain" / "zig-windows-x86_64-0.13.0" / "zig.exe"
    if candidate.is_file():
        return str(candidate)
    raise SystemExit("zig toolchain not found; set ZIG=/path/to/zig")


def compile_pie(zig, name):
    elf = OUT / ("user_%s.elf" % name)
    cmd = [zig, "cc", "-target", "aarch64-linux-musl", "-ffreestanding",
           "-fPIC", "-pie", "-nostdlib", "-fno-builtin", "-fno-stack-protector",
           "-mstrict-align", "-O2", "-g0",
           "-I", str(ROOT / "kernel" / "include"),
           "-Wl,-T," + (ROOT / "user" / "qen_app_aarch64.ld").as_posix(),
           "-Wl,--build-id=none", "-Wl,-e,create", "-Wl,-z,max-page-size=4096",
           "-Wl,--strip-debug",
           "-o", str(elf), str(ROOT / "user" / (name + ".c"))]
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=180)
    if proc.returncode != 0:
        raise SystemExit("qen app PIC link FAILED:\n" + (proc.stderr or proc.stdout))
    image = elf.read_bytes()
    if image[:4] != b"\x7fELF" or image[4] != 2 or image[5] != 1:
        raise SystemExit("qen app: not ELF64 little-endian")
    e_type, e_machine = struct.unpack_from("<HH", image, 16)
    if e_type != 3:
        raise SystemExit("qen app: not ET_DYN/PIE (e_type=%d)" % e_type)
    if e_machine != 0xB7:
        raise SystemExit("qen app: not EM_AARCH64 (0x%x)" % e_machine)
    return image


def parse_sections(image):
    e_shoff = struct.unpack_from("<Q", image, 40)[0]
    e_shentsize = struct.unpack_from("<H", image, 58)[0]
    e_shnum = struct.unpack_from("<H", image, 60)[0]
    e_shstrndx = struct.unpack_from("<H", image, 62)[0]
    raw = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        (name, typ, flags, addr, offset, size, link, info, align,
         entsize) = struct.unpack_from("<IIQQQQIIQQ", image, off)
        raw.append(dict(name=name, type=typ, flags=flags, addr=addr,
                        offset=offset, size=size, entsize=entsize))
    strtab = raw[e_shstrndx]
    names = image[strtab["offset"]:strtab["offset"] + strtab["size"]]
    for s in raw:
        end = names.find(b"\0", s["name"])
        s["sname"] = names[s["name"]:end].decode("ascii", "replace")
    return raw


def build_qen(image):
    sections = parse_sections(image)
    by_name = {s["sname"]: s for s in sections}
    e_entry = struct.unpack_from("<Q", image, 24)[0]

    alloc = [s for s in sections if (s["flags"] & SHF_ALLOC)
             and s["type"] in (SHT_PROGBITS, SHT_NOBITS) and s["size"]]
    if not alloc:
        raise SystemExit("qen app: no allocatable sections")
    total = max(s["addr"] + s["size"] for s in alloc)
    blob = bytearray(total)
    for s in alloc:
        if s["type"] == SHT_PROGBITS:
            blob[s["addr"]:s["addr"] + s["size"]] = image[s["offset"]:s["offset"] + s["size"]]

    def end_of(name, fallback):
        s = by_name.get(name)
        return s["addr"] + s["size"] if s and s["size"] else fallback

    text_end = end_of(".text", 0)
    rodata_end = end_of(".rodata", text_end)
    bss = by_name.get(".bss")
    bss_start = bss["addr"] if bss and bss["size"] else total
    bss_size = bss["size"] if bss else 0
    if not (text_end <= rodata_end <= bss_start <= total):
        raise SystemExit("qen app: sections not in ascending link order")
    code = bytes(blob[:text_end])
    rodata = bytes(blob[text_end:rodata_end])
    data = bytes(blob[rodata_end:bss_start])
    if e_entry >= len(code):
        raise SystemExit("qen app: entry outside .text")

    rela = by_name.get(".rela.dyn")
    relocs = []
    if rela:
        for off in range(rela["offset"], rela["offset"] + rela["size"], 24):
            r_offset, r_info, r_addend = struct.unpack_from("<QQq", image, off)
            if (r_info & 0xFFFFFFFF) == R_AARCH64_RELATIVE:
                if r_offset + 8 > bss_start:
                    raise SystemExit("qen app: reloc offset outside loaded image")
                relocs.append((r_offset, r_addend))
    if not relocs:
        raise SystemExit("qen app: no R_AARCH64_RELATIVE relocations (cannot prove relocation)")

    module = bytearray(qen.build_module(
        code, rodata, data, bss_size=bss_size,
        exports=[("create", e_entry)], arch="arm64",
        abi_version=2, entry_offset=e_entry))
    # Append the relocation table after exports; refresh module_size + hash.
    # RELA carries an explicit addend (the section bytes may be zero), so each
    # record stores (offset, type, addend); the loader writes base + addend.
    table = struct.pack("<I", len(relocs))
    for r_offset, r_addend in relocs:
        table += struct.pack("<IIQ", r_offset, R_AARCH64_RELATIVE, r_addend)
    module += table
    struct.pack_into("<I", module, 17, len(module))
    struct.pack_into("<I", module, 49, qen.fnv1a32(bytes(module[qen.HEADER_SIZE:])))
    module = bytes(module)

    errors = qen.validate_module(module, expected_arch="arm64")
    if errors:
        raise SystemExit("qen app: generated module failed validation: " + "; ".join(errors))
    return module, e_entry, len(relocs)


def emit_header(name, module, entry_offset, nreloc):
    upper = name.upper()
    guard = "VQEAF_USER_%s_QEN_H" % upper
    hdr = ROOT / "user" / (name + "_qen.h")
    lines = ["/* Generated by scripts/gen_qen_aarch64.py; do not edit. %d bytes, %d relocs. */"
             % (len(module), nreloc),
             "#ifndef " + guard,
             "#define " + guard,
             "#define VQEAF_USER_%s_SIZE %du" % (upper, len(module)),
             "#define VQEAF_USER_%s_ENTRY_OFFSET 0x%xull" % (upper, entry_offset),
             "__attribute__((aligned(8))) static const unsigned char vqeaf_user_%s_qen[] = {" % name]
    for i in range(0, len(module), 16):
        lines.append(" " + ",".join("0x%02x" % b for b in module[i:i + 16]) + ",")
    lines.append("};")
    lines.append("#endif")
    hdr.write_text("\n".join(lines) + "\n")
    print("gen_qen_aarch64: %s %d byte .qen, entry_offset=0x%x, %d R_AARCH64_RELATIVE -> %s"
          % (name, len(module), entry_offset, nreloc, hdr.name))


APPS = ("qen_app_aarch64", "qen_window_aarch64")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = find_zig()
    for name in APPS:
        image = compile_pie(zig, name)
        module, entry_offset, nreloc = build_qen(image)
        emit_header(name, module, entry_offset, nreloc)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())