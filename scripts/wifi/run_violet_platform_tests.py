#!/usr/bin/env python3
"""Build/run Violet platform host gate only with an explicitly validated board DTB."""
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import render_phone_ui as r

ROOT = Path(r.ROOT)
DTB_ENV = os.environ.get("QEAF_VIOLET_DTB")
MANIFEST_ENV = os.environ.get("QEAF_VIOLET_DTB_MANIFEST")
ALLOW_REFERENCE = os.environ.get("QEAF_ALLOW_REFERENCE_DTB") == "1"
OUT = ROOT / "build/violet-platform-test"
EXE = OUT / "vqeaf-violet-platform-test.exe"
SOURCES = [
    ROOT / "kernel/drivers/wifi/wcn3990/violet_platform.c",
    ROOT / "kernel/drivers/wcn3990_snoc.c",
    ROOT / "kernel/drivers/wcn3990_phase2.c",
    ROOT / "kernel/drivers/wifi/wcn3990/wcn3990_ce.c",
    ROOT / "system/core/driver.c",
    ROOT / "system/core/capability.c",
    ROOT / "tests/vqeaf_violet_platform_test.c",
]


def validate_board_dtb():
    if not DTB_ENV or not MANIFEST_ENV:
        raise ValueError(
            "Set QEAF_VIOLET_DTB and QEAF_VIOLET_DTB_MANIFEST; "
            "generic violet-reference.dtb is deliberately disabled."
        )
    dtb = Path(DTB_ENV).resolve()
    manifest_path = Path(MANIFEST_ENV).resolve()
    if not dtb.is_file() or not manifest_path.is_file():
        raise ValueError("Explicit Violet DTB or its manifest does not exist.")
    if dtb.name.lower() == "violet-reference.dtb":
        raise ValueError("Refusing generic SoC reference DTB as a Violet device tree.")

    raw = dtb.read_bytes()
    if len(raw) < 40 or raw[:4] != bytes.fromhex("d00dfeed"):
        raise ValueError("Input is not a flattened device tree (FDT).")
    for marker in (
        b"qcom,sm6150-idp",
        b"qcom,board-id",
        b"dsi_ft8719_tianma_vid_display",
        b"dsi_nt36672a_shenchao_vid_display",
    ):
        if marker not in raw:
            raise ValueError(f"Violet board DTB missing required marker: {marker.decode()}")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("target_device") != "violet":
        raise ValueError("DTB manifest target_device is not violet.")
    expected = manifest.get("base_dtb", {}).get("sha256", "").lower()
    actual = hashlib.sha256(raw).hexdigest()
    if not expected or expected != actual:
        raise ValueError("DTB SHA-256 does not match its manifest.")
    if not manifest.get("production_ready", False) and not ALLOW_REFERENCE:
        raise ValueError(
            "DTB is reference-only / kernel compatibility is unproven. "
            "Set QEAF_ALLOW_REFERENCE_DTB=1 only for host-only static tests."
        )
    if manifest.get("kernel_compatibility") != "exact" and not ALLOW_REFERENCE:
        raise ValueError("DTB source kernel does not exactly match the QEAF kernel.")
    print(f"DTB_VALIDATED={dtb}")
    print(f"DTB_SHA256={actual}")
    if not manifest.get("production_ready", False):
        print("WARNING: reference-only DTB; host test only, never boot/flash.")
    return dtb


def main():
    try:
        dtb = validate_board_dtb()
    except Exception as exc:
        print(f"DTB_GATE_FAIL: {exc}", file=sys.stderr)
        return 2

    OUT.mkdir(parents=True, exist_ok=True)
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu"] + r.CFLAGS + r.INCLUDES + \
          [str(s) for s in SOURCES] + ["-I", str(ROOT / "kernel/include"),
                                      "-I", str(ROOT / "tests"),
                                      "-o", str(EXE)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        print(p.stderr or p.stdout, file=sys.stderr)
        return p.returncode
    p = subprocess.run([str(EXE), str(dtb)], capture_output=True, text=True)
    print(p.stdout, end="")
    if p.stderr:
        print(p.stderr, end="", file=sys.stderr)
    return p.returncode


if __name__ == "__main__":
    raise SystemExit(main())
