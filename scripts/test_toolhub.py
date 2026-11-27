#!/usr/bin/env python3
"""Build and exercise QEAF Tool Hub on the deterministic host simulator.

The arm64 release is validated as a device package. Interaction and pixels are
host x86_64 evidence only; this script does not emulate or access a phone.
"""
import ast
import hashlib
import importlib.util
import json
import re
import struct
import subprocess
import sys
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "examples" / "qeaf-toolhub"
OUT = ROOT / "build" / "toolhub-test"
FRAMES = OUT / "frames"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


qeaf = load("qeaf_toolhub_sdk", ROOT / "sdk" / "cli" / "qeaf.py")
qen = load("qeaf_toolhub_qen", ROOT / "sdk" / "compiler" / "qen.py")
compiler = load("qeaf_toolhub_compiler", ROOT / "sdk" / "compiler" / "qecompile.py")
qesim = load("qeaf_toolhub_sim", ROOT / "sdk" / "simulator" / "qesim.py")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_png(path, pixels, width, height):
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            pixel = pixels[y * width + x]
            rows.extend(((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", header)
                     + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
                     + chunk(b"IEND", b""))


def build_host_package():
    sources = sorted((PROJECT / "src").rglob("*.c"))
    entry = "build/host/native/x86_64/app.qen"
    module = compiler.compile_native(qen, PROJECT, "x86_64", sources, entry,
                                     abi_version=5)
    errors = qen.validate_module(module, "x86_64")
    require(not errors, "host module invalid: %s" % errors)
    manifest = json.loads((PROJECT / "manifest.json").read_text(encoding="utf-8"))
    manifest["architecture"] = ["x86_64"]
    manifest["entry"] = "native/x86_64/app.qen"
    package = OUT / "toolhub-x86_64.qeapp"
    entries = {
        "manifest.json": json.dumps(manifest, ensure_ascii=False,
                                    indent=2).encode("utf-8"),
        manifest["entry"]: module,
        "assets/icon.rgba": (PROJECT / "assets" / "icon.rgba").read_bytes(),
    }
    with zipfile.ZipFile(package, "w", zipfile.ZIP_STORED) as archive:
        for name, data in sorted(entries.items()):
            archive.writestr(name, data)
    qeaf.sign_package(package)
    errors = qeaf.validate_package(package)
    require(not errors, "host package invalid: %s" % errors)
    return package


def build_denied_package(host_package):
    with zipfile.ZipFile(host_package) as archive:
        entries = {name: archive.read(name) for name in archive.namelist()
                   if name != "SIGNATURE"}
    manifest = json.loads(entries["manifest.json"])
    manifest["id"] = "org.vqeaf.toolhub-no-input"
    manifest["permissions"] = ["ui.display"]
    entries["manifest.json"] = json.dumps(manifest, ensure_ascii=False,
                                          indent=2).encode("utf-8")
    package = OUT / "toolhub-no-input.qeapp"
    with zipfile.ZipFile(package, "w", zipfile.ZIP_STORED) as archive:
        for name, data in sorted(entries.items()):
            archive.writestr(name, data)
    qeaf.sign_package(package)
    errors = qeaf.validate_package(package)
    require(not errors, "denied-input package invalid: %s" % errors)
    return package


def check_font_coverage():
    font = (PROJECT / "src" / "font_data" / "font_data.c").read_text(encoding="utf-8")
    table = font[font.index("toolhub_font_codepoints"):font.index("};")]
    codepoints = {int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", table)}
    used = set()
    for name in ("main.c", "catalog.c"):
        source = (PROJECT / "src" / name).read_text(encoding="utf-8")
        for literal in re.findall(r'"(?:\\.|[^"\\])*"', source):
            used.update(ord(char) for char in ast.literal_eval(literal)
                        if ord(char) >= 32)
    missing = sorted(used - codepoints)
    require(not missing, "font lacks U+%04X" % missing[0] if missing else "")
    return {"glyphs": len(codepoints), "used_codepoints": len(used)}


def check_source_contract():
    source = (PROJECT / "src" / "main.c").read_text(encoding="utf-8")
    catalog = (PROJECT / "src" / "catalog.c").read_text(encoding="utf-8")
    allowed_api = {"blit_pixels", "log_utf8", "request_permission"}
    used_api = set(re.findall(r"g_app\.api->(\w+)", source))
    require(used_api <= allowed_api, "unexpected native API use: %s" % used_api)
    for forbidden in ("system(", "popen(", "fork(", "execve(", "socket(",
                      "read_file_part(", "curl ", "wget ", "chmod ", "bash "):
        require(forbidden not in source and forbidden not in catalog,
                "forbidden shell/network/storage token: %s" % forbidden)
    catalog_header = (PROJECT / "src" / "catalog.h").read_text()
    require("TOOLHUB_CATEGORY_COUNT 17u" in catalog_header, "category count changed")
    require("TOOLHUB_TOOL_COUNT 34u" in catalog_header, "tool count changed")
    return {"api_calls": sorted(used_api)}


def run(exe, package, name, keys=None, touches=None, expected_inputs=None,
        expected_permissions=9, save=True):
    raw = FRAMES / (name + ".raw")
    png = FRAMES / (name + ".png")
    command = [str(exe), str(package), "--device", "violet", "--fb", str(raw)]
    if keys is not None:
        command += ["--keys", keys]
    if touches is not None:
        command += ["--touches", touches]
    proc = subprocess.run(command, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    require(proc.returncode == 0,
            "%s failed (%d):\n%s\n%s" % (name, proc.returncode, proc.stdout, proc.stderr))
    require("state: destroyed" in proc.stdout, name + ": lifecycle did not finish")
    require("lifecycle: create start pause resume stop destroy" in proc.stdout,
            name + ": lifecycle order/format mismatch")
    require("outstanding allocs: 0" in proc.stdout, name + ": allocation leak")
    require("memory used: 0" in proc.stdout, name + ": arena not released")
    require("call timeout: 0" in proc.stdout, name + ": call budget exceeded")
    require("permissions: 0x%08x" % expected_permissions in proc.stdout,
            name + ": permission mask mismatch")
    if expected_inputs is not None:
        require("input delivered: %d" % expected_inputs in proc.stdout,
                name + ": delivered input count mismatch")
    require(raw.is_file() and raw.stat().st_size == 720 * 1560 * 4,
            name + ": framebuffer size mismatch")
    pixels = list(struct.unpack("<%dI" % (720 * 1560), raw.read_bytes()))
    digest = hashlib.sha256(raw.read_bytes()).hexdigest()
    if save:
        write_png(png, pixels, 720, 1560)
        raw.unlink()
    hash_match = re.search(r"framebuffer: 720x1560 hash=0x([0-9a-f]+)", proc.stdout)
    require(hash_match is not None, name + ": no framebuffer hash")
    result = {
        "command": command,
        "exit": proc.returncode,
        "inputs": expected_inputs,
        "permissions": expected_permissions,
        "framebuffer_hash": hash_match.group(1),
        "framebuffer_sha256": digest,
        "png": str(png.relative_to(ROOT)) if save else None,
    }
    print("PASS %-22s hash=%s inputs=%s" %
          (name, result["framebuffer_hash"], expected_inputs))
    return result, pixels


def check_parser(exe, package):
    invalid = [
        ("--touches", "1,2"), ("--touches", "1,2,2"),
        ("--touches", "-1,2,1"), ("--touches", "1,-2,1"),
        ("--touches", "1,2,1;"), ("--touches", "1,2,1;;3,4,0"),
        ("--touches", "x,2,1"), ("--touches", "1,2,1x"),
        ("--touches", ";".join(["1,2,1"] * 65)),
        ("--keys", "2,"), ("--keys", "2x"),
        ("--keys", ",".join(["2"] * 65)),
    ]
    for option, value in invalid:
        proc = subprocess.run([str(exe), str(package), option, value],
                              capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        require(proc.returncode == 2,
                "parser accepted invalid %s %r" % (option, value))
    print("PASS input parser           %d malformed scripts rejected" % len(invalid))
    return len(invalid)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    FRAMES.mkdir(parents=True, exist_ok=True)
    font = check_font_coverage()
    contract = check_source_contract()
    arm_package = compiler.compile_project(PROJECT, "release", qeaf)
    require(arm_package.stat().st_size <= 512 * 1024, "arm64 package exceeds 512 KiB")
    with zipfile.ZipFile(arm_package) as archive:
        manifest = json.loads(archive.read("manifest.json"))
        module = archive.read(manifest["entry"])
        require(len(archive.namelist()) <= 32, "arm64 package has too many entries")
    require(manifest["architecture"] == ["arm64"], "release architecture is not arm64")
    require(manifest["abi_version"] == 5, "release ABI is not v5")
    require(manifest["permissions"] == ["ui.display", "input.receive"],
            "release permissions are not least-privilege")
    require(not qen.validate_module(module, "arm64"), "arm64 module validation failed")
    print("PASS arm64 release          %d bytes sha256=%s" %
          (arm_package.stat().st_size, sha256(arm_package)))

    host_package = build_host_package()
    denied_package = build_denied_package(host_package)
    exe = qesim.build_simulator(OUT)
    parser_cases = check_parser(exe, host_package)

    menu, menu_pixels = run(exe, host_package, "menu", expected_inputs=0)
    repeat, repeat_pixels = run(exe, host_package, "menu-repeat", expected_inputs=0,
                                save=False)
    require(repeat["framebuffer_sha256"] == menu["framebuffer_sha256"],
            "initial framebuffer is not deterministic")
    (FRAMES / "menu-repeat.raw").unlink()

    touch_list, _ = run(
        exe, host_package, "touch-list",
        touches="282,555,1;282,555,0", expected_inputs=2)
    touch_detail, _ = run(
        exe, host_package, "touch-detail",
        touches="282,555,1;282,555,0;540,750,1;540,750,0", expected_inputs=4)
    unavailable, _ = run(exe, host_package, "detail-unavailable",
                         keys="2,28", expected_inputs=2)
    local, local_pixels = run(exe, host_package, "detail-local",
                              keys="10,28", expected_inputs=2)
    console, _ = run(exe, host_package, "console-perms",
                     keys="25", expected_inputs=1)
    green = 0xFF41E2A4
    runs = []
    run_start = None
    for y in range(1560):
        is_green = local_pixels[y * 720 + 37] == green
        if is_green and run_start is None:
            run_start = y
        elif not is_green and run_start is not None:
            if y - run_start >= 80:
                runs.append((run_start, y - 1))
            run_start = None
    require(len(runs) == 1, "local action accent was not found unambiguously")
    action_y = round(((runs[0][0] + runs[0][1]) / 2 + 1) * 1.5)
    local_action, _ = run(
        exe, host_package, "local-action", keys="10,28",
        touches="540,%d,1;540,%d,0" % (action_y, action_y), expected_inputs=4)
    require(local_action["framebuffer_hash"] == console["framebuffer_hash"],
            "local permission action did not open the real permission report")
    about, _ = run(exe, host_package, "about", keys="23", expected_inputs=1)
    exit_screen, _ = run(
        exe, host_package, "exit",
        touches="929,2238,1;929,2238,0", expected_inputs=2)
    scrolled, scrolled_pixels = run(
        exe, host_package, "menu-scrolled",
        touches="540,1800,1;540,1500,1;540,960,1;540,960,0", expected_inputs=4)
    require(scrolled_pixels[:284 * 720] == menu_pixels[:284 * 720],
            "scrolled content painted over the header")
    require(scrolled_pixels[1448 * 720:] == menu_pixels[1448 * 720:],
            "scrolled content painted over navigation")
    denied, denied_pixels = run(
        exe, denied_package, "input-denied",
        touches="282,555,1;282,555,0", expected_inputs=0,
        expected_permissions=1)
    require(denied["framebuffer_sha256"] == menu["framebuffer_sha256"],
            "touch changed pixels without input.receive")
    for name, result in (("touch-list", touch_list), ("touch-detail", touch_detail),
                         ("detail-unavailable", unavailable), ("detail-local", local),
                         ("console-perms", console), ("local-action", local_action),
                         ("about", about), ("exit", exit_screen),
                         ("menu-scrolled", scrolled)):
        require(result["framebuffer_hash"] != menu["framebuffer_hash"],
                name + ": interaction did not change the framebuffer")

    report = {
        "evidence": "host x86_64 simulator; not arm64 emulation or physical hardware",
        "arm64_package": str(arm_package.relative_to(ROOT)),
        "arm64_package_sha256": sha256(arm_package),
        "host_package": str(host_package.relative_to(ROOT)),
        "host_package_sha256": sha256(host_package),
        "font": font,
        "source_contract": contract,
        "parser_rejections": parser_cases,
        "scenarios": [menu, repeat, touch_list, touch_detail, unavailable, local,
                      console, local_action, about, exit_screen, scrolled, denied],
    }
    (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n",
                                     encoding="utf-8")
    print("test_toolhub: ALL OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as exc:
        print("test_toolhub: FAILED: %s" % exc, file=sys.stderr)
        raise SystemExit(1)
