import os, shutil, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from setup_toolchain import detect_toolchain

print("VQEAF OS host doctor")
print("Python", sys.version.split()[0])
print(f"Project root : {ROOT}")
print()

info = detect_toolchain()

checks = [
    ("x86_64-w64-mingw32-gcc", info["compiler"], "C compiler (MinGW-w64 x86_64)", True),
    ("cmake", info["cmake"], "CMake build system", True),
    ("make", info["make"], "GNU Make (optional; CMake can use Ninja)", False),
    ("nasm", info["nasm"], "NASM assembler (optional for .S files)", False),
    ("qemu-system-x86_64", info["qemu"], "QEMU emulator for UEFI testing", True),
    ("edk2-x86_64-code.fd", info["edk2_code"], "OVMF UEFI code firmware", True),
    ("edk2-x86_64-vars.fd", info["edk2_vars"], "OVMF UEFI vars firmware", True),
]

missing_required = []
missing_optional = []
for name, path, desc, required in checks:
    status = "FOUND " if path else "MISSING "
    print(status + name + (" -> " + str(path) if path else "") + f"  [{desc}]")
    if not path:
        if required:
            missing_required.append(name)
        else:
            missing_optional.append(name)

print()
share = Path(r"C:\Program Files\QEMU\share")
for name in ("edk2-x86_64-code.fd", "edk2-i386-vars.fd"):
    p = share / name
    print(("FOUND " if p.is_file() else "MISSING ") + name + " -> " + str(p))

if missing_required:
    print()
    print("Missing required:", ", ".join(missing_required))
    print("Run: python scripts/setup_toolchain.py --write")
    sys.exit(1)

if missing_optional:
    print()
    print("Missing optional (build may still work):", ", ".join(missing_optional))

print()
print("Host environment OK — ready to build.")
