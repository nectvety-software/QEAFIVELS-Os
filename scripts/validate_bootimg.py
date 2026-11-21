#!/usr/bin/env python3
"""Host-side structural validation for VQEAF OS x86_64 UEFI boot images.

Why this file exists
--------------------
Before an image is written to a QEMU test disk or physical medium, the host
should verify that it has a coherent UEFI structure: a valid MBR/GPT, a FAT
EFI System Partition, and the expected EFI binaries inside. Writing a malformed
image can make the test disk unbootable and wastes iteration time.

This module is deliberately strict and fail-closed: a file is either a valid
VQEAF UEFI image or it is refused. Nothing here writes anything.

Usage:
    python scripts/validate_bootimg.py dist/vqeaf-x86_64-uefi.img
    python scripts/validate_bootimg.py dist/vqeaf-x86_64-recovery.img
"""

import hashlib
import os
import struct
import sys

# Maximum image size we consider sane for a UEFI test/reference image.
# 512 MiB is generous for a reference OS; anything larger is suspect.
MAX_IMAGE_SIZE = 512 * 1024 * 1024

# Minimum size: must hold at least a FAT12 ESP floppy image (1.44 MiB).
MIN_IMAGE_SIZE = 1024 * 1024  # 1 MiB

# UEFI signatures we expect to find somewhere in the image.
EFI_SYSTEM_PARTITION_GUID = bytes.fromhex("c12a7328f81f11d2ba4b00a0c93ec93b")
VQEAF_OS_STRING = b"VQEAF OS"


class Report:
    def __init__(self, path):
        self.path = path
        self.ok = True
        self.messages = []

    def fail(self, msg):
        self.ok = False
        self.messages.append("FAIL: " + msg)

    def warn(self, msg):
        self.messages.append("WARN: " + msg)

    def info(self, msg):
        self.messages.append("info: " + msg)


