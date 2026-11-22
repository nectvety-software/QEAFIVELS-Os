#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inspect a violet firmware package without extracting or modifying it.

Read-only: lists members, hashes them, detects duplicate payloads, classifies
known WCN3990 WLAN images and writes a manifest + compatibility report.
Never invents a file, calibration blob or MAC address.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

WLAN_NAMES = ("wlanmdsp.mbn", "bdwlan", "regdb.bin", "qdss_trace_config.cfg")
BT_NAMES = ("btfm.bin", "bt_firmware")
MODEM_NAMES = ("modem.mdt", "mba.mbn", "non-hlos", "nhlos")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def classify(name: str) -> str:
    low = Path(name).name.lower()
    if "wlanmdsp.mbn" in low or low.startswith("bdwlan") or "regdb.bin" in low:
        return "wcn3990_wlan"
    if "qdss" in low:
        return "wlan_diag"
    if any(x in low for x in BT_NAMES):
        return "bluetooth"
    if any(x in low for x in ("modem.mdt", "mba.mbn", "nhlos", "non-hlos")):
        return "modem"
    return "other"


def magic(data: bytes) -> str:
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return "png"
    if data[:4] == b"PK\x03\x04":
        return "zip"
    if data[:4] == b"\x7fELF":
        return "elf"
    if data[:2] == b"MZ":
        return "pe"
    if data[:4] in (b"\xef\xbb\xbf", b"\x00\x00\x00\x00"):
        return "unknown"
    return "raw/binary"


def inspect(package: Path, out_dir: Path) -> dict:
    if not package.is_file():
        raise SystemExit("missing firmware package: %s" % package)
    out_dir.mkdir(parents=True, exist_ok=True)
    items = []
    by_hash: dict[str, list[str]] = defaultdict(list)
    total = 0
    with zipfile.ZipFile(package, "r") as zf:
        for info in zf.infolist():
            if info.is_dir():
                continue
            with zf.open(info, "r") as fh:
                data = fh.read()
            digest = sha256(data)
            by_hash[digest].append(info.filename)
            total += len(data)
            items.append({
                "path": info.filename,
                "size": len(data),
                "sha256": digest,
                "kind": classify(info.filename),
                "format": magic(data),
                "compressed_size": info.compress_size,
            })
    required = {
        "wlanmdsp.mbn": any("wlanmdsp.mbn" in i["path"].lower() for i in items),
        "bdwlan": any("bdwlan" in i["path"].lower() for i in items),
        "regdb.bin": any("regdb.bin" in i["path"].lower() for i in items),
    }
    manifest = {
        "tool": "scripts/wifi/inspect_violet_firmware.py",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "package": str(package),
        "package_sha256": sha256(package.read_bytes()),
        "member_count": len(items),
        "total_uncompressed_bytes": total,
        "required_present": required,
        "duplicate_payloads": {h: names for h, names in by_hash.items()
                               if len(names) > 1},
        "files": items,
        "notes": [
            "Read-only inspection; no file was extracted or written to a device.",
            "Firmware region/version is NOT inferred from file names; verify SKU separately.",
            "Calibration/NV and factory MAC must come from this device's persist dump.",
        ],
    }
    (out_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True),
        encoding="utf-8")
    return manifest


def main(argv=None) -> int:
    import json as _json
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--package", type=Path, default=ROOT / "build" / "violet-fw.zip")
    p.add_argument("--out", type=Path, default=ROOT / "firmware" / "violet")
    a = p.parse_args(argv)
    m = inspect(a.package, a.out)
    print(_json.dumps({
        "manifest": str(a.out / "manifest.json"),
        "members": m["member_count"],
        "bytes": m["total_uncompressed_bytes"],
        "required_present": m["required_present"],
        "duplicates": len(m["duplicate_payloads"]),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
