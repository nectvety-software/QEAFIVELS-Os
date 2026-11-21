#!/usr/bin/env python3
"""Package the MT6891/MT6893 build into a QEAF boot bundle.

The bundle is deliberately honest about its state: it contains the real
userland ELF, the real kernel Image/dtb and the real modules and firmware that
were available, with hashes, and it says "build-only, not hardware-verified".

    python scripts/build_mt689x.py
    bash scripts/mt689x_kernel_build.sh chopin      (inside WSL/Linux)
    python scripts/mt689x_firmware.py --vendor <dump>/vendor
    python scripts/package_mt689x.py --kernel-dir <out/chopin> \\
        --bootimg --reference stock-boot.img

Without --bootimg only the component bundle is written (dist/mt689x/), which
is what a mainline bring-up workflow consumes. --bootimg additionally writes a
standard boot image v2; the addresses and page size are copied from
--reference (a stock chopin/ares boot.img) when given, because guessing them
would produce an image that silently cannot boot.
"""
import argparse
import gzip
import hashlib
import io
import json
import os
import shutil
import struct
import sys

# Boot-image format magic. Kept as raw bytes: the bootloader needs this exact
# 8-byte token; it is a format constant, not a QEAF identity string.
BOOT_MAGIC = bytes((0x41, 0x4E, 0x44, 0x52, 0x4F, 0x49, 0x44, 0x21))

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build", "mt689x")
INIT = os.path.join(BUILD, "vqeaf-init")
STAGE = os.path.join(BUILD, "stage")
RAMDISK = os.path.join(BUILD, "qeaf-mt689x.cpio.gz")
DEFAULT_KERNEL = (r"\\wsl.localhost\Ubuntu-24.04\root\qeaf-mt689x\out\chopin")
DEFAULT_OUT = os.path.join(ROOT, "dist", "mt689x")

# Modules QEAF loads at boot plus their dependencies. Copied flat into
# /lib/modules/mt689x, which is the directory mt689x_bringup.c reads.
MODULE_PATTERNS = (
    "conninfra", "wlan_drv", "wlan", "btmtk", "btif", "mt66xx",
    "mt8192", "mt6893", "mt6359", "cs35l41", "sensorhub", "mtk-",
    "mtk_", "consys", "panfrost", "snd-soc-core",
)

EM_AARCH64 = 0xB7


def fail(msg):
    sys.stderr.write("package_mt689x: %s\n" % msg)
    raise SystemExit(1)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_init():
    if not os.path.isfile(INIT):
        fail("%s missing; run `python scripts/build_mt689x.py`" % INIT)
    with open(INIT, "rb") as fh:
        head = fh.read(20)
    if head[:4] != b"\x7fELF" or head[4] != 2:
        fail("%s is not a 64-bit ELF" % INIT)
    if struct.unpack_from("<H", head, 18)[0] != EM_AARCH64:
        fail("%s is not aarch64" % INIT)


# ---- cpio newc -------------------------------------------------------------

class Cpio:
    def __init__(self):
        self.buf = io.BytesIO()
        self.ino = 1

    def _name(self, name):
        return name.encode("utf-8") + b"\0"

    def entry(self, name, mode, data=b"", ino=None, nlink=1):
        ino = ino or self.ino
        if not ino:
            ino = self.ino
        self.ino += 1
        namesize = len(self._name(name))
        header = struct.pack(
            "<6s8s8s8s8s8s8s8s8s8s8s8s8s8s",
            b"070701", b"%08X" % ino, b"%08X" % mode, b"%08X" % 0,
            b"%08X" % 0, b"%08X" % nlink, b"%08X" % 0,
            b"%08X" % len(data), b"%08X" % 0, b"%08X" % 0,
            b"%08X" % 0, b"%08X" % 0, b"%08X" % namesize,
            b"%08X" % 0)
        self.buf.write(header)
        self.buf.write(self._name(name))
        pad = (-len(header) - namesize) % 4
        self.buf.write(b"\0" * pad)
        self.buf.write(data)
        self.buf.write(b"\0" * ((-len(data)) % 4))

    def finish(self):
        self.entry("TRAILER!!!", 0, b"", ino=0, nlink=1)
        data = self.buf.getvalue()
        return data + b"\0" * ((-len(data)) % 512)


