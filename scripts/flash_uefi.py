#!/usr/bin/env python3
"""Flash a validated VQEAF OS UEFI image to an install volume.

This is the VQEAF x86_64 UEFI equivalent of QeafdropOs
scripts/prepare_rescue_flash.py. It stages a boot image onto a FAT-formatted
install volume (USB drive, SD card, or mounted test LUN) and writes a rescue
command (QEAF.CMD) that the device OS will execute on next boot.

Safety model (same as QeafdropOs prepare_rescue_flash.py):

  1. Default is a dry run: prints the plan, writes nothing.
  2. --commit is required before any file is written to the target volume.
  3. The image is validated with scripts/validate_bootimg.py before staging;
     a refused image is never copied.
  4. The target volume must carry a VQEAF OS identity signature (BOOTX64.EFI,
     KERNEL.EFI, or VQEAF.TXT) -- refusing to stage onto an unrecognized volume.
  5. The system drive (C:) is never a valid target.
  6. The image is copied to the 8.3 name BOOTIMG.BIN, re-read, and compared
     byte-for-byte by SHA-256.
  7. QEAF.CMD is written with a confirm token. No partition table is touched --
     only files on the volume are written.

Usage:
    # Dry run: validate + plan, write nothing
    python scripts/flash_uefi.py --image dist/vqeaf-x86_64-uefi.img --volume E:

    # Actually stage (requires --commit):
    python scripts/flash_uefi.py --image dist/vqeaf-x86_64-uefi.img \
        --volume E: --commit
"""

import argparse
import hashlib
import importlib.util
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from validate_bootimg import validate_file  # noqa: E402

DEFAULT_IMAGE = ROOT / "dist" / "vqeaf-x86_64-uefi.img"

# QEAF.CMD is an instruction the device OS executes on boot. The confirm
# token is mandatory; this host side never generates a command without it.
PARTITION_TARGET = "uefi"
CONFIRM_TOKEN = "confirm"
CMD_LINE = "flash %s BOOTIMG.BIN %s" % (PARTITION_TARGET, CONFIRM_TOKEN)

# Files whose presence on a volume marks it as a VQEAF OS install / rescue
# volume. At least one must exist, otherwise we refuse to stage.
VOLUME_SIGNATURES = ("BOOTX64.EFI", "KERNEL.EFI", "VQEAF.TXT")

# 8.3 name used on the target volume.
STAGED_NAME = "BOOTIMG.BIN"
CMD_NAME = "QEAF.CMD"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def looks_like_vqeaf_volume(volume):
    """Return (ok, reason) for a directory-backed volume."""
    if not os.path.isdir(volume):
        return False, "%s is not a directory" % volume
    found = [n for n in VOLUME_SIGNATURES
             if os.path.isfile(os.path.join(volume, n))]
    if not found:
        return False, ("no %s found - this does not look like a VQEAF OS "
                       "volume" % " / ".join(VOLUME_SIGNATURES))
    return True, ",".join(found)


def is_system_drive(volume):
    """True when volume resolves to a bare Windows system drive root (C:)."""
    real = os.path.realpath(volume)
    stripped = real.rstrip("\\/")
    if len(stripped) <= 3 and stripped.endswith(":"):
        return stripped.upper().startswith("C:")
    return False


def main(argv=None):
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--image", default=str(DEFAULT_IMAGE),
                   help="UEFI image to stage (default: %(default)s)")
    p.add_argument("--volume", default=None,
                   help="Target install volume, e.g. E: or a mounted path "
                        "(required with --commit)")
    p.add_argument("--commit", action="store_true",
                   help="actually write BOOTIMG.BIN and QEAF.CMD "
                        "(default: dry run)")
    args = p.parse_args(argv)

    image = Path(args.image)
    if not image.is_file():
        print("REFUSED: image not found: %s" % image, file=sys.stderr)
        return 2

    # 1. Validate the image structurally (same guard as packaging).
    r = validate_file(str(image))
    print("[%s] %s (%d bytes, sha256 %s)"
          % ("PASS" if r.ok else "REFUSED", r.path,
             os.path.getsize(str(image)) if image.is_file() else 0,
             sha256(str(image))[:16]))
    for msg in r.messages:
        print("  " + msg)
    if not r.ok:
        print("Nothing was written. Do NOT put this file on an install volume.",
              file=sys.stderr)
        return 2

    print("  plan : copy -> %s ; %s = %r" % (STAGED_NAME, CMD_NAME, CMD_LINE))
    print("  VOLUME WRITE: this stages files on the install volume.")
    print("  --commit is your explicit consent; without it nothing is written.")
    print("  image sha256: %s" % sha256(str(image)))

    if not args.commit:
        print("\nDRY RUN - nothing written. Re-run with --commit "
              "--volume <install volume> once the LUN is mounted.")
        return 0

    if not args.volume:
        print("REFUSED: --commit needs --volume", file=sys.stderr)
        return 2

    if is_system_drive(args.volume):
        print("REFUSED: refusing to write to the system drive", file=sys.stderr)
        return 2

    ok, why = looks_like_vqeaf_volume(args.volume)
    if not ok:
        print("REFUSED: %s" % why, file=sys.stderr)
        return 2
    print("  volume signature: %s (%s)" % (args.volume, why))

    volume = args.volume

    # 2. Copy image to BOOTIMG.BIN on the volume.
    dst = os.path.join(volume, STAGED_NAME)
    shutil.copyfile(str(image), dst)
    dst_sha = sha256(dst)
    src_sha = sha256(str(image))
    if dst_sha != src_sha:
        print("REFUSED: copy mismatch src=%s dst=%s - remove %s before "
              "retrying" % (src_sha[:16], dst_sha[:16], STAGED_NAME),
              file=sys.stderr)
        return 2
    print("  wrote %s (%d bytes, sha256 verified)"
          % (dst, os.path.getsize(dst)))

    # 3. Write QEAF.CMD with the confirm token.
    cmd_path = os.path.join(volume, CMD_NAME)
    with open(cmd_path, "w", newline="\n") as f:
        f.write(CMD_LINE + "\n")
    print("  wrote %s (one line: %s)" % (cmd_path, CMD_LINE))

    print("\nNEXT (do not eject):")
    print("  1. flush the volume cache (sync / Write-VolumeCache), never eject")
    print("  2. boot the target device from the volume")
    print("  3. the device reads QEAF.CMD and executes: %s" % CMD_LINE)
    print("  4. after boot, read RESPONSE.TXT from the volume to confirm the flash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
