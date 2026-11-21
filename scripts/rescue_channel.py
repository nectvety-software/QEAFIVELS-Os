#!/usr/bin/env python3
"""Host-side rescue channel tool for VQEAF OS.

Sends a command to a running VQEAF OS instance by injecting QEAF.CMD into a
QEMU FAT16 disk image, then extracts RESPONSE.TXT after the OS has processed
it. This mirrors the QeafdropOs rescue channel (QEAF.CMD -> RESPONSE.TXT over
a FAT16 LUN) adapted to the x86_64 UEFI reference.

Usage:
    # Inject a command into the storage disk image
    python scripts/rescue_channel.py --image build/virtio-test.img --cmd "status"

    # Inject and then run QEMU separately; after boot, extract the response
    python scripts/rescue_channel.py --image build/virtio-test.img --cmd "version" --out out/rescue_response.txt

    # Read a response from an already-booted image
    python scripts/rescue_channel.py --image build/virtio-test.img --read
"""

import argparse
import struct
import sys
from pathlib import Path

SECTOR = 512
PART_START = 2048
ROOT_LBA = PART_START + 1 + 2 * 32      # reserved + 2 FAT copies * 32 sectors
ROOT_ENTRIES = 512
DATA_LBA = ROOT_LBA + (ROOT_ENTRIES * 32 + SECTOR - 1) // SECTOR


def read_bytes(path: Path) -> bytearray:
    return bytearray(path.read_bytes())


def write_bytes(path: Path, data: bytearray) -> None:
    path.write_bytes(data)


def find_cluster(data: bytearray, name: str) -> int:
    """Find the starting cluster of an 8.3 root-dir entry."""
    stem, ext = name.upper().split(".")
    target = (stem.ljust(8) + ext.ljust(3)).encode("ascii")
    for i in range(ROOT_ENTRIES):
        off = ROOT_LBA * SECTOR + i * 32
        if data[off:off + 11] == target:
            return struct.unpack_from("<H", data, off + 26)[0]
    return -1


def cluster_lba(cluster: int) -> int:
    return DATA_LBA + (cluster - 2)


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


def write_file(data: bytearray, name: str, content: str) -> None:
    cluster = find_cluster(data, name)
    content_bytes = content.encode("utf-8")
    if cluster < 0:
        # Allocate cluster 2 if free (FAT[2] == 0xFFFF means EOC/free in our fixture)
        cluster = 2
    off = cluster_lba(cluster) * SECTOR
    data[off:off + len(content_bytes)] = content_bytes


def main() -> int:
    ap = argparse.ArgumentParser(description="VQEAF rescue channel host tool")
    ap.add_argument("--image", required=True, type=Path, help="FAT16 disk image path")
    ap.add_argument("--cmd", default=None, help="Command verb to inject (e.g. status, version, echo hello)")
    ap.add_argument("--read", action="store_true", help="Read RESPONSE.TXT from image")
    ap.add_argument("--out", type=Path, default=None, help="Write response to this file instead of stdout")
    args = ap.parse_args()

    if not args.image.is_file():
        print("Image not found: " + str(args.image), file=sys.stderr)
        return 2

    data = read_bytes(args.image)
    changed = False

    if args.cmd is not None:
        write_file(data, "QEAF.CMD", args.cmd + "\n")
        changed = True
        print("Injected QEAF.CMD:", args.cmd)

    if args.read:
        resp = read_file(data, "RESPONSE.TXT")
        if resp is None:
            print("RESPONSE.TXT not found in image", file=sys.stderr)
            return 3
        target = args.out if args.out else sys.stdout
        if args.out:
            args.out.write_text(resp)
            print("Wrote response to", args.out)
        else:
            print(resp, end="")

    if changed:
        write_bytes(args.image, data)
        print("Image updated.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
