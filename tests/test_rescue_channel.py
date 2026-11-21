#!/usr/bin/env python3
"""Test for the VQEAF rescue channel host tool (scripts/rescue_channel.py).

Creates a temporary FAT16 disk image, injects QEAF.CMD commands, then reads
back RESPONSE.TXT as if the OS had processed it. Mirrors the QeafdropOs
rescue channel (QEAF.CMD -> RESPONSE.TXT over FAT16 LUN).
"""

import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESCUE_CHANNEL = ROOT / "scripts" / "rescue_channel.py"

SECTOR = 512
PART_START = 2048
ROOT_LBA = PART_START + 1 + 2 * 32      # reserved + 2 FAT copies * 32 sectors
ROOT_ENTRIES = 512
DATA_LBA = ROOT_LBA + (ROOT_ENTRIES * 32 + SECTOR - 1) // SECTOR


def build_fat16_image(path: Path) -> None:
    """Build a minimal FAT16 image with an empty root directory."""
    total_sectors = 32768  # 16 MiB
    part_sectors = total_sectors - PART_START
    img = bytearray(total_sectors * SECTOR)

    # MBR
    mbr = bytearray(SECTOR)
    entry = bytearray(16)
    entry[0] = 0x80
    entry[1:4] = b"\x00\x02\x00"
    entry[4] = 0x06
    entry[5:8] = b"\xFE\xFF\xFF"
    struct.pack_into("<II", entry, 8, PART_START, part_sectors)
    mbr[446:462] = entry
    mbr[510:512] = b"\x55\xAA"
    img[0:SECTOR] = mbr

    # FAT16 boot sector
    bpb = bytearray(SECTOR)
    bpb[0:3] = b"\xEB\x3C\x90"
    bpb[3:11] = b"VQEAFOS "
    struct.pack_into("<H", bpb, 11, SECTOR)
    bpb[13] = 4  # sectors per cluster
    struct.pack_into("<H", bpb, 14, 1)  # reserved
    bpb[16] = 2  # FAT count
    struct.pack_into("<H", bpb, 17, ROOT_ENTRIES)
    struct.pack_into("<H", bpb, 19, part_sectors)
    bpb[21] = 0xF8
    struct.pack_into("<H", bpb, 22, 32)  # FAT sectors
    struct.pack_into("<H", bpb, 24, 63)
    struct.pack_into("<H", bpb, 26, 255)
    struct.pack_into("<I", bpb, 28, PART_START)
    struct.pack_into("<I", bpb, 32, 0)
    bpb[36] = 0x80
    bpb[38] = 0x29
    struct.pack_into("<I", bpb, 39, 0x1A2B3C4D)
    bpb[43:54] = b"VQEAF TEST "
    bpb[54:62] = b"FAT16   "
    bpb[510:512] = b"\x55\xAA"
    base = PART_START * SECTOR
    img[base:base + SECTOR] = bpb

    # FAT copies: mark cluster 2 as EOC (free marker)
    fat = bytearray(32 * SECTOR)
    struct.pack_into("<HHH", fat, 0, 0xFFF8, 0xFFFF, 0xFFFF)
    for copy in range(2):
        off = (PART_START + 1 + copy * 32) * SECTOR
        img[off:off + len(fat)] = fat

    path.write_bytes(img)


def find_cluster(data: bytearray, name: str) -> int:
    """Find starting cluster of an 8.3 root-dir entry."""
    stem, ext = name.upper().split(".")
    target = (stem.ljust(8) + ext.ljust(3)).encode("ascii")
    for i in range(ROOT_ENTRIES):
        off = ROOT_LBA * SECTOR + i * 32
        if data[off:off + 11] == target:
            return struct.unpack_from("<H", data, off + 26)[0]
    return -1


def cluster_lba(cluster: int) -> int:
    return DATA_LBA + (cluster - 2)


def write_file_with_entry(data: bytearray, name: str, content: str) -> None:
    """Write content to a file and create/update its root-dir entry."""
    cluster = find_cluster(data, name)
    content_bytes = content.encode("utf-8")
    if cluster < 0:
        cluster = 2
    off = cluster_lba(cluster) * SECTOR
    data[off:off + len(content_bytes)] = content_bytes

    # Create/update root-dir entry (8.3)
    stem, ext = name.upper().split(".")
    target = (stem.ljust(8) + ext.ljust(3)).encode("ascii")
    entry_found = -1
    for i in range(ROOT_ENTRIES):
        eoff = ROOT_LBA * SECTOR + i * 32
        if data[eoff] == 0x00 or data[eoff] == 0xE5:
            entry_found = eoff
            break
        if data[eoff:eoff + 11] == target:
            entry_found = eoff
            break
    if entry_found < 0:
        raise RuntimeError("root directory full")
    e = bytearray(32)
    e[:11] = target
    e[11] = 0x20
    struct.pack_into("<H", e, 26, cluster)
    struct.pack_into("<I", e, 28, len(content_bytes))
    data[entry_found:entry_found + 32] = e


