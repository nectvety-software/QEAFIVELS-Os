#!/usr/bin/env python3
"""Safely install the verified Violet QEAF image to p77/recovery.

Violet's stock boot path appends skip_initramfs, so fastboot boot returns to
Android instead of entering QEAF initramfs. This installer uses the live
QEAFRESCUE flash verb, which writes only p77 and verifies the full image
byte-for-byte. It refuses to run without the verified pre-update p77 backup.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_IMAGE = os.path.join(ROOT, "dist", "boot-vqeaf-violet.img")
BACKUP = os.path.join(ROOT, "build", "device-tests", "backup",
                      "p77-recovery-before-terminal.img")
BACKUP_SHA256 = "A45944CA33A34E2DD8C4A11CB8837B946AD7A0D43098B5B7641B2BA9CEC955A0"
P77_SIZE = 64 * 1024 * 1024


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", default=DEFAULT_IMAGE)
    args = ap.parse_args()
    image = os.path.abspath(args.image)
    manifest_path = os.path.splitext(image)[0] + ".manifest.json"

    if not os.path.isfile(image) or not os.path.isfile(manifest_path):
        raise SystemExit("install-violet: image or manifest missing")
    size = os.path.getsize(image)
    if size <= 0 or size > P77_SIZE:
        raise SystemExit("install-violet: image exceeds p77 capacity")
    with open(manifest_path, "r", encoding="utf-8") as f:
        manifest = json.load(f)
    actual = sha256(image)
    if actual != manifest.get("image", {}).get("sha256", "").upper():
        raise SystemExit("install-violet: image SHA-256 does not match manifest")

    if not os.path.isfile(BACKUP) or os.path.getsize(BACKUP) != P77_SIZE:
        raise SystemExit("install-violet: verified 64 MiB p77 backup is missing")
    backup_hash = sha256(BACKUP)
    if backup_hash != BACKUP_SHA256:
        raise SystemExit("install-violet: p77 backup SHA-256 mismatch")

    print("target: p77/recovery only")
    print("image: %s (%d bytes)" % (image, size))
    print("image sha256: %s" % actual)
    print("backup sha256: %s" % backup_hash)
    print("p76/boot, userdata and misc are not targets")
    cmd = [sys.executable, os.path.join(ROOT, "scripts", "device_check.py"),
           "--flash", "--image", image]
    return subprocess.run(cmd, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
