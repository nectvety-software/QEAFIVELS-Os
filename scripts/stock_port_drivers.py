#!/usr/bin/env python3
"""Port stock Android (Redmi Note 7 Pro / violet) drivers into QEAFIVELS OS.

Input: the official Xiaomi fastboot ROM (violet_images_V12.5.4.0.QFHCNXM.tgz).
Output under build/stock-port/extract/:
  boot/    - unpacked stock boot.img (kernel Image, ramdisk)
  dtbo/    - each dtbo.img overlay entry decoded as .dtb (fdtdump-compatible)
  vendor/  - selected driver payloads from vendor.img:
             firmware blobs (wlan/wcnss, touch, camera), .ko modules,
             modules.dep/.alias, and a driver inventory report
The kernel Image is the carrier of the stock built-in drivers; the DTB and
vendor firmware are the device-specific bindings QEAFIVELS reuses.
"""
import os
import struct
import sys
import tarfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STOCK = os.path.join(ROOT, "build", "stock-port")
EXTRACT = os.path.join(STOCK, "extract")

CHUNK = 4 * 1024 * 1024


def unpack_boot(path, out):
    """Parse Android boot image (v0-v3 headers), dump kernel + ramdisk."""
    os.makedirs(out, exist_ok=True)
    with open(path, "rb") as f:
        magic = f.read(8)
        if magic[:4] != b"ANDROID":
            print("  boot.img: no ANDROID magic (%r)" % magic[:6])
            return
        f.seek(0)
        hdr = f.read(1660)
        # header version in bytes 40-43 of v3, else first fields
        (header_version,) = struct.unpack_from("<I", hdr, 0)
        hdr = hdr[4:] if False else hdr  # keep layout math below
        f.seek(0)
        base = f.read(512)
        (magic, kernel_size, kernel_addr) = struct.unpack_from("<8sII", base, 0)
        (ramdisk_size, ramdisk_addr) = struct.unpack_from("<II", base, 12)
        (second_size, second_addr) = struct.unpack_from("<II", base, 20)
        (tags_addr, page_size, dt_size, unused, os_ver, sec) = struct.unpack_from(
            "<IIIIII", base, 28)
        # header_version lives at offset 40 in v1+
        (hv,) = struct.unpack_from("<I", base, 40)
        print("  boot.img: magic ok kernel=%u ramdisk=%u page=%u hdr_ver=%u"
              % (kernel_size, ramdisk_size, page_size, hv))
        off = page_size
        f.seek(off)
        kernel = f.read(kernel_size)
        with open(os.path.join(out, "kernel.img"), "wb") as k:
            k.write(kernel)
        off = page_size * ((kernel_size + page_size - 1) // page_size) + page_size
        f.seek(off)
        if ramdisk_size:
            rd = f.read(ramdisk_size)
            with open(os.path.join(out, "ramdisk.img"), "wb") as r:
                r.write(rd)
        if dt_size:
            f.seek(page_size * ((kernel_size + page_size - 1) // page_size) +
                   page_size * ((ramdisk_size + page_size - 1) // page_size) +
                   page_size)
            dt = f.read(dt_size)
            with open(os.path.join(out, "dt.img"), "wb") as d:
                d.write(dt)
        # kernel identity: version banner sits at the Image start
        if kernel[:4] in (b"\x1f\x8b\x08", b"\x28\xb5\x2f\xfd"):
            comp = {b"\x1f\x8b\x08": "gzip", b"\x28\xb5\x2f\xfd": "zstd"}[
                kernel[:3] if kernel[1] == 0x8b else kernel[:4]]
            print("  kernel: %s-compressed (%u bytes)" % (comp, kernel_size))
        else:
            arm64 = kernel[:4] == b"\x41\x4d\x50\x52" or kernel[0x38:0x3C] == \
                b"ARM\x64"
            print("  kernel: uncompressed (%u bytes) ARM64=%s"
                  % (kernel_size, kernel[:4] == b"ARMd" or arm64))


def unpack_dtbo(path, out):
    """Parse Android DTBO v0 header and dump every overlay entry."""
    os.makedirs(out, exist_ok=True)
    with open(path, "rb") as f:
        hdr = f.read(28)
        if hdr[:4] != b"\xd0\x0d\xfe\xed":
            print("  dtbo.img: not a DTBO header")
            return
        (magic, total, hdr_size, _, page) = struct.unpack_from(">IIIII", hdr, 0)
        (dt_entries,) = struct.unpack_from(">I", hdr, 16)
        print("  dtbo.img: entries=%u pagesize=%u" % (dt_entries, page))
        for i in range(dt_entries):
            f.seek(28 + i * 32)
            (size, off, _, _, id_v, rev, custom) = struct.unpack_from(
                ">IIIIIII", f.read(32), 0)
            f.seek(off)
            blob = f.read(size)
            name = "entry%02u_p%08x_r%08x.dtb" % (i, id_v, rev)
            with open(os.path.join(out, name), "wb") as d:
                d.write(blob)
            # count strings + a few names for the inventory
            strings = blob.count(b"\x00")
            print("    %s: %u bytes" % (name, size))


def ext4_read(image_path, out, prefixes):
    """Stream an ext4 image and copy files whose path starts with a prefix."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "persist_inspect", os.path.join(ROOT, "scripts", "persist_inspect.py"))
    pi = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pi)
    size = os.path.getsize(image_path)
    print("  %s: %u bytes" % (os.path.basename(image_path), size))
    # Ext4Image loads fully into RAM; fine for 1-2 GB images
    with open(image_path, "rb") as f:
        data = f.read()
    try:
        img = pi.Ext4Image(data)
    except ValueError as e:
        print("    ext4: %s" % e)
        return 0
    del data
    print("    ext4: volume='%s' blocks=%u" % (img.volume_name,
                                               img.blocks_count_lo))
    files = []

    def walk(num, path, depth=0):
        if depth > 12:
            return
        node = img.inode(num)
        if node is None or not node.get("dir"):
            return
        try:
            entries = img.read_dir(node)
        except (ValueError, IndexError):
            return
        for (name, num2, _) in entries:
            if name in (".", ".."):
                continue
            p = path + "/" + name
            child = img.inode(num2)
            if child is None:
                continue
            if child.get("dir"):
                walk(num2, p, depth + 1)
            else:
                if any(p.startswith(x) for x in prefixes):
                    files.append((p, num2))
    walk(2, "")
    os.makedirs(out, exist_ok=True)
    n = 0
    for (p, num) in files:
        node = img.inode(num)
        try:
            payload = img.read_file(node)
        except (ValueError, IndexError):
            continue
        rel = p.strip("/").replace("/", "__")
        with open(os.path.join(out, rel), "wb") as g:
            g.write(payload)
        n += 1
    print("    copied %u files" % n)
    return n


def main():
    tgz = os.path.join(STOCK, "violet_images_V12.5.4.0.QFHCNXM.tgz")
    if not os.path.exists(tgz):
        print("ROM tgz not found; download first")
        return 1
    stage = os.path.join(STOCK, "stage")
    os.makedirs(stage, exist_ok=True)
    wanted = ("boot.img", "dtbo.img", "vendor.img", "vbmeta.img")
    print("scanning %s" % tgz)
    members = []
    with tarfile.open(tgz, "r:gz") as t:
        for m in t:
            base = os.path.basename(m.name)
            if base in wanted and m.isfile():
                members.append(m)
                print("  member: %s (%u bytes)" % (m.name, m.size))
                t.extract(m, stage, set_attrs=False)
    boot = os.path.join(stage, "boot.img")
    if os.path.exists(boot):
        unpack_boot(boot, os.path.join(EXTRACT, "boot"))
    dtbo = os.path.join(stage, "dtbo.img")
    if os.path.exists(dtbo):
        unpack_dtbo(dtbo, os.path.join(EXTRACT, "dtbo"))
    vendor = os.path.join(stage, "vendor.img")
    if os.path.exists(vendor):
        prefixes = ("/firmware/", "/firmware_mnt/", "/lib/modules/",
                    "/etc/firmware/", "/lib/modules", "/odm/")
        ext4_read(vendor, os.path.join(EXTRACT, "vendor"), prefixes)
    print("done -> %s" % EXTRACT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
