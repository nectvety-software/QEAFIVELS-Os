#!/usr/bin/env python3
"""VQEAF OS toolchain setup and validation.

Detects available build tools, locates QEMU + EDK2 firmware, creates the
CMake toolchain file if needed, and validates that a UEFI build can proceed.

Usage:
    python scripts/setup_toolchain.py          # detect and report
    python scripts/setup_toolchain.py --write  # also write toolchain file + env script
"""

import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE_DIR = ROOT / "cmake"
TOOLCHAIN_FILE = CMAKE_DIR / "VQEAFToolchain.cmake"
ENV_SCRIPT = ROOT / "vqeaf-env.sh"

# Candidate locations for tools on this Windows/MSYS2 host.
QEMU_CANDIDATES = [
    Path(r"C:\Program Files\QEMU\qemu-system-x86_64.exe"),
    Path(r"C:\Program Files\qemu\qemu-system-x86_64.exe"),
    Path("/c/Program Files/QEMU/qemu-system-x86_64.exe"),
    Path("/c/Program Files/qemu/qemu-system-x86_64.exe"),
]
EDK2_CODE_CANDIDATES = [
    Path(r"C:\Program Files\QEMU\share\edk2-x86_64-code.fd"),
    Path("/c/Program Files/QEMU/share/edk2-x86_64-code.fd"),
]
EDK2_VARS_CANDIDATES = [
    Path(r"C:\Program Files\QEMU\share\edk2-x86_64-vars.fd"),
    Path("/c/Program Files/QEMU/share/edk2-x86_64-vars.fd"),
]
COMPILER_CANDIDATES = [
    "x86_64-w64-mingw32-gcc",
    Path("/c/msys64/mingw64/bin/x86_64-w64-mingw32-gcc.exe"),
    Path("C:/msys64/mingw64/bin/x86_64-w64-mingw32-gcc.exe"),
]
MAKE_CANDIDATES = [
    "make",
    "mingw32-make",
    Path("/c/msys64/usr/bin/make.exe"),
    Path("/c/msys64/mingw64/bin/mingw32-make.exe"),
    Path("C:/msys64/usr/bin/make.exe"),
    Path("C:/msys64/mingw64/bin/mingw32-make.exe"),
]


def find_first(existing):
    for p in existing:
        if isinstance(p, str):
            found = shutil.which(p)
            if found:
                return Path(found)
        elif isinstance(p, Path) and p.is_file():
            return p
    return None


def detect_toolchain() -> dict:
    info = {
        "compiler": find_first(COMPILER_CANDIDATES),
        "cmake": Path(shutil.which("cmake")) if shutil.which("cmake") else None,
        "make": find_first(MAKE_CANDIDATES),
        "nasm": Path(shutil.which("nasm")) if shutil.which("nasm") else None,
        "qemu": find_first(QEMU_CANDIDATES),
        "edk2_code": find_first(EDK2_CODE_CANDIDATES),
        "edk2_vars": find_first(EDK2_VARS_CANDIDATES),
    }

    # Auto-create edk2 x86_64 vars.fd from code.fd if vars.fd is missing.
    # QEMU needs a writable vars.fd for UEFI variable storage.
    if not info["edk2_vars"] and info["edk2_code"]:
        auto_vars = ROOT / "build" / "edk2-x86_64-vars.fd"
        if not auto_vars.is_file():
            try:
                import shutil as _shutil
                _shutil.copyfile(info["edk2_code"], auto_vars)
                info["edk2_vars"] = auto_vars
            except Exception:
                pass
        else:
            info["edk2_vars"] = auto_vars

    return info


def write_env_script(info: dict) -> None:
    lines = [
        "#!/usr/bin/env bash",
        "# VQEAF OS build environment — source this before building",
        f"export VQEAF_ROOT=\"{ROOT.as_posix()}\"",
        f"export VQEAF_CC=\"{info['compiler'].as_posix() if info['compiler'] else ''}\"",
        f"export VQEAF_CMAKE=\"{info['cmake'].as_posix() if info['cmake'] else ''}\"",
        f"export VQEAF_MAKE=\"{info['make'].as_posix() if info['make'] else ''}\"",
        f"export VQEAF_QEMU=\"{info['qemu'].as_posix() if info['qemu'] else ''}\"",
        f"export VQEAF_OVMF_CODE=\"{info['edk2_code'].as_posix() if info['edk2_code'] else ''}\"",
        f"export VQEAF_OVMF_VARS=\"{info['edk2_vars'].as_posix() if info['edk2_vars'] else ''}\"",
        "",
        "# Add compiler to PATH if not already present",
        f'export PATH="{info["compiler"].parent.as_posix() if info["compiler"] else ""}:$PATH"',
    ]
    ENV_SCRIPT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote environment script: {ENV_SCRIPT}")


def report(info: dict) -> int:
    print("=== VQEAF OS Toolchain Detection ===")
    print(f"Project root : {ROOT}")
    print()

    checks = [
        ("x86_64-w64-mingw32-gcc", info["compiler"], "C compiler (MinGW-w64 x86_64)"),
        ("cmake", info["cmake"], "CMake build system"),
        ("make", info["make"], "GNU Make (optional; CMake can use Ninja)"),
        ("nasm", info["nasm"], "NASM assembler (optional for .S files)"),
        ("qemu-system-x86_64", info["qemu"], "QEMU emulator for UEFI testing"),
        ("edk2-x86_64-code.fd", info["edk2_code"], "OVMF UEFI code firmware"),
        ("edk2-x86_64-vars.fd", info["edk2_vars"], "OVMF UEFI vars firmware"),
    ]

    missing = []
    for name, path, desc in checks:
        status = "OK" if path else "MISSING"
        print(f"  [{status}] {name}: {path or '(not found)'}  — {desc}")
        if not path:
            missing.append(name)

    print()
    if missing:
        print("Missing tools:", ", ".join(missing))
        print()
        print("Install suggestions (Windows / MSYS2):")
        print("  - MinGW-w64: pacman -S mingw-w64-x86_64-gcc   (via MSYS2)")
        print("  - CMake:     pacman -S cmake                  (via MSYS2)")
        print("  - Make:      pacman -S make                   (via MSYS2)")
        print("  - NASM:      pacman -S nasm                   (via MSYS2)")
        print("  - QEMU:      https://www.qemu.org/download/   (ensure edk2 firmware included)")
        print()
        print("Run: python scripts/setup_toolchain.py --write")
        print("     to write vqeaf-env.sh with detected paths.")
        return 1

    print("All required tools found. Toolchain is ready.")
    return 0


def main() -> int:
    write = "--write" in sys.argv
    info = detect_toolchain()

    rc = report(info)

    if write:
        CMAKE_DIR.mkdir(parents=True, exist_ok=True)
        write_env_script(info)
        if TOOLCHAIN_FILE.exists():
            print(f"Toolchain file already exists: {TOOLCHAIN_FILE}")
        else:
            print(f"Toolchain file: {TOOLCHAIN_FILE} (created manually, see cmake/VQEAFToolchain.cmake)")

    return rc


if __name__ == "__main__":
    raise SystemExit(main())
