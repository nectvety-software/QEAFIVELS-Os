"""Minimal PIC static linker: ELF64 relocatable objects -> .qen sections.

This is a build-time tool, not the device loader. It concatenates the code,
rodata and data sections of one or more freestanding, position-independent
objects, resolves PC-relative relocations against the final contiguous layout
(code | rodata | data | bss) and rejects anything that would need a load-time
relocation (absolute or GOT relocations) or an undefined symbol. The result is
a fully position-independent blob the simulator can map at any address.

Only the relocation types needed by freestanding C are supported; anything
else is a hard error so a broken module is never shipped silently.
"""

import struct

SHT_PROGBITS = 1
SHT_STRTAB = 3
SHT_SYMTAB = 2
SHT_RELA = 4
SHT_NOBITS = 8

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

STT_SECTION = 3
STB_GLOBAL = 1
STB_WEAK = 2
SHN_UNDEF = 0

EM_X86_64 = 62
EM_AARCH64 = 183

# x86_64 relocation types
R_X86_64_NONE = 0
R_X86_64_64 = 1
R_X86_64_PC32 = 2
R_X86_64_PLT32 = 4
R_X86_64_32 = 10
R_X86_64_32S = 11

# aarch64 relocation types
R_AARCH64_NONE = 0
R_AARCH64_ABS64 = 257
R_AARCH64_LD_PREL_LO19 = 273
R_AARCH64_ADR_PREL_LO21 = 274
R_AARCH64_ADR_PREL_PG_HI21 = 275
R_AARCH64_ADR_PREL_PG_HI21_NC = 276
R_AARCH64_ADD_ABS_LO12_NC = 277
R_AARCH64_LDST8_ABS_LO12_NC = 278
R_AARCH64_TSTBR14 = 279
R_AARCH64_CONDBR19 = 280
R_AARCH64_JUMP26 = 282
R_AARCH64_CALL26 = 283
R_AARCH64_LDST16_ABS_LO12_NC = 284
R_AARCH64_LDST32_ABS_LO12_NC = 285
R_AARCH64_LDST64_ABS_LO12_NC = 286
R_AARCH64_LDST128_ABS_LO12_NC = 299

_SHDR = struct.Struct("<IIQQQQIIQQ")
_SYM = struct.Struct("<IBBHQQ")
_RELA = struct.Struct("<QQq")


class LinkError(ValueError):
    pass


def _cstr(blob, off):
    end = blob.index(b"\0", off)
    return blob[off:end].decode("utf-8", "replace")


class _Section:
    __slots__ = ("name", "type", "flags", "offset", "size", "link", "info",
                 "align", "entsize", "data", "base", "category")

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k, 0))
        self.data = kw.get("data", b"")


class _Object:
    def __init__(self, path, blob):
        if blob[:4] != b"\x7fELF" or blob[4] != 2 or blob[5] != 1:
            raise LinkError("%s: not a little-endian ELF64 object" % path)
        self.path = path
        self.blob = blob
        self.machine = struct.unpack_from("<H", blob, 18)[0]
        shoff = struct.unpack_from("<Q", blob, 40)[0]
        shentsize = struct.unpack_from("<H", blob, 58)[0]
        shnum = struct.unpack_from("<H", blob, 60)[0]
        shstrndx = struct.unpack_from("<H", blob, 62)[0]
        raw = []
        for i in range(shnum):
            (name, typ, flags, addr, offset, size, link, info,
             align, entsize) = _SHDR.unpack_from(blob, shoff + i * shentsize)
            sec = _Section(name=name, type=typ, flags=flags, offset=offset,
                           size=size, link=link, info=info, align=align,
                           entsize=entsize)
            if typ != SHT_NOBITS:
                sec.data = bytearray(blob[offset:offset + size])
            raw.append(sec)
        shstr = raw[shstrndx]
        strblob = blob[shstr.offset:shstr.offset + shstr.size]
        for sec in raw:
            sec.name = _cstr(strblob, sec.name)
        self.sections = raw
        self.symbols = []
        self.relas = {}
        for idx, sec in enumerate(raw):
            if sec.type == SHT_SYMTAB:
                strsec = raw[sec.link]
                symstr = blob[strsec.offset:strsec.offset + strsec.size]
                for off in range(0, sec.size, 24):
                    nm, info, other, shndx, value, size = _SYM.unpack_from(
                        sec.data, off)
                    self.symbols.append({
                        "name": _cstr(symstr, nm), "info": info,
                        "shndx": shndx, "value": value, "size": size,
                        "section": idx})
            elif sec.type == SHT_RELA:
                self.relas[sec.info] = sec

    def category(self, sec):
        if sec.type == SHT_NOBITS and sec.name.startswith(".bss"):
            return "bss"
        if sec.type != SHT_PROGBITS or not (sec.flags & SHF_ALLOC):
            return None
        if sec.name.startswith(".text") or (sec.flags & SHF_EXECINSTR):
            return "code"
        if sec.flags & SHF_WRITE:
            if sec.name.startswith(".data"):
                return "data"
            return None
        if sec.name.startswith(".rodata"):
            return "rodata"
        return None