def read_file(data: bytearray, name: str) -> str | None:
    cluster = find_cluster(data, name)
    if cluster < 0:
        return None
    off = cluster_lba(cluster) * SECTOR
    raw = data[off:off + SECTOR]
    nul = raw.find(b"\x00")
    if nul < 0:
        nul = len(raw)
    return raw[:nul].decode("utf-8", errors="replace")


def run_rescue(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(RESCUE_CHANNEL), *args],
        capture_output=True, text=True
    )


def main() -> int:
    failures = 0

    with tempfile.TemporaryDirectory(prefix="vqeaf-rescue-test-") as td:
        img_path = Path(td) / "test.img"
        build_fat16_image(img_path)

        # --- Test 1: inject status command ---
        r = run_rescue(["--image", str(img_path), "--cmd", "status"])
        if r.returncode != 0:
            print(f"FAIL: status injection exited {r.returncode}: {r.stderr}")
            failures += 1
        else:
            print("PASS: status command injected")

        # --- Test 2: inject version command ---
        r = run_rescue(["--image", str(img_path), "--cmd", "version"])
        if r.returncode != 0:
            print(f"FAIL: version injection exited {r.returncode}: {r.stderr}")
            failures += 1
        else:
            print("PASS: version command injected")

        # --- Test 3: inject echo command ---
        r = run_rescue(["--image", str(img_path), "--cmd", "echo hello rescue"])
        if r.returncode != 0:
            print(f"FAIL: echo injection exited {r.returncode}: {r.stderr}")
            failures += 1
        else:
            print("PASS: echo command injected")

        # --- Test 4: inject help command ---
        r = run_rescue(["--image", str(img_path), "--cmd", "help"])
        if r.returncode != 0:
            print(f"FAIL: help injection exited {r.returncode}: {r.stderr}")
            failures += 1
        else:
            print("PASS: help command injected")

        # --- Test 5: inject ls command ---
        r = run_rescue(["--image", str(img_path), "--cmd", "ls"])
        if r.returncode != 0:
            print(f"FAIL: ls injection exited {r.returncode}: {r.stderr}")
            failures += 1
        else:
            print("PASS: ls command injected")

        # --- Test 6: inject sync command ---
        r = run_rescue(["--image", str(img_path), "--cmd", "sync"])
        if r.returncode != 0:
            print(f"FAIL: sync injection exited {r.returncode}: {r.stderr}")
            failures += 1
        else:
            print("PASS: sync command injected")

        # --- Test 7: read response from image ---
        # Simulate OS having written RESPONSE.TXT
        data = bytearray(img_path.read_bytes())
        write_file_with_entry(data, "RESPONSE.TXT",
                              "status=ok\nos=VQEAF OS 0.2.0\nbuild=2026-11-10\ntarget=x86_64-uefi\nstorage=mounted\n")
        img_path.write_bytes(data)

        r = run_rescue(["--image", str(img_path), "--read"])
        if r.returncode != 0:
            print(f"FAIL: read response exited {r.returncode}: {r.stderr}")
            failures += 1
        elif "status=ok" not in r.stdout:
            print(f"FAIL: response missing status=ok: {r.stdout}")
            failures += 1
        else:
            print("PASS: response read back correctly")

        # --- Test 8: read response to file ---
        out_path = Path(td) / "response.txt"
        r = run_rescue(["--image", str(img_path), "--read", "--out", str(out_path)])
        if r.returncode != 0:
            print(f"FAIL: read-to-file exited {r.returncode}: {r.stderr}")
            failures += 1
        elif not out_path.is_file():
            print("FAIL: output file not created")
            failures += 1
        else:
            content = out_path.read_text()
            if "status=ok" in content:
                print("PASS: response written to output file")
            else:
                print(f"FAIL: output file missing content: {content}")
                failures += 1

        # --- Test 9: missing image ---
        r = run_rescue(["--image", str(Path(td) / "nonexistent.img"), "--cmd", "status"])
        if r.returncode == 0:
            print("FAIL: missing image should fail")
            failures += 1
        else:
            print("PASS: missing image correctly rejected")

    if failures:
        print(f"\n{failures} test(s) FAILED")
        return 1
    print("\nAll rescue channel tests PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
