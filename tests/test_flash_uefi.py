#!/usr/bin/env python3
"""Tests for scripts/flash_uefi.py.

Covers the QeafdropOs-inspired safety model:

  * dry run by default (nothing written without --commit)
  * missing image refused
  * volume signature check (must look like a VQEAF OS volume)
  * system drive (C:) refused
  * commit flow: copy to BOOTIMG.BIN, SHA-256 byte-for-byte verify,
    write QEAF.CMD with confirm token

Each test builds a temporary FAT-style volume directory with VQEAF signature
files, then runs flash_uefi.py as a subprocess.
"""

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLASH_UEFI = ROOT / "scripts" / "flash_uefi.py"
VALID_IMAGE = ROOT / "dist" / "vqeaf-x86_64-uefi.img"

VOLUME_SIGNATURES = ("BOOTX64.EFI", "KERNEL.EFI", "VQEAF.TXT")
STAGED_NAME = "BOOTIMG.BIN"
CMD_NAME = "QEAF.CMD"
CMD_LINE = "flash uefi BOOTIMG.BIN confirm"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


_make_volume_counter = [0]

def make_volume(td, signatures=None, extra_files=None):
    """Create a temp directory acting as a mountable volume."""
    _make_volume_counter[0] += 1
    vol = Path(td) / ("volume_%d" % _make_volume_counter[0])
    vol.mkdir()
    signatures = signatures or ["BOOTX64.EFI"]
    for name in signatures:
        (vol / name).write_bytes(b"dummy efi payload " + name.encode())
    extra_files = extra_files or {}
    for name, content in extra_files.items():
        (vol / name).write_bytes(content)
    return vol


def run_flash(args):
    return subprocess.run(
        [sys.executable, str(FLASH_UEFI), *args],
        capture_output=True, text=True,
    )


def main():
    failures = 0

    with tempfile.TemporaryDirectory(prefix="vqeaf-flash-test-") as td:
        # ---- Test 1: missing image refused ----
        r = run_flash(["--image", str(Path(td) / "nope.img"), "--volume", str(Path(td) / "vol")])
        if r.returncode == 0:
            print("FAIL T1: missing image should be refused")
            failures += 1
        else:
            print("PASS T1: missing image refused (exit %d)" % r.returncode)

        # ---- Test 2: dry run writes nothing ----
        vol = make_volume(td)
        r = run_flash(["--image", str(VALID_IMAGE), "--volume", str(vol)])
        if r.returncode != 0:
            print("FAIL T2: dry run should succeed, got %d: %s" % (r.returncode, r.stderr))
            failures += 1
        elif "DRY RUN - nothing written" not in r.stdout:
            print("FAIL T2: dry run message missing: %s" % r.stdout)
            failures += 1
        elif any((vol / n).exists() for n in (STAGED_NAME, CMD_NAME)):
            print("FAIL T2: dry run wrote files to volume")
            failures += 1
        else:
            print("PASS T2: dry run succeeded, nothing written")

        # ---- Test 3: volume without signatures refused ----
        bare = make_volume(td, signatures=[])
        r = run_flash(["--image", str(VALID_IMAGE), "--volume", str(bare), "--commit"])
        if r.returncode == 0:
            print("FAIL T3: unsigned volume should be refused")
            failures += 1
        else:
            print("PASS T3: unsigned volume refused (exit %d)" % r.returncode)

        # ---- Test 4: system drive C: refused ----
        r = run_flash(["--image", str(VALID_IMAGE), "--volume", "C:", "--commit"])
        if r.returncode == 0:
            print("FAIL T4: C: drive should be refused")
            failures += 1
        else:
            print("PASS T4: C: drive refused (exit %d)" % r.returncode)

        # ---- Test 5: --commit without --volume refused ----
        r = run_flash(["--image", str(VALID_IMAGE), "--commit"])
        if r.returncode == 0:
            print("FAIL T5: --commit without --volume should be refused")
            failures += 1
        else:
            print("PASS T5: --commit without --volume refused (exit %d)" % r.returncode)

        # ---- Test 6: commit flow writes BOOTIMG.BIN + QEAF.CMD, SHA-256 verified ----
        vol = make_volume(td)
        r = run_flash(["--image", str(VALID_IMAGE), "--volume", str(vol), "--commit"])
        if r.returncode != 0:
            print("FAIL T6: commit flow failed (%d): %s" % (r.returncode, r.stderr))
            failures += 1
        else:
            staged = vol / STAGED_NAME
            cmd = vol / CMD_NAME
            if not staged.is_file():
                print("FAIL T6: BOOTIMG.BIN not written")
                failures += 1
            elif not cmd.is_file():
                print("FAIL T6: QEAF.CMD not written")
                failures += 1
            else:
                staged_sha = sha256_file(staged)
                image_sha = sha256_file(VALID_IMAGE)
                cmd_text = cmd.read_text()
                if staged_sha != image_sha:
                    print("FAIL T6: BOOTIMG.BIN sha256 mismatch")
                    failures += 1
                elif CMD_LINE not in cmd_text:
                    print("FAIL T6: QEAF.CMD missing confirm line: %r" % cmd_text)
                    failures += 1
                elif "confirm" not in cmd_text:
                    print("FAIL T6: QEAF.CMD missing confirm token")
                    failures += 1
                else:
                    print("PASS T6: commit flow wrote BOOTIMG.BIN + QEAF.CMD, SHA-256 verified")

        # ---- Test 7: copy mismatch detected ----
        vol = make_volume(td)
        # Pre-write a corrupted BOOTIMG.BIN to simulate a stale/mismatched copy.
        (vol / STAGED_NAME).write_bytes(b"corrupted stale image")
        r = run_flash(["--image", str(VALID_IMAGE), "--volume", str(vol), "--commit"])
        if r.returncode == 0:
            print("FAIL T7: copy mismatch should be refused")
            failures += 1
        else:
            print("PASS T7: copy mismatch refused (exit %d)" % r.returncode)

        # ---- Test 8: multiple volume signatures all accepted ----
        for sig in VOLUME_SIGNATURES:
            vol = make_volume(td, signatures=[sig])
            r = run_flash(["--image", str(VALID_IMAGE), "--volume", str(vol), "--commit"])
            if r.returncode != 0:
                print("FAIL T8: volume with %s should be accepted (%d): %s"
                      % (sig, r.returncode, r.stderr))
                failures += 1
            else:
                print("PASS T8: volume with %s accepted" % sig)

    if failures:
        print("\n%d test(s) FAILED" % failures)
        return 1
    print("\nAll flash_uefi tests PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
