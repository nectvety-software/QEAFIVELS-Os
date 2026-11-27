#!/usr/bin/env python3
"""Verify a rebuilt Violet initramfs without booting or writing device partitions."""
import gzip
import hashlib
import json
import os
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/violet"
IMAGE = ROOT / "dist/boot-vqeaf-violet.img"
INIT = BUILD / "vqeaf-init"
RAMDISK = BUILD / "qeaf-initramfs.cpio.gz"
DEVNODES = ROOT / "platform/violet/devnodes.c"
MAIN = ROOT / "platform/violet/main_violet.c"
MANIFEST = BUILD / "diagnostic-ramdisk.manifest.json"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def parse_newc(blob):
    pos = 0
    rows = {}
    while pos + 110 <= len(blob):
        hdr = blob[pos:pos + 110]
        if hdr[:6] not in (b"070701", b"070702"):
            raise ValueError(f"bad newc magic at {pos}")
        v = [int(hdr[6 + i * 8:14 + i * 8], 16) for i in range(13)]
        mode, size, namesz = v[1], v[6], v[11]
        no = pos + 110
        if namesz < 2 or no + namesz > len(blob):
            raise ValueError("invalid newc name")
        name = blob[no:no + namesz - 1].decode("utf-8")
        dataoff = (no + namesz + 3) & ~3
        end = dataoff + size
        if end > len(blob):
            raise ValueError(f"truncated newc entry: {name}")
        data = blob[dataoff:end]
        if name == "TRAILER!!!":
            if any(blob[end:]):
                raise ValueError("non-zero bytes after CPIO trailer")
            return rows
        if name in rows:
            raise ValueError(f"duplicate CPIO entry: {name}")
        rows[name] = (mode, data, v)
        pos = (end + 3) & ~3
    raise ValueError("newc trailer missing")


def boot_ramdisk(image):
    if image[:8] != b"ANDROID!":
        raise ValueError("base boot image lacks ANDROID! header")
    kernel_size = struct.unpack_from("<I", image, 8)[0]
    ramdisk_size = struct.unpack_from("<I", image, 16)[0]
    page = struct.unpack_from("<I", image, 36)[0]
    if page != 4096 or not kernel_size or not ramdisk_size:
        raise ValueError("unexpected Violet boot image header")
    offset = page + ((kernel_size + page - 1) // page) * page
    compressed = image[offset:offset + ramdisk_size]
    if len(compressed) != ramdisk_size:
        raise ValueError("base ramdisk truncated")
    return gzip.decompress(compressed)


def check_elf(data):
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("/init is not ELF64 little-endian")
    if struct.unpack_from("<H", data, 18)[0] != 183:
        raise ValueError("/init is not AArch64")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize = struct.unpack_from("<H", data, 54)[0]
    phnum = struct.unpack_from("<H", data, 56)[0]
    if phentsize < 56 or phoff + phentsize * phnum > len(data):
        raise ValueError("invalid ELF program header table")
    for i in range(phnum):
        if struct.unpack_from("<I", data, phoff + i * phentsize)[0] == 3:
            raise ValueError("/init is dynamically linked (PT_INTERP present)")


def main():
    for p in (IMAGE, INIT, RAMDISK, DEVNODES, MAIN):
        if not p.is_file():
            raise SystemExit(f"MISSING={p}")

    image = IMAGE.read_bytes()
    init = INIT.read_bytes()
    kernel_size = struct.unpack_from("<I", image, 8)[0]
    page_size = struct.unpack_from("<I", image, 36)[0]
    kernel_payload = image[page_size:page_size + kernel_size]
    old = parse_newc(boot_ramdisk(image))
    new = parse_newc(gzip.decompress(RAMDISK.read_bytes()))
    init_key = "./init" if "./init" in new else "init"
    old_key = "./init" if "./init" in old else "init"
    if init_key not in new or old_key not in old:
        raise ValueError("base or rebuilt ramdisk has no /init")
    mode, init_data, _ = new[init_key]
    if (mode & 0o170000) != 0o100000 or not (mode & 0o111):
        raise ValueError("/init is not executable regular file")
    check_elf(init_data)
    if init_data != init:
        raise ValueError("rebuilt ramdisk /init does not match build/violet/vqeaf-init")

    old_rest = {k: v for k, v in old.items() if k != old_key}
    new_rest = {k: v for k, v in new.items() if k != init_key}
    if old_rest != new_rest:
        raise ValueError("non-/init ramdisk entries changed during patch")

    dev_src = DEVNODES.read_text(encoding="utf-8")
    main_src = MAIN.read_text(encoding="utf-8")
    required_dev = ('make_node("/dev/console", 5, 1, S_IFCHR)',
                    'make_node("/dev/kmsg", 1, 11, S_IFCHR)',
                    "reopen_console_stdio()")
    required_main = ('pid1: /init entry reached',
                     'set_stage("pid1-entry")',
                     'set_stage("usb-gadget")',
                     'set_stage("drm-wait")',
                     'display: DRM/KMS init failed')
    for marker in required_dev:
        if marker not in dev_src:
            raise ValueError(f"missing console/kmsg init support: {marker}")
    for marker in required_main:
        if marker not in main_src:
            raise ValueError(f"missing PID1/USB/display diagnostic marker: {marker}")

    # This diagnostic archive intentionally has no static /dev nodes; PID 1
    # creates them after mounting tmpfs and then reopens stdio on /dev/console.
    static_console_nodes = [k for k in new if k.lstrip("./").startswith("dev/")]
    report = {
        "target_device": "violet",
        "role": "diagnostic initramfs only",
        "production_ready": False,
        "physical_boot_tested": False,
        "base_boot_image_sha256": sha(image),
        "base_kernel_sha256": sha(kernel_payload),
        "init": {"size": len(init), "sha256": sha(init), "elf": "AArch64 static"},
        "ramdisk": {"size": RAMDISK.stat().st_size,
                    "sha256": sha(RAMDISK.read_bytes()),
                    "entries": len(new),
                    "non_init_entries_preserved": True},
        "pid1_runtime": "not proven; requires device boot log",
        "console": "PID1 creates /dev/console and /dev/kmsg then reattaches stdio",
        "usb_rescue": "source path logs UDC discovery/bind result; runtime not proven",
        "display": "source path distinguishes DRM node wait from KMS init failure; runtime not proven",
        "static_dev_entries": static_console_nodes,
        "reason": "DTB/kernel compatibility and physical PID1/USB/display boot are still unverified."
    }
    MANIFEST.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"INIT_SHA256={report['init']['sha256']}")
    print(f"RAMDISK_SHA256={report['ramdisk']['sha256']}")
    print(f"RAMDISK_ENTRIES={len(new)}")
    print("NON_INIT_ENTRIES_PRESERVED=PASS")
    print("PID1_ELF_AARCH64_STATIC=PASS")
    print("CONSOLE_REOPEN_SOURCE=PASS")
    print("USB_DISPLAY_DIAGNOSTIC_MARKERS=PASS")
    print("PID1_STATIC_GATE=PASS")
    print("PRODUCTION_READY=false (physical boot and matching DTB/kernel required)")


if __name__ == "__main__":
    main()
