#!/usr/bin/env python3
"""Verify the packaged Violet native-QEAF connectivity runtime (newc CPIO)."""
from pathlib import Path
import gzip
import stat
import sys

root = Path(__file__).resolve().parents[1]
archive = root / "build/violet/qeaf-initramfs.cpio.gz"
if not archive.is_file():
    raise SystemExit(f"missing {archive}")

data = gzip.decompress(archive.read_bytes())
entries = {}
pos = 0
while pos + 110 <= len(data):
    hdr = data[pos:pos + 110]
    if hdr[:6] not in (b"070701", b"070702"):
        raise SystemExit(f"invalid newc header at {pos}")
    mode = int(hdr[14:22], 16)
    size = int(hdr[54:62], 16)
    namesz = int(hdr[94:102], 16)
    name_start = pos + 110
    name_end = name_start + namesz
    name = data[name_start:name_end - 1].decode("utf-8", "replace").lstrip("./")
    body_start = (name_end + 3) & ~3
    body_end = body_start + size
    entries[name] = (mode, data[body_start:body_end])
    pos = (body_end + 3) & ~3
    if name == "TRAILER!!!":
        break

# Runtime APEX path as staged into the initramfs by package_violet.py.
APEX_LINKER = "apex/com.qeafivels.runtime/bin/linker64"

required = [
    "sbin/qeaf/pd-mapper", "sbin/qeaf/tqftpserv", "sbin/qeaf/rmtfs",
    "sbin/qeaf/wlfwd", "sbin/qeaf/pdr-restart", "sbin/qeaf/pd-listen",
    "sbin/qeaf/qrtr-lookup", "sbin/qeaf/cnss-daemon",
    "system/bin/linker64", APEX_LINKER,
    "system/lib64/libc.so", "system/lib64/libcutils.so",
    "system/lib64/liblog.so", "system/lib64/libnl.so",
    "vendor/lib64/libqmi_cci.so", "vendor/lib64/libqmi_common_so.so",
    "vendor/lib64/libcld80211.so", "vendor/lib64/libperipheral_client.so",
    "vendor/lib64/libmdmdetect.so", "vendor/lib64/libqmiservices.so",
    "vendor/etc/wifi/WCNSS_qcom_cfg.ini",
    "lib/firmware/bdwlan.bin", "lib/firmware/regdb.bin",
    "lib/firmware/wlanmdsp.mbn",
    "lib/firmware/wlan/qca_cld/bdwlan.bin",
    "lib/firmware/wlan/qca_cld/regdb.bin",
    "lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini",
    "etc/qeaf/radio-firmware-status.txt",
]
missing = [name for name in required if name not in entries]
if missing:
    raise SystemExit("missing package inputs:\n  " + "\n  ".join(missing))

for name in ("sbin/qeaf/cnss-daemon", "system/bin/linker64",
             APEX_LINKER):
    mode = stat.S_IMODE(entries[name][0])
    if not mode & 0o111:
        raise SystemExit(f"{name} is not executable in CPIO: {mode:04o}")

for name in entries:
    if name.startswith("tmp/qeaf-pd/") or name.startswith("tmp/rmtfs/"):
        raise SystemExit(f"synthetic temporary state must not be packaged: {name}")

print(f"PASS: {len(entries)} CPIO entries")
print("PASS: native QRTR daemons + CNSS + Bionic interpreter/dependencies")
print("PASS: CNSS and both linker64 interpreters are executable")
print("PASS: no generated PD maps or blank RAM-EFS files packaged")
