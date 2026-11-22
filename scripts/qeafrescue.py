#!/usr/bin/env python3
"""QEAFRESCUE v1 host transport: detect, verify, and RAM-boot Violet.

This tool deliberately has no flash/erase/update command. RAM boot is invoked
only with --execute and only after Fastboot reports product=violet.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_IMAGE = ROOT / "dist" / "boot-vqeaf-violet.img"
MAGIC = b"ANDROID!"
PAGE_MIN, PAGE_MAX = 512, 65536


class RescueError(RuntimeError):
    pass


def tool_path(name: str) -> str:
    found = shutil.which(name)
    if found:
        return found
    local = os.environ.get("LOCALAPPDATA")
    if local:
        candidate = Path(local) / "Android" / "Sdk" / "platform-tools" / (name + ".exe")
        if candidate.is_file():
            return str(candidate)
    raise RescueError(f"{name} not found; install Android platform-tools")


def run(args: list[str], timeout: int = 20) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(args, capture_output=True, text=True,
                              errors="replace", timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RescueError(f"command failed: {args[0]}: {exc}") from exc


def parse_fastboot_devices(output: str) -> list[str]:
    devices = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[1].lower().startswith("fastboot"):
            devices.append(fields[0])
        elif len(fields) >= 2 and fields[1].lower() == "no permissions":
            devices.append(fields[0])
    return devices


def parse_adb_devices(output: str) -> list[dict[str, str]]:
    found = []
    for line in output.splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 2:
            found.append({"serial": fields[0], "state": fields[1]})
    return found


def detect() -> dict:
    result = {"adb": [], "fastboot": [], "errors": []}
    try:
        p = run([tool_path("adb"), "devices", "-l"])
        result["adb"] = parse_adb_devices(p.stdout)
        if p.returncode:
            result["errors"].append("adb: " + (p.stderr or p.stdout).strip())
    except RescueError as exc:
        result["errors"].append(str(exc))
    try:
        p = run([tool_path("fastboot"), "devices", "-l"])
        result["fastboot"] = parse_fastboot_devices(p.stdout + "\n" + p.stderr)
        if p.returncode:
            result["errors"].append("fastboot: " + (p.stderr or p.stdout).strip())
    except RescueError as exc:
        result["errors"].append(str(exc))
    return result


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def align(value: int, page: int) -> int:
    return (value + page - 1) // page * page


def inspect_boot_image(path: Path) -> dict:
    if not path.is_file():
        raise RescueError(f"image not found: {path}")
    size = path.stat().st_size
    if size < 2048:
        raise RescueError("image is too small to contain an Android boot header")
    with path.open("rb") as f:
        header = f.read(4096)
    if header[:8] != MAGIC:
        raise RescueError("image magic is not ANDROID!; refusing RAM boot")
    fields = struct.unpack_from("<10I", header, 8)
    kernel_size, kernel_addr, ramdisk_size, ramdisk_addr, second_size, second_addr, tags_addr, page, version, os_version = fields
    if page < PAGE_MIN or page > PAGE_MAX or page & (page - 1):
        raise RescueError(f"invalid boot page size: {page}")
    if version > 2:
        raise RescueError(f"unsupported Android boot header version: {version}")
    if not kernel_size or not ramdisk_size:
        raise RescueError("kernel or ramdisk payload is empty")
    kernel_off = page
    ramdisk_off = align(kernel_off + kernel_size, page)
    second_off = align(ramdisk_off + ramdisk_size, page)
    end = align(second_off + second_size, page) if second_size else second_off
    if end > size:
        raise RescueError(f"truncated boot image: payload end {end}, file size {size}")
    name = header[48:64].split(b"\0", 1)[0].decode("ascii", "replace")
    cmdline = header[64:576].split(b"\0", 1)[0].decode("ascii", "replace")
    return {
        "path": str(path.resolve()), "size": size, "sha256": sha256(path),
        "header_version": version, "page_size": page,
        "kernel_size": kernel_size, "kernel_addr": kernel_addr,
        "ramdisk_size": ramdisk_size, "ramdisk_addr": ramdisk_addr,
        "second_size": second_size, "payload_end": end,
        "name": name, "cmdline": cmdline,
    }


def fastboot_getvar(fastboot: str, serial: str, key: str) -> str:
    p = run([fastboot, "-s", serial, "getvar", key])
    text = (p.stdout + "\n" + p.stderr).strip()
    # Fastboot commonly writes getvar output to stderr and may return 0 or 1.
    for line in text.splitlines():
        if ":" in line:
            lhs, rhs = line.split(":", 1)
            if lhs.strip().lower().endswith(key.lower()):
                return rhs.strip()
    raise RescueError(f"cannot verify Fastboot variable '{key}': {text or 'no response'}")


def select_fastboot(serial_arg: str | None) -> tuple[str, str]:
    fb = tool_path("fastboot")
    devices = detect()["fastboot"]
    if serial_arg:
        if serial_arg not in devices:
            raise RescueError(f"requested Fastboot serial {serial_arg!r} is not connected")
        return fb, serial_arg
    if len(devices) != 1:
        raise RescueError(f"expected exactly one Fastboot device; detected {len(devices)}: {devices}")
    return fb, devices[0]


def cmd_detect(_args) -> int:
    result = detect()
    print(json.dumps(result, indent=2, ensure_ascii=False))
    if not result["adb"] and not result["fastboot"]:
        print("QEAFRESCUE: no USB device detected")
        return 2
    return 0


def cmd_verify(args) -> int:
    report = inspect_boot_image(Path(args.image))
    if args.expect_sha256 and report["sha256"].lower() != args.expect_sha256.lower():
        raise RescueError("SHA-256 mismatch: expected " + args.expect_sha256 +
                          ", got " + report["sha256"])
    print(json.dumps(report, indent=2, ensure_ascii=False))
    print("IMAGE VERIFY: PASS")
    return 0


def cmd_ram_boot(args) -> int:
    report = inspect_boot_image(Path(args.image))
    if args.expect_sha256 and report["sha256"].lower() != args.expect_sha256.lower():
        raise RescueError("SHA-256 mismatch; refusing RAM boot")
    fb, serial = select_fastboot(args.serial)
    product = fastboot_getvar(fb, serial, "product")
    if product.strip().lower() != "violet":
        raise RescueError(f"device product is {product!r}, not 'violet'; refusing")
    try:
        unlocked = fastboot_getvar(fb, serial, "unlocked")
    except RescueError:
        unlocked = "unknown"
    if unlocked.lower() in ("no", "false", "0", "locked"):
        raise RescueError("bootloader reports locked; refusing unsigned RAM boot")
    if unlocked.lower() not in ("yes", "true", "1", "unlocked"):
        raise RescueError(f"bootloader unlock state is unverifiable ({unlocked!r}); refusing RAM boot")
    print(f"DEVICE VERIFY: PASS serial={serial} product={product} unlocked={unlocked}")
    print(f"IMAGE VERIFY: PASS sha256={report['sha256']} size={report['size']}")
    if not args.execute:
        print("DRY RUN: no device command sent. Re-run with --execute to issue fastboot boot.")
        return 0
    print("RAM BOOT: sending 'fastboot boot' (temporary boot; no partition write)")
    p = run([fb, "-s", serial, "boot", str(Path(args.image).resolve())], timeout=args.timeout)
    output = (p.stdout + "\n" + p.stderr).strip()
    print(output or f"fastboot exit code {p.returncode}")
    if p.returncode != 0 or "FAILED" in output.upper():
        raise RescueError("Fastboot RAM boot was rejected; no flash fallback is attempted")
    print("RAM BOOT: command accepted; verify the phone's boot/runtime logs")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    sub.add_parser("detect", help="read-only ADB/Fastboot USB detection")
    verify = sub.add_parser("verify", help="validate Android boot image and SHA-256")
    verify.add_argument("--image", default=str(DEFAULT_IMAGE))
    verify.add_argument("--expect-sha256")
    boot = sub.add_parser("ram-boot", help="temporary Fastboot boot; never flashes")
    boot.add_argument("--image", default=str(DEFAULT_IMAGE))
    boot.add_argument("--serial")
    boot.add_argument("--expect-sha256")
    boot.add_argument("--timeout", type=int, default=120)
    boot.add_argument("--execute", action="store_true",
                      help="required to actually send the non-persistent fastboot boot command")
    args = ap.parse_args(argv)
    try:
        if args.command == "detect":
            return cmd_detect(args)
        if args.command == "verify":
            return cmd_verify(args)
        if args.command == "ram-boot":
            return cmd_ram_boot(args)
    except RescueError as exc:
        print("QEAFRESCUE ERROR: " + str(exc), file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
