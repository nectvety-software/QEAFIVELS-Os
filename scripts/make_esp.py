#!/usr/bin/env python3
"""Create a deterministic FAT12 EFI floppy image; never touches physical disks."""
import argparse
import struct
from pathlib import Path

SECTOR = 512
TOTAL = 2880
FAT_SECTORS = 9
ROOT_ENTRIES = 224
ROOT_SECTORS = (ROOT_ENTRIES * 32) // SECTOR
DATA_START = 1 + 2 * FAT_SECTORS + ROOT_SECTORS
MAX_CLUSTERS = TOTAL - DATA_START
image = bytearray(TOTAL * SECTOR)
fat = [0] * (FAT_SECTORS * SECTOR * 2 // 3)
fat[0], fat[1] = 0xFF0, 0xFFF
next_cluster = 2

def set_fat12(cluster, value):
    offset = cluster + cluster // 2
    word = fat_bytes[offset] | (fat_bytes[offset + 1] << 8)
    if cluster & 1:
        word = (word & 0x000F) | ((value & 0x0FFF) << 4)
    else:
        word = (word & 0xF000) | (value & 0x0FFF)
    fat_bytes[offset] = word & 0xFF
    fat_bytes[offset + 1] = (word >> 8) & 0xFF

def allocate(data):
    global next_cluster
    count = max(1, (len(data) + SECTOR - 1) // SECTOR)
    first = next_cluster
    if first + count > MAX_CLUSTERS + 2:
        raise ValueError("FAT12 image capacity exceeded")
    for i in range(count):
        cluster = next_cluster
        next_cluster += 1
        set_fat12(cluster, next_cluster if i < count - 1 else 0xFFF)
        start = (DATA_START + cluster - 2) * SECTOR
        part = data[i * SECTOR:(i + 1) * SECTOR]
        image[start:start + len(part)] = part
    return first, count

def short_name(name):
    if name == ".": return b".          "
    if name == "..": return b"..         "
    pieces = name.upper().split(".")
    base = pieces[0][:8].ljust(8)
    ext = (pieces[1][:3] if len(pieces) > 1 else "").ljust(3)
    if len(pieces) > 2:
        raise ValueError("Only 8.3 names are supported: " + name)
    return (base + ext).encode("ascii")

def entry(name, attr, cluster, size):
    item = bytearray(32)
    item[:11] = short_name(name)
    item[11] = attr
    struct.pack_into("<H", item, 26, cluster)
    struct.pack_into("<I", item, 28, size)
    return bytes(item)

def main():
    global fat_bytes
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--entry", choices=("normal", "recovery"), default="normal")
    args = parser.parse_args()
    build = Path(args.build)
    sources = {
        "EFI/BOOT/BOOTX64.EFI": build / "BOOTX64.EFI",
        "EFI/VQEAF/KERNEL.EFI": build / "KERNEL.EFI",
        "EFI/VQEAF/RECOVERY.EFI": build / "RECOVERY.EFI",
    }
    if args.entry == "recovery":
        sources["EFI/BOOT/BOOTX64.EFI"] = build / "RECOVERY.EFI"
    for path in sources.values():
        if not path.is_file():
            raise SystemExit("Missing EFI binary: " + str(path))
    fat_bytes = bytearray(FAT_SECTORS * SECTOR)
    fat_bytes[0:3] = b"\xF0\xFF\xFF"
    dirs = {"": {}, "EFI": {}, "EFI/BOOT": {}, "EFI/VQEAF": {}}
    for path, source in sources.items():
        parent, name = path.rsplit("/", 1)
        dirs[parent][name] = ("file", source.read_bytes())
    dirs[""]["EFI"] = ("dir", "EFI")
    startup = b"FS0:\\EFI\\BOOT\\BOOTX64.EFI\r\n" if args.entry == "normal" else b"FS0:\\EFI\\VQEAF\\RECOVERY.EFI\r\n"
    dirs[""]["STARTUP.NSH"] = ("file", startup)
    dirs["EFI"]["BOOT"] = ("dir", "EFI/BOOT")
    dirs["EFI"]["VQEAF"] = ("dir", "EFI/VQEAF")
    dir_cluster = {}
    for name in ("EFI", "EFI/BOOT", "EFI/VQEAF"):
        dir_cluster[name] = allocate(bytes(SECTOR))[0]
    file_alloc = {}
    for parent, items in dirs.items():
        for name, (kind, value) in items.items():
            if kind == "file":
                file_alloc[(parent, name)] = allocate(value)
    def make_dir(path, parent_cluster):
        rows = []
        if path:
            own = dir_cluster[path]
            rows.append(entry(".", 0x10, own, 0))
            rows.append(entry("..", 0x10, parent_cluster, 0))
        for name, (kind, value) in dirs[path].items():
            if kind == "dir":
                rows.append(entry(name, 0x10, dir_cluster[value], 0))
            else:
                first, count = file_alloc[(path, name)]
                rows.append(entry(name, 0x20, first, len(value)))
        data = b"".join(rows) + b"\x00"
        if len(data) > SECTOR:
            raise ValueError("Directory exceeds one FAT12 cluster")
        return data.ljust(SECTOR, b"\x00")
    for path in ("EFI/BOOT", "EFI/VQEAF", "EFI"):
        parent = 0 if path == "EFI" else dir_cluster[path.rsplit("/", 1)[0]]
        data = make_dir(path, parent)
        start = (DATA_START + dir_cluster[path] - 2) * SECTOR
        image[start:start + SECTOR] = data
    root = make_dir("", 0).ljust(ROOT_SECTORS * SECTOR, b"\x00")
    root_offset = (1 + 2 * FAT_SECTORS) * SECTOR
    image[root_offset:root_offset + ROOT_SECTORS * SECTOR] = root
    # FAT12 BIOS parameter block (UEFI firmware reads this as a FAT volume).
    boot = bytearray(SECTOR)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"VQEAFOS "
    struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, 1, 1, 2, ROOT_ENTRIES, TOTAL, 0xF0, FAT_SECTORS, 18, 2, 0, 0)
    boot[36] = 0
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0x56414551)
    boot[43:54] = b"VQEAF EFI  "
    boot[54:62] = b"FAT12   "
    boot[510:512] = b"\x55\xAA"
    image[:SECTOR] = boot
    for copy in range(2):
        start = (1 + copy * FAT_SECTORS) * SECTOR
        image[start:start + len(fat_bytes)] = fat_bytes
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image)
    print("Created", out, "bytes=", len(image), "FAT12 clusters used=", next_cluster - 2)

if __name__ == "__main__":
    main()
