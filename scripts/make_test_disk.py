"""Generate a deterministic MBR + FAT16 test disk for a block driver.

This is a test fixture, never a device firmware image. Two instances are built:
one attached to QEMU as a virtio-blk drive, one as a SATA drive. Build/test
scripts write only inside this project and QEMU virtual machines.
"""
import argparse
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "build" / "virtio-test.img"
SECTOR = 512
TOTAL_SECTORS = 32768              # 16 MiB
PART_START = 2048
PART_SECTORS = TOTAL_SECTORS - PART_START
BYTES_PER_SECTOR = 512
SECTORS_PER_CLUSTER = 4
RESERVED = 1
FAT_COUNT = 2
FAT_SECTORS = 32
ROOT_ENTRIES = 512
FILE_NAME = "VQEAF.TXT"
FILE_CONTENT = b"VQEAF OS virtio-blk FAT16 read-only mount OK\n"


def build_image(out: Path = DEFAULT_OUT, file_name: str = FILE_NAME,
                content: bytes = FILE_CONTENT) -> Path:
    out.parent.mkdir(parents=True, exist_ok=True)
    img = bytearray(TOTAL_SECTORS * SECTOR)

    # ---- MBR with a single FAT16 partition ----
    mbr = bytearray(SECTOR)
    entry = bytearray(16)
    entry[0] = 0x80                 # bootable
    entry[1:4] = b"\x00\x02\x00"    # CHS start (not used by our parser)
    entry[4] = 0x06                 # FAT16B
    entry[5:8] = b"\xFE\xFF\xFF"    # CHS end
    struct.pack_into("<II", entry, 8, PART_START, PART_SECTORS)
    mbr[446:462] = entry
    mbr[510:512] = b"\x55\xAA"
    img[0:SECTOR] = mbr

    # ---- FAT16 volume boot sector ----
    bpb = bytearray(SECTOR)
    bpb[0:3] = b"\xEB\x3C\x90"      # jump
    bpb[3:11] = b"VQEAFOS "         # OEM
    struct.pack_into("<H", bpb, 11, BYTES_PER_SECTOR)
    bpb[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", bpb, 14, RESERVED)
    bpb[16] = FAT_COUNT
    struct.pack_into("<H", bpb, 17, ROOT_ENTRIES)
    struct.pack_into("<H", bpb, 19, PART_SECTORS)   # total sectors 16-bit
    bpb[21] = 0xF8                  # media
    struct.pack_into("<H", bpb, 22, FAT_SECTORS)
    struct.pack_into("<H", bpb, 24, 63)   # sectors per track
    struct.pack_into("<H", bpb, 26, 255)  # heads
    struct.pack_into("<I", bpb, 28, PART_START)  # hidden sectors
    struct.pack_into("<I", bpb, 32, 0)   # total sectors 32-bit (unused)
    bpb[36] = 0x80                  # drive number
    bpb[38] = 0x29                  # extended boot signature
    struct.pack_into("<I", bpb, 39, 0x1A2B3C4D)
    bpb[43:54] = b"VQEAF TEST "   # 11-byte volume label
    bpb[54:62] = b"FAT16   "
    bpb[510:512] = b"\x55\xAA"
    base = PART_START * SECTOR
    img[base:base + SECTOR] = bpb

    # ---- FAT copies: cluster 2 -> EOC ----
    fat = bytearray(FAT_SECTORS * SECTOR)
    struct.pack_into("<HHH", fat, 0, 0xFFF8, 0xFFFF, 0xFFFF)
    for copy in range(FAT_COUNT):
        off = (PART_START + RESERVED + copy * FAT_SECTORS) * SECTOR
        img[off:off + len(fat)] = fat

    # ---- Root directory: VQEAF.TXT -> cluster 2 ----
    root_lba = PART_START + RESERVED + FAT_COUNT * FAT_SECTORS
    root = bytearray(ROOT_ENTRIES * 32)
    stem, ext = file_name.split(".")
    root[0:11] = (stem.ljust(8) + ext.ljust(3)).encode("ascii")
    root[11] = 0x20
    struct.pack_into("<H", root, 26, 2)
    struct.pack_into("<I", root, 28, len(content))
    img[root_lba * SECTOR:root_lba * SECTOR + len(root)] = root

    # ---- Data cluster 2 ----
    data_lba = root_lba + (ROOT_ENTRIES * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
    img[data_lba * SECTOR:data_lba * SECTOR + len(content)] = content

    out.write_bytes(img)
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--name", default=FILE_NAME)
    ap.add_argument("--content", default=None)
    args = ap.parse_args()
    content = args.content.encode() if args.content is not None else None
    path = build_image(args.out, args.name) if content is None else build_image(args.out, args.name, content)
    print(f"Created {path} bytes={path.stat().st_size} FAT16 partition at LBA {PART_START}")
