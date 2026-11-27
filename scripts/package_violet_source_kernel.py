#!/usr/bin/env python3
"""Build a separate Violet RAM-boot image using the source-built 4.14.215 kernel.

This never flashes a device or writes a partition. The kernel is source-built;
the board DTB is required to match the exact stock Violet DTB byte-for-byte.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
QEAF = Path(os.environ.get("QEAFDROPOS", r"D:\QeafivelsOfficial\QeafdropOs"))
REFERENCE = QEAF / "out/reference/stock-violet/boot.img"
KERNEL = ROOT / "build/kernel-violet-4.14.215-stock-audit/source-Image.gz-dtb-initrdfix"
RAMDISK = ROOT / "build/violet/qeaf-initramfs.cpio.gz"
CONFIG = ROOT / "build/kernel-violet-4.14.215-stock-audit/perf-final.config"
OUTPUT = ROOT / "dist/boot-vqeaf-violet-source.img"
MANIFEST = ROOT / "dist/boot-vqeaf-violet-source.manifest.json"
SOURCE_COMMIT = "a8547758e03dc26a3a587a0661f17b25289e743e"
SOURCE_BRANCH = "thirteen"
KERNEL_RELEASE = b"4.14.215-azure-Pixel+"
PAGE = 4096
P77_SIZE = 67108864
CMDLINE_OFF = 0x40
CMDLINE_MAX = 512
KERNEL_ADDR = 0x00008000
RAMDISK_ADDR = 0x01000000


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def align(value: int, unit: int = PAGE) -> int:
    return (value + unit - 1) // unit * unit


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def read_android_header(data: bytes) -> dict:
    require(len(data) >= PAGE, "stock reference is shorter than one header page")
    require(data[:8] == b"ANDROID!", "stock reference does not contain ANDROID! boot magic")
    h = {
        "kernel_size": u32(data, 0x08),
        "kernel_addr": u32(data, 0x0C),
        "ramdisk_size": u32(data, 0x10),
        "ramdisk_addr": u32(data, 0x14),
        "second_size": u32(data, 0x18),
        "second_addr": u32(data, 0x1C),
        "tags_addr": u32(data, 0x20),
        "page_size": u32(data, 0x24),
        "header_version": u32(data, 0x28),
    }
    require(h["page_size"] == PAGE, f"stock boot page_size={h['page_size']}, expected 4096")
    require(h["header_version"] == 1, f"stock boot header_version={h['header_version']}, expected 1")
    require(h["kernel_addr"] == KERNEL_ADDR, f"unexpected Violet kernel_addr=0x{h['kernel_addr']:08x}")
    return h


def gzip_image_and_tail(payload: bytes, label: str) -> tuple[bytes, bytes]:
    dec = zlib.decompressobj(31)
    try:
        image = dec.decompress(payload)
        image += dec.flush()
    except zlib.error as exc:
        raise RuntimeError(f"{label} is not a valid gzip Image: {exc}") from exc
    return image, dec.unused_data


def arm64_header(image: bytes, label: str) -> dict:
    require(len(image) >= 0x40, f"{label}: uncompressed image is too small")
    require(image[0x38:0x3C] == b"ARMd", f"{label}: ARM64 Image magic not found at 0x38")
    text_offset, image_size, flags = struct.unpack_from("<QQQ", image, 0x08)
    return {"text_offset": text_offset, "image_size": image_size, "flags": flags}


def cpio_entries(data: bytes):
    offset = 0
    while offset + 110 <= len(data) and data[offset:offset + 6] in (b"070701", b"070702"):
        try:
            fields = [int(data[offset + 6 + i * 8:offset + 14 + i * 8], 16) for i in range(13)]
        except ValueError:
            return
        namesize, filesize = fields[11], fields[6]
        if namesize < 2 or namesize > 4096:
            return
        name = data[offset + 110:offset + 110 + namesize - 1].decode("utf-8", "replace")
        start = offset + ((110 + namesize + 3) & ~3)
        end = start + filesize
        if end > len(data):
            return
        if name == "TRAILER!!!":
            return
        yield name.removeprefix("./"), data[start:end], fields[1]
        offset = start + ((filesize + 3) & ~3)


def validate_aarch64_init(init: bytes, mode: int) -> dict:
    require(len(init) >= 64, "ramdisk /init ELF header is truncated")
    require(init[:4] == b"\x7fELF", "ramdisk /init is not ELF")
    require(init[4] == 2 and init[5] == 1, "ramdisk /init must be ELF64 little-endian")
    etype, machine = struct.unpack_from("<HH", init, 16)
    entry, phoff = struct.unpack_from("<QQ", init, 24)
    ehsize, phentsize, phnum = struct.unpack_from("<HHH", init, 52)
    require(etype == 2, f"/init ELF type must be ET_EXEC (2), got {etype}")
    require(machine == 183, f"/init ELF machine must be AArch64 (183), got {machine}")
    require(ehsize >= 64 and phentsize >= 56 and phnum > 0,
            "ramdisk /init ELF program-header table is invalid")
    require(phoff + phentsize * phnum <= len(init),
            "ramdisk /init ELF program-header table is out of bounds")
    executable_entry = False
    interp = False
    loads = 0
    for index in range(phnum):
        p_type, p_flags, p_offset, p_vaddr, _paddr, p_filesz, p_memsz, _align = \
            struct.unpack_from("<IIQQQQQQ", init, phoff + index * phentsize)
        if p_type == 1:  # PT_LOAD
            loads += 1
            require(p_filesz <= p_memsz, f"/init PT_LOAD[{index}] filesz exceeds memsz")
            require(p_offset + p_filesz <= len(init),
                    f"/init PT_LOAD[{index}] file range is out of bounds")
            if p_flags & 1 and p_vaddr <= entry < p_vaddr + p_memsz:
                executable_entry = True
        elif p_type == 3:  # PT_INTERP
            interp = True
    require(loads > 0, "/init has no PT_LOAD segment")
    require(executable_entry, "/init entry point is not inside an executable PT_LOAD segment")
    require(not interp, "/init must be static and must not contain PT_INTERP")
    require(mode & 0o111, "/init CPIO entry does not have an executable mode")
    return {"elf_type": etype, "machine": machine, "entry": entry,
            "program_headers": phnum, "load_segments": loads,
            "static_no_interpreter": True, "cpio_mode": oct(mode)}


def build(args) -> int:
    reference = Path(args.reference).read_bytes()
    header = read_android_header(reference)
    ref_kernel = reference[PAGE:PAGE + header["kernel_size"]]
    require(len(ref_kernel) == header["kernel_size"], "stock kernel payload is truncated")
    stock_image, stock_dtb = gzip_image_and_tail(ref_kernel, "stock kernel")
    stock_arm = arm64_header(stock_image, "stock kernel")

    kernel = Path(args.kernel).read_bytes()
    image, dtb = gzip_image_and_tail(kernel, "source kernel")
    arm = arm64_header(image, "source kernel")
    require(KERNEL_RELEASE in image, "source kernel release marker is missing")
    override_log = (b"QEAF: tagged Violet RAM boot overrides skip_initramfs; "
                    b"unpacking supplied initrd")
    require(b"qeaf_ramboot_probe" in image and override_log in image,
            "source kernel is missing the tagged initramfs override")
    require(arm["text_offset"] == stock_arm["text_offset"],
            f"text_offset differs from stock: 0x{arm['text_offset']:x} vs 0x{stock_arm['text_offset']:x}")
    require(arm["flags"] == stock_arm["flags"],
            f"ARM64 Image flags differ from stock: 0x{arm['flags']:x} vs 0x{stock_arm['flags']:x}")
    require(dtb[:4] == b"\xd0\x0d\xfe\xed", "source kernel has no appended FDT")
    require(len(dtb) >= 40 and struct.unpack_from(">I", dtb, 4)[0] == len(dtb),
            "source appended FDT totalsize does not match its byte length")
    require(dtb == stock_dtb,
            f"source DTB differs from stock: source={sha256(dtb)} stock={sha256(stock_dtb)}")

    config = Path(args.config).read_text(encoding="utf-8", errors="replace")
    critical = (
        "CONFIG_ICNSS=y",
        "CONFIG_ICNSS_QMI=y",
        "CONFIG_REGULATOR_RPMH=y",
        "CONFIG_QTI_RPMH_API=y",
        "CONFIG_MSM_CLK_RPMH=y",
        "CONFIG_MSM_GCC_SM6150=y",
        "CONFIG_ARM_SMMU=y",
        "CONFIG_IOMMU_DMA=y",
        "CONFIG_ARM64_DMA_USE_IOMMU=y",
        "CONFIG_DMA_CMA=y",
        "CONFIG_SPMI_MSM_PMIC_ARB=y",
        "CONFIG_QRTR_SMD=y",
        "CONFIG_BLK_DEV_INITRD=y",
        "CONFIG_BLK_DEV_RAM=y",
        "CONFIG_RD_GZIP=y",
    )
    missing = [symbol for symbol in critical if symbol not in config.splitlines()]
    require(not missing, "kernel .config missing required providers: " + ", ".join(missing))
    require("# CONFIG_CNSS2 is not set" in config,
            "CNSS2 must remain disabled on integrated Violet ICNSS/SNOC")

    ramdisk = Path(args.ramdisk).read_bytes()
    require(ramdisk.startswith(b"\x1f\x8b\x08"), "ramdisk is not gzip")
    try:
        ramdisk_plain = gzip.decompress(ramdisk)
    except OSError as exc:
        raise RuntimeError(f"ramdisk decompression failed: {exc}") from exc

    entries = list(cpio_entries(ramdisk_plain))
    init_entries = [(body, mode) for name, body, mode in entries
                    if name in ("init", "./init")]
    require(len(init_entries) == 1,
            "ramdisk must contain exactly one /init entry")
    init, init_mode = init_entries[0]
    init_elf = validate_aarch64_init(init, init_mode)
    require(b"/etc/qeaf/wlan-pd-provider-verified" in init,
            "PID 1 is missing the fail-closed WLAN-PD gate")
    require(not any(name.lstrip("/") == "etc/qeaf/wlan-pd-provider-verified"
                    for name, _body, _mode in entries),
            "provider verification marker must NOT be staged in this diagnostic image")

    base_cmdline = reference[CMDLINE_OFF:CMDLINE_OFF + CMDLINE_MAX].split(b"\0", 1)[0].decode("ascii", "strict")
    # These flags are not kernel features in this tree; use the explicit,
    # tag-gated initramfs override instead of advertising no-op flags.
    ignored_flags = {"force_initramfs", "prefer_initramfs", "skip_initramfs"}
    cmdline_tokens = [token for token in base_cmdline.split()
                      if token not in ignored_flags and not token.startswith("qeaf_ramboot_probe=")]
    additions = [
        "init=/init",
        "rdinit=/init",
        "androidboot.qeaf_ramboot_probe=violet-source-4.14.215",
        "qeaf_ramboot_probe=violet-source-4.14.215",
    ]
    cmdline = " ".join(cmdline_tokens + additions)
    require(len(cmdline.encode("ascii")) < CMDLINE_MAX, "boot command line exceeds 511 bytes")
    require("androidboot.hardware=qcom" in cmdline, "Violet hardware cmdline token is absent")
    require("qeaf_ramboot_probe=violet-source-4.14.215" in cmdline,
            "tag-gated initramfs override token is absent")
    require(not any(token in ignored_flags for token in cmdline.split()),
            "obsolete initramfs flags must not be staged in the candidate header")

    out_header = bytearray(reference[:PAGE])
    struct.pack_into("<I", out_header, 0x08, len(kernel))
    struct.pack_into("<I", out_header, 0x0C, header["kernel_addr"])
    struct.pack_into("<I", out_header, 0x10, len(ramdisk))
    struct.pack_into("<I", out_header, 0x14, RAMDISK_ADDR)
    out_header[CMDLINE_OFF:CMDLINE_OFF + CMDLINE_MAX] = (
        cmdline.encode("ascii") + b"\0" * (CMDLINE_MAX - len(cmdline.encode("ascii")))
    )

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    tmp = output.with_suffix(output.suffix + ".part")
    with tmp.open("wb") as handle:
        handle.write(out_header)
        handle.write(kernel)
        handle.write(b"\0" * (align(len(kernel)) - len(kernel)))
        handle.write(ramdisk)
        handle.write(b"\0" * (align(len(ramdisk)) - len(ramdisk)))
        handle.write(b"\0" * (align(handle.tell()) - handle.tell()))
    tmp.replace(output)

    output_size = output.stat().st_size
    if output_size > P77_SIZE:
        output.unlink(missing_ok=True)
        raise RuntimeError(f"candidate image exceeds 64 MiB p77 budget ({output_size} bytes)")

    validate = Path(args.validate)
    audit = Path(args.audit)
    for tool, label in ((validate, "validate_bootimg"), (audit, "audit_rescue_channel")):
        proc = subprocess.run([sys.executable, str(tool), str(output)],
                              capture_output=True, text=True, encoding="utf-8", errors="replace")
        print(proc.stdout.rstrip())
        if proc.returncode:
            print(proc.stderr.rstrip(), file=sys.stderr)
            output.unlink(missing_ok=True)
            raise RuntimeError(f"{label} failed; candidate image removed")

    image_hash = sha256(output.read_bytes())
    manifest = {
        "format": "qeaf-violet-source-kernel-ramboot/v1",
        "target": "Xiaomi Redmi Note 7 Pro / violet / SM6150",
        "source": {
            "repository": "PixelExperience-Devices/kernel_xiaomi_violet",
            "branch": SOURCE_BRANCH,
            "commit": SOURCE_COMMIT,
            "kernel_release": KERNEL_RELEASE.decode("ascii"),
            "defconfig": "vendor/violet-perf_defconfig",
            "config_sha256": sha256(Path(args.config).read_bytes()),
        },
        "providers": {
            "pm6150_spmi_and_rpmh": True,
            "rpmh_clock_and_sm6150_gcc": True,
            "icnss_vreg_clock_sequence": True,
            "arm_smmu_iommu_dma": True,
            "dma_cma_coherent_api": True,
            "icnss_qmi": True,
            "cnss2": False,
            "verification_scope": "source config + build objects + stock DTB equality; runtime not yet tested",
        },
        "kernel": {
            "origin": "source-built",
            "size": len(kernel),
            "sha256": sha256(kernel),
            "uncompressed_image_size": len(image),
            "text_offset": arm["text_offset"],
            "image_size": arm["image_size"],
            "flags": arm["flags"],
            "release": KERNEL_RELEASE.decode("ascii"),
            "stock_kernel_sha256": sha256(ref_kernel),
            "matches_stock_kernel": kernel == ref_kernel,
        },
        "dtb": {
            "size": len(dtb),
            "sha256": sha256(dtb),
            "matches_stock_violet_dtb": True,
            "icnss_msa_region": "0x93300000 + 0x200000",
            "icnss_iommu_stream_id": "0x1640",
            "icnss_iova_window": "0xa0000000 + 0x10000000",
        },
        "ramdisk": {
            "size": len(ramdisk),
            "sha256": sha256(ramdisk),
            "init_size": len(init),
            "init_elf": init_elf,
            "wlan_pd_gate_present": True,
            "provider_marker_staged": False,
        },
        "initramfs_override": {
            "source_patch": str(ROOT / "patches/violet/0001-qeaf-ramboot-prioritize-initramfs.patch"),
            "source_patch_sha256": sha256((ROOT / "patches/violet/0001-qeaf-ramboot-prioritize-initramfs.patch").read_bytes()),
            "enabled_only_for_tag": "qeaf_ramboot_probe=violet-source-4.14.215",
            "normal_boot_skip_initramfs_behavior_preserved": True,
        },
        "boot_header": {
            "page_size": PAGE,
            "header_version": 1,
            "kernel_addr": header["kernel_addr"],
            "ramdisk_addr": RAMDISK_ADDR,
            "tags_addr": header["tags_addr"],
            "cmdline": cmdline,
        },
        "image": {"path": str(output), "size": output.stat().st_size, "sha256": image_hash},
        "verification": {
            "structural_gate": "PASS",
            "rescue_channel_gate": "PASS",
            "init_elf_abi_gate": "PASS",
            "tag_gated_initramfs_override": "BUILT_NOT_RUNTIME_TESTED",
            "dtb_matches_stock": True,
            "arm64_image_magic": "PASS",
            "kernelrelease": "PASS",
            "ram_boot_attempted": False,
            "physical_boot_tested": False,
            "partition_writes": 0,
            "wlan_pd_enabled": False,
            "wlfw_tested": False,
        },
    }
    Path(args.manifest).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"SOURCE RAM-BOOT IMAGE: {output}")
    print(f"SIZE: {output.stat().st_size} / {P77_SIZE} bytes")
    print(f"SHA256: {image_hash}")
    print(f"MANIFEST: {args.manifest}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", default=str(REFERENCE))
    parser.add_argument("--kernel", default=str(KERNEL))
    parser.add_argument("--ramdisk", default=str(RAMDISK))
    parser.add_argument("--config", default=str(CONFIG))
    parser.add_argument("--output", default=str(OUTPUT))
    parser.add_argument("--manifest", default=str(MANIFEST))
    parser.add_argument("--validate", default=str(QEAF / "scripts/validate_bootimg.py"))
    parser.add_argument("--audit", default=str(QEAF / "scripts/audit_rescue_channel.py"))
    args = parser.parse_args()
    try:
        return build(args)
    except Exception as exc:
        print(f"package_violet_source_kernel: REFUSED: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
