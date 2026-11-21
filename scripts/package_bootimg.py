#!/usr/bin/env python3
"""Package a VQEAF OS x86_64 UEFI boot image (FAT12 ESP) and validate it.

Pipeline, in order:

    build/BOOTX64.EFI, build/KERNEL.EFI, build/RECOVERY.EFI   (CMake build)
        -> FAT12 ESP image with EFI/BOOT/BOOTX64.EFI + EFI/VQEAF/*
        -> validate_bootimg.py (structural guard)
        -> dist/vqeaf-x86_64-uefi.img  or  dist/vqeaf-x86_64-recovery.img

This is the host-side packager for the reference OS. It writes only inside
this project and never touches physical disks. Use scripts/flash_uefi.py to
write the image to a USB install drive.

Usage:
    python scripts/package_bootimg.py --build build --out dist/vqeaf-x86_64-uefi.img --entry normal
    python scripts/package_bootimg.py --build build --out dist/vqeaf-x86_64-recovery.img --entry recovery
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path

SECTOR = 512
TOTAL = 2880          # 1.44 MiB floppy-sized ESP image
FAT_SECTORS = 9
ROOT_ENTRIES = 224
ROOT_SECTORS = (ROOT_ENTRIES * 32) // SECTOR
DATA_START = 1 + 2 * FAT_SECTORS + ROOT_SECTORS
MAX_CLUSTERS = TOTAL - DATA_START

# VQEAF OS identity — single source of truth, matches system/core/include/vqeaf/version.h
VQEAF_OS_STRING = "VQEAF OS 0.2.0"


def build_image(build: Path, entry: str) -> bytes:
    """Build a FAT12 ESP image containing the EFI binaries."""
    sources = {
        "EFI/BOOT/BOOTX64.EFI": build / "BOOTX64.EFI",
        "EFI/VQEAF/KERNEL.EFI": build / "KERNEL.EFI",
        "EFI/VQEAF/RECOVERY.EFI": build / "RECOVERY.EFI",
    }
    if entry == "recovery":
        sources["EFI/BOOT/BOOTX64.EFI"] = build / "RECOVERY.EFI"

    for path in sources.values():
        if not path.is_file():
            raise SystemExit("Missing EFI binary: " + str(path))

    image = bytearray(TOTAL * SECTOR)
    fat_bytes = bytearray(FAT_SECTORS * SECTOR)
    fat_bytes[0:3] = b"\xF0\xFF\xFF"

    def set_fat(cluster: int, value: int) -> None:
        offset = cluster + cluster // 2
        word = fat_bytes[offset] | (fat_bytes[offset + 1] << 8)
        if cluster & 1:
            word = (word & 0x000F) | ((value & 0x0FFF) << 4)
        else:
            word = (word & 0xF000) | (value & 0x0FFF)
        fat_bytes[offset] = word & 0xFF
        fat_bytes[offset + 1] = (word >> 8) & 0xFF

    next_cluster = 2
    dirs = {"": {}, "EFI": {}, "EFI/BOOT": {}, "EFI/VQEAF": {}}
    for path, source in sources.items():
        parent, name = path.rsplit("/", 1)
        dirs[parent][name] = ("file", source.read_bytes())

    dirs[""]["EFI"] = ("dir", "EFI")
    startup = b"FS0:\\EFI\\BOOT\\BOOTX64.EFI\r\n" if entry == "normal" else b"FS0:\\EFI\\VQEAF\\RECOVERY.EFI\r\n"
    dirs[""]["STARTUP.NSH"] = ("file", startup)
    dirs["EFI"]["BOOT"] = ("dir", "EFI/BOOT")
    dirs["EFI"]["VQEAF"] = ("dir", "EFI/VQEAF")

    dir_cluster = {}
    for name in ("EFI", "EFI/BOOT", "EFI/VQEAF"):
        dir_cluster[name] = next_cluster
        # A directory occupies exactly one cluster; OVMF refuses to traverse
        # it unless its FAT entry marks end-of-chain instead of free (0).
        set_fat(next_cluster, 0xFFF)
        next_cluster += 1

    file_alloc = {}
    for parent, items in dirs.items():
        for name, (kind, value) in items.items():
            if kind == "file":
                count = max(1, (len(value) + SECTOR - 1) // SECTOR)
                first = next_cluster
                if first + count > MAX_CLUSTERS + 2:
                    raise SystemExit("FAT12 image capacity exceeded")
                for i in range(count):
                    cluster = next_cluster
                    next_cluster += 1
                    end_val = next_cluster if i < count - 1 else 0xFFF
                    set_fat(cluster, end_val)
                    start = (DATA_START + cluster - 2) * SECTOR
                    part = value[i * SECTOR:(i + 1) * SECTOR]
                    image[start:start + len(part)] = part
                file_alloc[(parent, name)] = (first, len(value))

    def short_name(name: str) -> bytes:
        if name == ".":
            return b".          "
        if name == "..":
            return b"..         "
        pieces = name.upper().split(".")
        base = pieces[0][:8].ljust(8)
        ext = (pieces[1][:3] if len(pieces) > 1 else "").ljust(3)
        if len(pieces) > 2:
            raise SystemExit("Only 8.3 names are supported: " + name)
        return (base + ext).encode("ascii")

    def entry_bytes(name: str, attr: int, cluster: int, size: int) -> bytes:
        item = bytearray(32)
        item[:11] = short_name(name)
        item[11] = attr
        struct.pack_into("<H", item, 26, cluster)
        struct.pack_into("<I", item, 28, size)
        return bytes(item)

    def make_dir(path: str, parent_cluster: int) -> bytes:
        rows = []
        if path:
            own = dir_cluster[path]
            rows.append(entry_bytes(".", 0x10, own, 0))
            rows.append(entry_bytes("..", 0x10, parent_cluster, 0))
        for name, (kind, value) in dirs[path].items():
            if kind == "dir":
                rows.append(entry_bytes(name, 0x10, dir_cluster[value], 0))
            else:
                first, size = file_alloc[(path, name)]
                rows.append(entry_bytes(name, 0x20, first, size))
        data = b"".join(rows) + b"\x00"
        if len(data) > SECTOR:
            raise SystemExit("Directory exceeds one FAT12 cluster: " + path)
        return data.ljust(SECTOR, b"\x00")

    for path in ("EFI/BOOT", "EFI/VQEAF", "EFI"):
        parent = 0 if path == "EFI" else dir_cluster[path.rsplit("/", 1)[0]]
        data = make_dir(path, parent)
        start = (DATA_START + dir_cluster[path] - 2) * SECTOR
        image[start:start + SECTOR] = data

    root = make_dir("", 0).ljust(ROOT_SECTORS * SECTOR, b"\x00")
    root_offset = (1 + 2 * FAT_SECTORS) * SECTOR
    image[root_offset:root_offset + ROOT_SECTORS * SECTOR] = root

    # FAT12 BIOS parameter block
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

    return bytes(image)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description="Package VQEAF UEFI boot image")
    ap.add_argument("--build", required=True, type=Path, help="CMake build directory")
    ap.add_argument("--out", required=True, type=Path, help="Output image path")
    ap.add_argument("--entry", choices=("normal", "recovery"), default="normal")
    args = ap.parse_args()

    image = build_image(args.build, args.entry)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image)

    sha = sha256(out)
    print(f"Created {out} bytes={len(image)} sha256={sha} entry={args.entry}")

    # Validate the packaged image (same guard QeafdropOs uses before flashing)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from validate_bootimg import validate_file
    report = validate_file(str(out))
    for msg in report.messages:
        print(msg)
    if not report.ok:
        print("PACKAGE REJECTED: image failed validation.")
        return 3
    print("PACKAGE OK: image passed structural validation.")
    print("")
    print("Flash it to a USB install drive:")
    print(f"  python scripts/flash_uefi.py --image {out} --volume <USB drive letter>")
    print("  # then add --commit to the same command to agree to the write")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
