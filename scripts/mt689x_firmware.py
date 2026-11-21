#!/usr/bin/env python3
"""Extract MediaTek MT6893 firmware from a stock vendor dump.

QEAF never ships or invents vendor firmware. This tool copies the blobs the
MT6893 connectivity stack asks for out of a stock chopin/ares vendor tree
(a mounted vendor partition, an extracted super image, or a factory dump) and
writes a manifest that records exactly what was found.

    python scripts/mt689x_firmware.py --vendor D:\\dump\\vendor \\
        --out build/mt689x/firmware

Nothing is modified in the source tree. Missing families are reported, not
fabricated; the kernel will report Wi-Fi/Bluetooth as BLOCKED_FIRMWARE until
the files exist.
"""
import argparse
import hashlib
import os
import shutil
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, "build", "mt689x", "firmware")

# Families the downstream driver requests at runtime. Names are prefixes, not
# exact file names: the vendor image carries chip revisions in the middle.
FAMILIES = {
    "wifi_ram_code": ("wifi_ram_code", "wifi_ram_code2"),
    "wifi_patch": ("wifi_mt", "wifi_mt6893"),
    "bt_ram_code": ("bt_ram_code",),
    "bt_config": ("bt_addr", "bt_fw.cfg", "eeprom_mt"),
    "connsys": ("conninfra", "consys", "rom_patch"),
}

# Where the MTK stack looks. The extractor keeps the mediatek/mt6893 prefix so
# /lib/firmware/mediatek/mt6893/... resolves the driver's request unchanged.
PREFERRED_SUBDIR = os.path.join("mediatek", "mt6893")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def classify(name):
    low = name.lower()
    for family, prefixes in FAMILIES.items():
        if any(p in low for p in prefixes):
            return family
    return "other"


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--vendor", required=True,
                    help="stock vendor tree or firmware dir to read")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args(argv)

    source = os.path.abspath(args.vendor)
    if not os.path.isdir(source):
        print("mt689x_firmware: %s is not a directory" % source, file=sys.stderr)
        return 2

    out = os.path.abspath(args.out)
    target = os.path.join(out, PREFERRED_SUBDIR)

    found = []
    for dirpath, _dirs, files in os.walk(source):
        low = dirpath.replace("\\", "/").lower()
        in_mtk = "/mediatek/mt6893" in low or low.endswith("/mediatek/mt6893")
        for name in files:
            n = name.lower()
            if not in_mtk and classify(name) == "other":
                continue
            if not (n.endswith(".bin") or n.endswith(".dat") or
                    n.endswith(".cfg") or n in ("bt_addr", "wifi", "wifi_custom")):
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), source)
            found.append((rel, os.path.join(dirpath, name), classify(name)))

    if not found:
        print("mt689x_firmware: no MT6893 firmware files under %s" % source,
              file=sys.stderr)
        return 1

    families = {}
    for rel, path, family in found:
        families.setdefault(family, []).append(rel)

    print("mt689x_firmware: %d file(s) from %s" % (len(found), source))
    for family in sorted(families):
        print("  %-14s %d" % (family, len(families[family])))
    for family in sorted(FAMILIES):
        if family not in families:
            print("  %-14s MISSING (kernel will report BLOCKED_FIRMWARE)"
                  % family)

    if args.dry_run:
        for rel, _path, family in sorted(found):
            print("    %-12s %s" % (family, rel))
        return 0

    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(target)
    lines = ["# QEAF MT6893 firmware manifest",
             "# source: %s" % source,
             "# %d file(s)" % len(found), ""]
    for rel, path, family in sorted(found):
        # Keep only the basename: the driver asks for a flat name under
        # mediatek/mt6893, not for the dump's directory layout.
        name = os.path.basename(path)
        dest = os.path.join(target, name)
        if os.path.exists(dest):
            base, ext = os.path.splitext(name)
            dest = os.path.join(target, "%s.%s%s" % (
                base, family, ext))
        shutil.copy2(path, dest)
        lines.append("%s  %s  %s" % (sha256(dest), family, os.path.basename(dest)))
        print("  -> %s" % os.path.relpath(dest, ROOT))

    with open(os.path.join(out, "MANIFEST.txt"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print("mt689x_firmware: wrote %s (MANIFEST.txt included)" % target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
