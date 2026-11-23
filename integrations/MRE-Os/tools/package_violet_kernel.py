import argparse, gzip, json, struct, subprocess
from pathlib import Path

MAGIC = b"\xd0\x0d\xfe\xed"
TEXT_OFFSET = 0x80000
IMAGE_FLAGS = 0xA

def be32(data, off):
    return struct.unpack_from(">I", data, off)[0]

def parse_dtb(path):
    data = Path(path).read_bytes()
    if len(data) < 40 or data[:4] != MAGIC:
        raise SystemExit("DTB rejected: invalid FDT magic/header")
    total, struct_off, strings_off = struct.unpack_from(">III", data, 4)
    reserve_off = be32(data, 16)
    version, last = struct.unpack_from(">II", data, 20)
    strings_size, struct_size = struct.unpack_from(">II", data, 32)
    if total > len(data) or version < 17 or last > version:
        raise SystemExit("DTB rejected: invalid FDT bounds/version")
    if reserve_off < 40 or reserve_off >= struct_off or reserve_off & 7:
        raise SystemExit("DTB rejected: invalid reserve-map offset")
    if struct_off + struct_size > total or strings_off + strings_size > total:
        raise SystemExit("DTB rejected: FDT blocks outside totalsize")
    strings = data[strings_off:strings_off+strings_size]
    roots, stack, p = [], [], struct_off
    while p < struct_off + struct_size:
        token = be32(data, p); p += 4
        if token == 1:
            end = data.find(b"\0", p, struct_off+struct_size)
            if end < 0: raise SystemExit("DTB rejected: malformed node")
            name = data[p:end].decode("utf-8", "replace")
            p = (end + 4) & ~3
            node = {"name": name, "props": {}, "children": [], "parent": stack[-1] if stack else None}
            (stack[-1]["children"] if stack else roots).append(node)
            stack.append(node)
        elif token == 2:
            if not stack: raise SystemExit("DTB rejected: unbalanced tree")
            stack.pop()
        elif token == 3:
            length, nameoff = struct.unpack_from(">II", data, p); p += 8
            end = p + length
            if end > struct_off+struct_size or nameoff >= len(strings):
                raise SystemExit("DTB rejected: malformed property")
            name_end = strings.find(b"\0", nameoff)
            if name_end < 0: raise SystemExit("DTB rejected: malformed property name")
            stack[-1]["props"][strings[nameoff:name_end].decode("utf-8","replace")] = data[p:end]
            p = (end + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            break
        else:
            raise SystemExit(f"DTB rejected: unknown token {token}")
    if stack or not roots: raise SystemExit("DTB rejected: incomplete tree")
    def walk(nodes):
        for node in nodes:
            yield node
            yield from walk(node["children"])
    def strings_prop(node, key):
        raw = node["props"].get(key, b"")
        return [x.decode("utf-8","replace") for x in raw.rstrip(b"\0").split(b"\0") if x]
    root_compat = strings_prop(roots[0], "compatible")
    if "xiaomi,violet" not in root_compat:
        raise SystemExit("DTB rejected: root compatible does not identify xiaomi,violet; generic SM6150/IDP DTBs are not accepted")
    mem_nodes = [n for n in walk(roots) if strings_prop(n, "device_type") == ["memory"]]
    if not mem_nodes:
        raise SystemExit("DTB rejected: no memory node")
    valid_memory = False
    for node in mem_nodes:
        parent = node["parent"]
        if parent is None: continue
        ac = be32(parent["props"].get("#address-cells", b"\0\0\0\2"), 0)
        sc = be32(parent["props"].get("#size-cells", b"\0\0\0\1"), 0)
        raw = node["props"].get("reg", b"")
        stride = (ac+sc)*4
        if stride and len(raw)%stride == 0:
            for off in range(0,len(raw),stride):
                address = int.from_bytes(raw[off:off+ac*4],"big")
                size = int.from_bytes(raw[off+ac*4:off+stride],"big")
                if size and address+size < (1<<48): valid_memory=True
    if not valid_memory:
        raise SystemExit("DTB rejected: RAM node has no valid runtime reg range")
    reserved = next((n for n in walk(roots) if n["name"]=="reserved-memory"),None)
    if reserved is None:
        raise SystemExit("DTB rejected: missing reserved-memory node")
    for child in reserved["children"]:
        if "reg" not in child["props"]:
            raise SystemExit(f"DTB rejected: dynamic/firmware-owned reservation has no reg: {child['name']}")
    reserve_count = 0
    rp = reserve_off
    while rp+16 <= struct_off:
        address,size = struct.unpack_from(">QQ",data,rp); rp += 16
        if address == 0 and size == 0: break
        if size == 0 or address+size >= (1<<48):
            raise SystemExit("DTB rejected: malformed reservation-map entry")
        reserve_count += 1
    else:
        raise SystemExit("DTB rejected: unterminated reservation map")
    return data[:total], root_compat, reserve_count

def symbol_value(nm, elf, symbol):
    output = subprocess.check_output([nm, "-n", str(elf)], text=True)
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] == symbol:
            return int(parts[0],16)
    raise SystemExit(f"ELF rejected: missing linker symbol {symbol}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--dtb", required=True)
    ap.add_argument("--objcopy", required=True)
    ap.add_argument("--nm", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()
    elf, dtb, out = Path(args.elf), Path(args.dtb), Path(args.output)
    dtb_blob, compatible, reserve_count = parse_dtb(dtb)
    entry = symbol_value(args.nm, elf, "_start")
    kernel_end = symbol_value(args.nm, elf, "__kernel_end")
    image_base = 0
    entry_offset = entry - (image_base + TEXT_OFFSET)
    image_size = kernel_end - (image_base + TEXT_OFFSET)
    if entry_offset < 64 or entry_offset & 3 or image_size <= entry_offset:
        raise SystemExit("ELF rejected: entry/image size does not fit ARM64 Image header layout")
    raw = out.with_suffix(".raw")
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.check_call([args.objcopy, "-O", "binary", str(elf), str(raw)])
    code = raw.read_bytes()
    if entry_offset + len(code) > image_size:
        raise SystemExit("ELF rejected: binary exceeds linked image footprint")
    image = bytearray(entry_offset)
    branch = 0x14000000 | ((entry_offset // 4) & 0x03FFFFFF)
    header = struct.pack("<IIQQQQQQII", branch, 0, TEXT_OFFSET, image_size,
                         IMAGE_FLAGS, 0, 0, 0, 0x644D5241, 0)
    image[:64] = header
    image.extend(code)
    out_image = out.with_suffix(".Image")
    out_image.write_bytes(image)
    compressed = gzip.compress(image, compresslevel=9, mtime=0) + dtb_blob
    out.write_bytes(compressed)
    manifest = {
        "artifact": str(out),
        "format": "Image.gz-dtb",
        "elf": str(elf),
        "root_compatible": compatible,
        "text_offset": hex(TEXT_OFFSET),
        "link_entry": hex(entry),
        "entry_offset_from_image_start": hex(entry_offset),
        "image_memory_footprint": hex(image_size),
        "dtb_size": len(dtb_blob),
        "fdt_reservation_map_entries": reserve_count,
        "boot_img_created": False,
        "partition_written": False,
        "note": "Candidate kernel payload only. Build boot.img with the verified Violet ramdisk/header settings and test via fastboot boot; never flash."
    }
    out.with_suffix(".manifest.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    print(json.dumps(manifest,indent=2))

if __name__ == "__main__":
    main()