def _align_up(value, align):
    if align <= 1:
        return value
    return (value + align - 1) & ~(align - 1)


def _patch32(buf, off, value):
    struct.pack_into("<i", buf, off, value)


def _apply_x86(buf, place, rtype, sym_val, addend):
    if rtype == R_X86_64_NONE:
        return
    if rtype in (R_X86_64_PC32, R_X86_64_PLT32):
        _patch32(buf, place, sym_val + addend - place)
        return
    raise LinkError("x86_64 relocation type %d is not position-independent "
                    "(absolute/GOT); use -fPIC freestanding code" % rtype)


def _apply_aarch64(buf, place, rtype, sym_val, addend):
    if rtype == R_AARCH64_NONE:
        return
    insn = struct.unpack_from("<I", buf, place)[0]
    target = sym_val + addend
    if rtype in (R_AARCH64_ADR_PREL_PG_HI21, R_AARCH64_ADR_PREL_PG_HI21_NC):
        delta = (target >> 12) - (place >> 12)
        if rtype == R_AARCH64_ADR_PREL_PG_HI21 and not (-(1 << 20) <= delta < (1 << 20)):
            raise LinkError("adrp target out of +/-4GiB range")
        immlo = delta & 0x3
        immhi = (delta >> 2) & 0x7FFFF
        insn = (insn & ~0x60000000 & ~0x00FFFFE0) | (immlo << 29) | (immhi << 5)
    elif rtype == R_AARCH64_ADR_PREL_LO21:
        delta = target - place
        if not (-(1 << 18) <= delta < (1 << 18)):
            raise LinkError("adr target out of +/-1MiB range")
        insn = (insn & ~0x00FFFFE0) | ((delta & 0x7FFFF) << 5)
    elif rtype == R_AARCH64_ADD_ABS_LO12_NC:
        insn = (insn & ~0x003FFC00) | ((target & 0xFFF) << 10)
    elif rtype in (R_AARCH64_LDST8_ABS_LO12_NC, R_AARCH64_LDST16_ABS_LO12_NC,
                   R_AARCH64_LDST32_ABS_LO12_NC, R_AARCH64_LDST64_ABS_LO12_NC,
                   R_AARCH64_LDST128_ABS_LO12_NC):
        scale = {R_AARCH64_LDST8_ABS_LO12_NC: 0, R_AARCH64_LDST16_ABS_LO12_NC: 1,
                 R_AARCH64_LDST32_ABS_LO12_NC: 2, R_AARCH64_LDST64_ABS_LO12_NC: 3,
                 R_AARCH64_LDST128_ABS_LO12_NC: 4}[rtype]
        if scale and (target & ((1 << scale) - 1)):
            raise LinkError("load/store target misaligned for access size")
        # The immediate encodes the offset inside the 4 KiB page addressed by
        # the paired adrp, so mask the page first: (target>>scale)&0xFFF would
        # leak bits from the page number into the immediate.
        insn = (insn & ~0x003FFC00) | (((target & 0xFFF) >> scale) << 10)
    elif rtype == R_AARCH64_LD_PREL_LO19:
        delta = target - place
        if delta & 3:
            raise LinkError("literal load target not 4-byte aligned")
        imm = (delta >> 2) & 0x7FFFF
        insn = (insn & ~0x00FFFFE0) | (imm << 5)
    elif rtype == R_AARCH64_CONDBR19:
        delta = target - place
        if delta & 3:
            raise LinkError("conditional branch target not 4-byte aligned")
        imm = (delta >> 2) & 0x7FFFF
        insn = (insn & ~0x00FFFFE0) | (imm << 5)
    elif rtype == R_AARCH64_TSTBR14:
        delta = target - place
        if delta & 3:
            raise LinkError("test branch target not 4-byte aligned")
        imm = (delta >> 2) & 0x3FFF
        insn = (insn & ~0x0007FFE0) | (imm << 5)
    elif rtype in (R_AARCH64_CALL26, R_AARCH64_JUMP26):
        delta = target - place
        if delta & 3:
            raise LinkError("branch target not 4-byte aligned")
        if not (-(1 << 27) <= delta < (1 << 27)):
            raise LinkError("branch target out of +/-128MiB range")
        imm = (delta >> 2) & 0x03FFFFFF
        insn = (insn & ~0x03FFFFFF) | imm
    else:
        raise LinkError("aarch64 relocation type %d is not position-independent "
                        "(absolute/GOT); use -fPIC freestanding code" % rtype)
    struct.pack_into("<I", buf, place, insn)


