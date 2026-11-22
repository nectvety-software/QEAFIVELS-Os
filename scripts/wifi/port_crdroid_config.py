#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Port the Wi-Fi *configuration* from crDroid's violet device tree.

This repository's device tree contains NO portable C driver: qcacld-3.0 /
icnss live in the kernel/vendor, not here. What can be ported honestly is the
board config consumed by the real driver.

Safety: the upstream WCNSS_qcom_cfg.ini carries placeholder MAC addresses
(`Intf0MacAddress=000AF58989FF`). Those are NOT this device's factory MAC and
must never become one. They are commented out with an explanation; the real
MAC must come from /mnt/vendor/persist/wlan_mac.bin on the device.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = Path("C:/Users/doxuanhop/AppData/Local/Temp/opencode/crdroid-violet")
DEFAULT_OUT = ROOT / "firmware" / "violet" / "config"
UPSTREAM = "https://github.com/crdroidandroid/android_device_xiaomi_violet"
UPSTREAM_COMMIT = "faab5ba"

FILES = (
    ("wifi/WCNSS_qcom_cfg.ini", "WCNSS_qcom_cfg.ini"),
    ("wifi/wpa_supplicant_overlay.conf", "wpa_supplicant_overlay.conf"),
    ("wifi/p2p_supplicant_overlay.conf", "p2p_supplicant_overlay.conf"),
)

BANNER = (
    "# Ported from crDroid android_device_xiaomi_violet (%s).\n"
    "# Vendor placeholder MAC addresses are intentionally NOT active:\n"
    "# the real WCN3990 MAC must come from /mnt/vendor/persist/wlan_mac.bin.\n"
    "# No Android framework, Binder or HIDL dependency is introduced.\n" % UPSTREAM_COMMIT
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sanitize_ini(text: str) -> str:
    out = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("Intf0MacAddress=") or \
           stripped.startswith("Intf1MacAddress="):
            key = stripped.split("=", 1)[0]
            out.append("# [QEAF] disabled vendor placeholder MAC (not this device):")
            out.append("# %s=<removed>" % key)
            continue
        out.append(line)
    return "\n".join(out) + "\n"


def port(source: Path, out: Path) -> dict:
    if not source.is_dir():
        raise SystemExit("missing crDroid checkout: %s" % source)
    out.mkdir(parents=True, exist_ok=True)
    entries = []
    for rel, name in FILES:
        src = source / rel
        if not src.is_file():
            raise SystemExit("missing source file: %s" % src)
        raw = src.read_bytes()
        text = raw.decode("utf-8", errors="replace")
        if name.endswith(".ini"):
            text = BANNER + sanitize_ini(text)
        data = text.encode("utf-8")
        (out / name).write_bytes(data)
        entries.append({
            "source": rel,
            "name": name,
            "source_sha256": sha256(raw),
            "output_sha256": sha256(data),
            "bytes": len(data),
        })
    manifest = {
        "tool": "scripts/wifi/port_crdroid_config.py",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "upstream": UPSTREAM,
        "upstream_commit": UPSTREAM_COMMIT,
        "source": str(source),
        "files": entries,
        "notes": [
            "No C driver exists in the crDroid device tree; qcacld-3.0/icnss "
            "are kernel/vendor components and are not copied.",
            "Placeholder MAC addresses were removed; no MAC is synthesized.",
            "Files are data only: no Android framework/Binder/HIDL dependency.",
        ],
    }
    (out / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True),
        encoding="utf-8")
    return manifest


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    p.add_argument("--out", type=Path, default=DEFAULT_OUT)
    a = p.parse_args(argv)
    m = port(a.source, a.out)
    print(json.dumps({"manifest": str(a.out / "manifest.json"),
                      "files": len(m["files"])}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