def add_tree(cpio, src, prefix, seen=None):
    if seen is None:
        seen = set()
    real = os.path.realpath(src)
    if real in seen:
        return
    seen.add(real)
    cpio.entry(prefix, 0o040755)
    for name in sorted(os.listdir(src)):
        path = os.path.join(src, name)
        target = "%s/%s" % (prefix.rstrip("/"), name)
        if os.path.isdir(path):
            add_tree(cpio, path, target, seen)
        elif os.path.islink(path):
            cpio.entry(target, 0o120777, os.readlink(path).encode())
        else:
            with open(path, "rb") as fh:
                data = fh.read()
            cpio.entry(target, 0o100644, data)


def stage(modules_dir, firmware_dir):
    shutil.rmtree(STAGE, ignore_errors=True)
    os.makedirs(STAGE)
    shutil.copy2(INIT, os.path.join(STAGE, "init"))
    os.chmod(os.path.join(STAGE, "init"), 0o755)

    for path in ("etc/qeaf", "lib/modules/mt689x", "lib/firmware/mediatek/mt6893",
                 "run", "tmp", "dev", "proc", "sys"):
        os.makedirs(os.path.join(STAGE, path), exist_ok=True)
    os.chmod(os.path.join(STAGE, "tmp"), 0o1777)
    with open(os.path.join(STAGE, "etc", "qeaf", "session"), "w",
              encoding="utf-8") as fh:
        fh.write("qeaf\n")

    copied = []
    if modules_dir and os.path.isdir(modules_dir):
        for dirpath, _dirs, files in os.walk(modules_dir):
            for name in files:
                if not name.endswith(".ko"):
                    continue
                low = name.lower()
                if not any(p in low for p in MODULE_PATTERNS):
                    continue
                dest = os.path.join(STAGE, "lib", "modules", "mt689x", name)
                shutil.copy2(os.path.join(dirpath, name), dest)
                copied.append(name)
    else:
        sys.stderr.write("package_mt689x: no modules dir (%s); the ramdisk "
                         "will not load connectivity/audio modules\n" %
                         (modules_dir or "(none)"))

    fw = 0
    fw_root = os.path.join(firmware_dir, "mediatek", "mt6893") \
        if firmware_dir else None
    if fw_root and os.path.isdir(fw_root):
        for name in sorted(os.listdir(fw_root)):
            src = os.path.join(fw_root, name)
            if not os.path.isfile(src):
                continue
            shutil.copy2(src, os.path.join(STAGE, "lib", "firmware",
                                           "mediatek", "mt6893", name))
            fw += 1
    else:
        sys.stderr.write("package_mt689x: no firmware at %s; Wi-Fi/BT will "
                         "report BLOCKED_FIRMWARE\n" % (fw_root or "(none)"))

    cpio = Cpio()
    add_tree(cpio, STAGE, ".")
    with open(RAMDISK, "wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", compresslevel=9,
                           mtime=0) as out:
            out.write(cpio.finish())
    print("package_mt689x: ramdisk %s (%d bytes), %d module(s), %d firmware "
          "file(s)" % (RAMDISK, os.path.getsize(RAMDISK), len(copied), fw))
    return copied, fw


# ---- boot image v2 -------------------------------------------------