def link_objects(paths, arch):
    """Link ELF objects into .qen sections. Returns a dict with code, rodata,
    data, bss_size, exports (list of (name, offset)) and entry_offset."""
    if arch == "x86_64":
        machine, apply_reloc = EM_X86_64, _apply_x86
    elif arch == "arm64":
        machine, apply_reloc = EM_AARCH64, _apply_aarch64
    else:
        raise LinkError("unsupported architecture: %s" % arch)

    objects = []
    for path in paths:
        with open(path, "rb") as handle:
            obj = _Object(str(path), handle.read())
        if obj.machine != machine:
            raise LinkError("%s: object architecture does not match %s" % (path, arch))
        objects.append(obj)

    # Assign each kept section a base offset inside its category blob.
    blobs = {"code": bytearray(), "rodata": bytearray(), "data": bytearray()}
    max_align = {"code": 1, "rodata": 1, "data": 1, "bss": 1}
    bss_size = 0
    kept = []
    for obj in objects:
        for idx, sec in enumerate(obj.sections):
            cat = obj.category(sec)
            if cat is None:
                continue
            align = max(1, sec.align)
            max_align[cat] = max(max_align[cat], align)
            if cat == "bss":
                sec.base = _align_up(bss_size, align)
                bss_size = sec.base + sec.size
            else:
                blob = blobs[cat]
                base = _align_up(len(blob), align)
                blob.extend(b"\0" * (base - len(blob)))
                blob.extend(sec.data)
                sec.base = base
            kept.append((obj, idx, sec, cat))

    # Pad each blob to a common boundary alignment so every category base in
    # the contiguous code|rodata|data|bss layout meets all section alignments.
    boundary = max(16, max_align["code"], max_align["rodata"],
                   max_align["data"], max_align["bss"])
    code = blobs["code"]
    code.extend(b"\0" * (_align_up(len(code), boundary) - len(code)))
    rodata = blobs["rodata"]
    rodata.extend(b"\0" * (_align_up(len(rodata), boundary) - len(rodata)))
    data = blobs["data"]
    data.extend(b"\0" * (_align_up(len(data), boundary) - len(data)))
    bases = {"code": 0, "rodata": len(code), "data": len(code) + len(rodata)}

    # Merge symbols; resolve section-relative values to layout offsets.
    globals_by_name = {}
    for obj in objects:
        for sym in obj.symbols:
            bind = sym["info"] >> 4
            stype = sym["info"] & 0xF
            if sym["shndx"] == SHN_UNDEF:
                sym["abs"] = None
                continue
            if sym["shndx"] >= 0xFF00:
                # Special indices: SHN_ABS (0xFFF1) carries an absolute value.
                sym["abs"] = sym["value"] if sym["shndx"] == 0xFFF1 else None
                continue
            sec = obj.sections[sym["shndx"]]
            cat = obj.category(sec)
            if cat is None or cat == "bss":
                # Symbols in dropped/bss sections still need a base for relocs.
                if cat == "bss":
                    sym["abs"] = bases["data"] + len(data) + sec.base
                else:
                    sym["abs"] = None
                continue
            sym["abs"] = bases[cat] + sec.base + sym["value"]
            if bind in (STB_GLOBAL, STB_WEAK) and stype != STT_SECTION and sym["name"]:
                if sym["name"] in globals_by_name:
                    raise LinkError("duplicate global symbol: %s" % sym["name"])
                globals_by_name[sym["name"]] = (sym, cat)

    def sym_value(obj, sym_index):
        sym = obj.symbols[sym_index]
        if sym["shndx"] == SHN_UNDEF:
            name = sym["name"]
            if name and name in globals_by_name:
                gsym, gcat = globals_by_name[name]
                if gsym["abs"] is not None:
                    return gsym["abs"]
            raise LinkError("undefined symbol: %s (no libc in native modules)"
                            % (sym["name"] or "<unnamed>"))
        if (sym["info"] & 0xF) == STT_SECTION:
            sec = obj.sections[sym["shndx"]]
            cat = obj.category(sec)
            if cat is None:
                raise LinkError("relocation against dropped section %s" % sec.name)
            if cat == "bss":
                return bases["data"] + len(data) + sec.base
            return bases[cat] + sec.base
        if sym["abs"] is None:
            raise LinkError("symbol %s has no layout address" % sym["name"])
        return sym["abs"]

    # Apply relocations to kept sections.
    for obj, idx, sec, cat in kept:
        if cat == "bss":
            continue
        rela = obj.relas.get(idx)
        if rela is None:
            continue
        blob = blobs[cat]
        for off in range(0, rela.size, 24):
            r_offset, r_info, r_addend = _RELA.unpack_from(rela.data, off)
            sym_index = r_info >> 32
            rtype = r_info & 0xFFFFFFFF
            place = sec.base + r_offset
            value = sym_value(obj, sym_index)
            apply_reloc(blob, place, rtype, value, r_addend)

    exports = []
    for name, (sym, cat) in sorted(globals_by_name.items()):
        if cat == "code":
            exports.append((name, sym["abs"]))
    entry_offset = next((off for name, off in exports if name == "create"), 0)
    return {"code": bytes(code), "rodata": bytes(rodata), "data": bytes(data),
            "bss_size": bss_size, "exports": exports, "entry_offset": entry_offset}