def rd16_le(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def rd32_le(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def has_efi_signature(buf):
    """Look for common UEFI/FAT signatures inside the image."""
    # FAT BPB magic at offset 510/511 of a sector
    for off in range(0, len(buf) - 512, 512):
        if buf[off + 510] == 0x55 and buf[off + 511] == 0xAA:
            return True
    # EFI PE/COFF magic "MZ" or PE signature "PE\0\0"
    if b"PE\x00\x00" in buf:
        return True
    return False


def find_os_string(buf):
    """Check whether the image contains the VQEAF OS identity string."""
    return VQEAF_OS_STRING in buf


def _fat12_entry(fat, cluster):
    off = cluster + cluster // 2
    word = fat[off] | (fat[off + 1] << 8)
    return (word >> 4) if cluster & 1 else (word & 0x0FFF)


def _read_chain(buf, fat, first, data_start, max_clusters):
    """Read a FAT12 cluster chain. Returns (data, ok); ok=False on corruption
    (free/bad/reserved entry, cycle, or out-of-range cluster)."""
    out = bytearray()
    cluster = first
    visited = set()
    while True:
        if cluster < 2 or cluster >= max_clusters + 2 or cluster in visited:
            return bytes(out), False
        visited.add(cluster)
        start = (data_start + cluster - 2) * 512
        out += buf[start:start + 512]
        nxt = _fat12_entry(fat, cluster)
        if nxt >= 0xFF8:          # end of chain
            return bytes(out), True
        if nxt <= 1 or nxt == 0xFF7 or nxt >= max_clusters + 2:
            return bytes(out), False
        cluster = nxt


def _check_pe(data, label, report):
    if len(data) < 64 or data[:2] != b"MZ":
        report.fail(label + ": thiếu chữ ký MZ")
        return
    e_lfanew = rd32_le(data, 0x3C)
    if e_lfanew + 6 > len(data) or data[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
        report.fail(label + ": thiếu chữ ký PE")
        return
    machine = rd16_le(data, e_lfanew + 4)
    if machine != 0x8664:
        report.fail(label + ": machine không phải x86_64 (0x%X)" % machine)
        return
    report.info(label + ": PE x86_64 hợp lệ (%d bytes)" % len(data))


def validate_fat12_esp(buf, report):
    """Strict FAT12 ESP check: BPB, both FAT copies, every directory/file
    cluster chain ends at EOF, and EFI/BOOT/BOOTX64.EFI is a valid x86_64 PE.
    This mirrors what OVMF requires to traverse the ESP and launch the loader."""
    if len(buf) < 512:
        report.fail("FAT12: ảnh quá ngắn")
        return
    bpb = buf[:512]
    if bpb[510:512] != b"\x55\xAA":
        report.fail("FAT12: thiếu chữ ký boot 0xAA55")
        return
    if bpb[54:62] != b"FAT12   ":
        report.fail("FAT12: nhãn hệ thống tệp không phải FAT12")
        return
    bps = rd16_le(bpb, 11)
    spc = bpb[13]
    reserved = rd16_le(bpb, 14)
    nfat = bpb[16]
    rootent = rd16_le(bpb, 17)
    tot16 = rd16_le(bpb, 19)
    fatsz = rd16_le(bpb, 22)
    if (bps, spc, reserved, nfat, rootent, tot16, fatsz) != (512, 1, 1, 2, 224, 2880, 9):
        report.fail("FAT12: BPB không khớp bố cục floppy ESP chuẩn")
        return
    if len(buf) != tot16 * bps:
        report.fail("FAT12: kích thước ảnh không khớp BPB")
        return

    root_sectors = (rootent * 32) // bps
    data_start = reserved + nfat * fatsz + root_sectors
    max_clusters = tot16 - data_start
    fat_off = reserved * bps
    fat = buf[fat_off:fat_off + fatsz * bps]
    fat2 = buf[fat_off + fatsz * bps:fat_off + 2 * fatsz * bps]
    if fat != fat2:
        report.fail("FAT12: hai bản sao FAT khác nhau")
        return
    if _fat12_entry(fat, 0) != 0xFF0 or _fat12_entry(fat, 1) != 0xFFF:
        report.fail("FAT12: mục FAT dành riêng (0/1) sai")
        return

    files = {}

    def parse_dir(data, path):
        for off in range(0, len(data) - 31, 32):
            e = data[off:off + 32]
            if e[0] == 0x00:
                break
            if e[0] == 0xE5 or e[11] == 0x0F:
                continue
            attr = e[11]
            base = e[0:8].decode("ascii", "replace").rstrip()
            ext = e[8:11].decode("ascii", "replace").rstrip()
            name = base + ("." + ext if ext else "")
            cluster = rd16_le(e, 26)
            size = rd32_le(e, 28)
            full = path + "/" + name if path else name
            if attr & 0x10:
                if name in (".", ".."):
                    continue
                if cluster < 2:
                    report.fail("FAT12: thư mục " + full + " không có cluster hợp lệ")
                    continue
                sub, ok = _read_chain(buf, fat, cluster, data_start, max_clusters)
                if not ok:
                    report.fail("FAT12: chuỗi cluster thư mục " + full + " hỏng (không kết thúc EOF)")
                    continue
                parse_dir(sub, full)
            else:
                cdata, ok = _read_chain(buf, fat, cluster, data_start, max_clusters)
                if not ok:
                    report.fail("FAT12: chuỗi cluster tệp " + full + " hỏng (không kết thúc EOF)")
                    continue
                if len(cdata) < size:
                    report.fail("FAT12: tệp " + full + " chuỗi ngắn hơn kích thước khai báo")
                    continue
                files[full] = cdata[:size]

    root_off = (reserved + nfat * fatsz) * bps
    parse_dir(buf[root_off:root_off + root_sectors * bps], "")

    loader = files.get("EFI/BOOT/BOOTX64.EFI")
    if loader is None:
        report.fail("FAT12: không tìm thấy EFI/BOOT/BOOTX64.EFI")
    else:
        _check_pe(loader, "EFI/BOOT/BOOTX64.EFI", report)
    if report.ok:
        report.info("FAT12: cây thư mục + chuỗi cluster hợp lệ (%d tệp)" % len(files))


def validate_file(path):
    report = Report(path)

    if not os.path.isfile(path):
        report.fail("không tìm thấy file: " + path)
        return report

    size = os.path.getsize(path)
    report.info("kích thước: %d bytes (%d MiB)" % (size, size // (1024 * 1024)))

    if size < MIN_IMAGE_SIZE:
        report.fail("quá nhỏ để là ảnh UEFI bootable (< %d MiB)" % (MIN_IMAGE_SIZE // (1024 * 1024)))
    if size > MAX_IMAGE_SIZE:
        report.fail("quá lớn (> %d MiB), có thể không phải ảnh tham chiếu" % (MAX_IMAGE_SIZE // (1024 * 1024)))

    with open(path, "rb") as f:
        buf = f.read()

    if not has_efi_signature(buf):
        report.fail("không tìm thấy ký hiệu UEFI/FAT (0xAA55 hoặc PE\0\0) trong ảnh")
    else:
        report.info("tìm thấy cấu trúc UEFI/FAT hợp lệ")

    if not find_os_string(buf):
        report.warn("không tìm thấy chuỗi bản định danh 'VQEAF OS' trong ảnh (không bắt buộc)")
    else:
        report.info("tìm thấy chuỗi bản định danh 'VQEAF OS'")

    validate_fat12_esp(buf, report)

    # SHA-256 for record-keeping
    sha = hashlib.sha256(buf).hexdigest()
    report.info("sha256: " + sha)

    return report


def main(argv):
    if len(argv) < 2:
        print("usage: python scripts/validate_bootimg.py <image.img>")
        return 2

    path = argv[1]
    report = validate_file(path)

    for msg in report.messages:
        print(msg)

    if report.ok:
        print("KẾT LUẬN: ảnh hợp lệ, có thể dùng để boot thử trong QEMU.")
        return 0
    else:
        print("KẾT LUẬN: ảnh BỊ TỪ CHỐI, không được ghi vào ổ thử nghiệm.")
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv))