def read_boot_header(path):
    with open(path, "rb") as fh:
        data = fh.read(4096)
    if data[:8] != BOOT_MAGIC:
        fail("%s is not a supported boot image" % path)
    fields = struct.unpack_from("<10I", data, 8)
    (kernel_size, kernel_addr, ramdisk_size, ramdisk_addr, second_size,
     second_addr, tags_addr, page_size, header_version, os_version) = fields
    name = data[48:64].split(b"\0")[0].decode("latin-1", "replace")
    cmdline = data[64:576].split(b"\0")[0].decode("latin-1", "replace")
    dtb_size = dtb_addr = 0
    if header_version >= 2:
        dtb_size, dtb_addr = struct.unpack_from("<2Q", data, 1632)
    return {
        "kernel_addr": kernel_addr, "ramdisk_addr": ramdisk_addr,
        "second_addr": second_addr, "tags_addr": tags_addr,
        "page_size": page_size or 2048, "header_version": header_version,
        "os_version": os_version, "name": name, "cmdline": cmdline,
        "dtb_addr": dtb_addr,
    }


def make_bootimg(kernel, ramdisk, dtb, out, page_size, addrs, cmdline,
                 name="qeaf-mt689x"):
    kernel_data = open(kernel, "rb").read()
    ramdisk_data = open(ramdisk, "rb").read()
    dtb_data = open(dtb, "rb").read() if dtb else b""

    def pad(data):
        return data + b"\0" * ((-len(data)) % page_size)

    header = struct.pack(
        "<8s10I16s512s32s1024s", BOOT_MAGIC,
        len(kernel_data), addrs["kernel_addr"],
        len(ramdisk_data), addrs["ramdisk_addr"],
        0, addrs["second_addr"], addrs["tags_addr"], page_size, 2, 0,
        name.encode()[:16], cmdline.encode()[:511] + b"\0", b"\0" * 32,
        b"\0" * 1024)
    header += struct.pack("<4I", 0, 0, 0, 0)          # v1 fields (unused)
    header += struct.pack("<IQ", len(dtb_data), addrs["dtb_addr"])
    header += b"\0" * (1660 - len(header))
    header += b"\0" * ((-len(header)) % page_size)

    with open(out, "wb") as fh:
        fh.write(header)
        fh.write(pad(kernel_data))
        fh.write(pad(ramdisk_data))
        if dtb_data:
            fh.write(pad(dtb_data))
    return os.path.getsize(out)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel-dir", default=os.environ.get("MTK_KERNEL_OUT",
                                                           DEFAULT_KERNEL))
    ap.add_argument("--modules", default=None)
    ap.add_argument("--firmware", default=os.path.join(BUILD, "firmware"))
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--dtb", default=None,
                    help="default: <kernel-dir>/arch/arm64/boot/dts/mediatek/"
                         "mt6893-xiaomi-chopin.dtb")
    ap.add_argument("--bootimg", action="store_true")
    ap.add_argument("--reference", default=None,
                    help="stock boot.img whose addresses/page size are reused")
    ap.add_argument("--cmdline", default=None)
    args = ap.parse_args(argv)

    check_init()
    modules = args.modules or os.path.join(args.kernel_dir, "modules")
    copied, fw = stage(modules, args.firmware)

    os.makedirs(args.out, exist_ok=True)
    shutil.copy2(INIT, os.path.join(args.out, "vqeaf-init"))
    shutil.copy2(RAMDISK, os.path.join(args.out, "qeaf-mt689x.cpio.gz"))

    artifacts = {
        "vqeaf-init": os.path.join(args.out, "vqeaf-init"),
        "qeaf-mt689x.cpio.gz": os.path.join(args.out, "qeaf-mt689x.cpio.gz"),
    }
    kernel = os.path.join(args.kernel_dir, "arch", "arm64", "boot", "Image")
    dtb = args.dtb or os.path.join(args.kernel_dir, "arch", "arm64", "boot",
                                   "dts", "mediatek",
                                   "mt6893-xiaomi-chopin.dtb")
    if os.path.isfile(kernel):
        shutil.copy2(kernel, os.path.join(args.out, "Image"))
        artifacts["Image"] = os.path.join(args.out, "Image")
    else:
        sys.stderr.write("package_mt689x: kernel Image not found at %s\n" % kernel)
    if os.path.isfile(dtb):
        shutil.copy2(dtb, os.path.join(args.out, "mt6893-xiaomi-chopin.dtb"))
        artifacts["mt6893-xiaomi-chopin.dtb"] = os.path.join(
            args.out, "mt6893-xiaomi-chopin.dtb")
    else:
        sys.stderr.write("package_mt689x: dtb not found at %s\n" % dtb)

    manifest = {
        "platform": "mt6891-mt6893",
        "board_dts": "mt6893-xiaomi-chopin.dtb",
        "kernel_branch": "7.3.3-mt6893-xiaomi-chopin",
        "verification": "build-only: no MT6891/MT6893 device was attached, "
                        "nothing here has been booted or flashed",
        "modules": copied,
        "firmware_files": fw,
        "artifacts": {name: {"size": os.path.getsize(path),
                             "sha256": sha256(path)}
                      for name, path in artifacts.items()},
        "unsupported": ["nfc", "camera", "baseband"],
    }

    if args.bootimg:
        if "Image" not in artifacts:
            fail("--bootimg needs the kernel Image")
        addrs = {"kernel_addr": 0x40080000, "ramdisk_addr": 0x4BF00000,
                 "second_addr": 0x40F00000, "tags_addr": 0x40000100,
                 "dtb_addr": 0x41F00000}
        page_size = 2048
        cmdline = args.cmdline or (
            "console=tty0 root=/dev/ram console=ttyS0,921600n1 "
            "vmalloc=400M swiotlb=noforce "
            "firmware_class.path=/lib/firmware")
        if args.reference:
            ref = read_boot_header(args.reference)
            for key in ("kernel_addr", "ramdisk_addr", "second_addr",
                        "tags_addr", "dtb_addr"):
                if ref.get(key):
                    addrs[key] = ref[key]
            page_size = ref["page_size"]
            if not args.cmdline and ref.get("cmdline"):
                cmdline = ref["cmdline"]
            print("package_mt689x: reused header from %s (page %d, "
                  "kernel 0x%x)" % (args.reference, page_size,
                                    addrs["kernel_addr"]))
        boot = os.path.join(args.out, "boot-qeaf-mt689x.img")
        size = make_bootimg(kernel, RAMDISK,
                            dtb if os.path.isfile(dtb) else None,
                            boot, page_size, addrs, cmdline)
        manifest["artifacts"]["boot-qeaf-mt689x.img"] = {
            "size": size, "sha256": sha256(boot)}
        print("package_mt689x: %s (%d bytes)" % (boot, size))

    with open(os.path.join(args.out, "manifest.json"), "w",
              encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2)
        fh.write("\n")

    with open(os.path.join(args.out, "README.txt"), "w", encoding="utf-8") as fh:
        fh.write(
            "QEAF MT6891/MT6893 bundle\n"
            "========================\n\n"
            "State: build-only. No chopin/ares device was attached while this\n"
            "was produced; nothing has been booted, and no partition was\n"
            "written. The kernel is MT6891-Mainline/linux branch\n"
            "7.3.3-mt6893-xiaomi-chopin plus platform/mt689x/kernel/\n"
            "qeaf_mt689x.fragment.\n\n"
            "Supported by design: UFS, USB (MTU3 dual-role + MT6360 Type-C),\n"
            "DRM/KMS display, touch, Wi-Fi, high refresh (30/48/50/60/120 Hz\n"
            "timings, 120 Hz preferred), Bluetooth, audio, auto-rotate, KVM.\n"
            "GPU is not usable on this tree yet: the DTS node is\n"
            "mediatek,mt6893-mali and Panfrost only matches arm,mali-bifrost /\n"
            "arm,mali-valhall-jm, so no render node binds and QEAF reports GPU\n"
            "UNSUPPORTED. Not supported: NFC, camera, baseband.\n\n"
            "Firmware and modules are only present when a stock vendor dump\n"
            "and a kernel module build were supplied; the manifest records\n"
            "exactly which files made it in.\n")
        fh.write("\nArtifacts:\n")
        for name, info in sorted(manifest["artifacts"].items()):
            fh.write("  %-30s %10d  %s\n" % (name, info["size"],
                                             info["sha256"]))
    print("package_mt689x: wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
