import os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
DIST = ROOT / "dist"
ENV_SCRIPT = ROOT / "vqeaf-env.sh"

# Load environment from vqeaf-env.sh if it exists (produced by setup_toolchain.py)
env = os.environ.copy()
if ENV_SCRIPT.is_file():
    result = subprocess.run(
        ["bash", "-c", f"source {ENV_SCRIPT.as_posix()} && env"],
        capture_output=True, text=True, cwd=ROOT
    )
    if result.returncode == 0:
        for line in result.stdout.splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                env[k] = v

cc = env.get("VQEAF_CC", "x86_64-w64-mingw32-gcc")
cmake = env.get("VQEAF_CMAKE", "cmake")
make_prog = env.get("VQEAF_MAKE", "mingw32-make")

# UEFI/freestanding compiler flags (passed directly to CMake to avoid
# CMake 4.4 compiler-try-compile issues with the toolchain file).
# NOTE: We deliberately do NOT set CMAKE_EXE_LINKER_FLAGS_INIT here, because
# CMake's try-compile test program links with default flags and would fail
# with the UEFI --entry=efi_main flag. UEFI linker flags are applied per-target
# via target_link_options() in CMakeLists.txt (add_efi).
c_flags = (
    "-ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar "
    "-mno-red-zone -fno-builtin -Wall -Wextra -mgeneral-regs-only "
    "-mno-stack-arg-probe -std=c11"
)

BUILD.mkdir(parents=True, exist_ok=True)
DIST.mkdir(parents=True, exist_ok=True)

BUILD.mkdir(parents=True, exist_ok=True)
DIST.mkdir(parents=True, exist_ok=True)

# Configure CMake with UEFI freestanding flags passed directly
cmd = [
    cmake, "-S", str(ROOT), "-B", str(BUILD),
    "-G", "MinGW Makefiles",
    f"-DCMAKE_C_COMPILER={cc}",
    f"-DCMAKE_ASM_COMPILER={cc}",
    f"-DCMAKE_MAKE_PROGRAM={make_prog}",
    f"-DCMAKE_C_FLAGS_INIT={c_flags}",
    f"-DCMAKE_C_FLAGS_DEBUG_INIT=-O0 -g",
    f"-DCMAKE_C_FLAGS_RELEASE_INIT=-O2 -DNDEBUG",
    "-DCMAKE_C_COMPILER_WORKS=TRUE",
    "-DCMAKE_C_COMPILER_ABI=elf",
    "-DCMAKE_C_COMPILER_ABI_LOADED=TRUE",
    "-DCMAKE_C_STANDARD_COMPUTED_DEFAULT=11",
    "-DCMAKE_C_EXTENSIONS_COMPUTED_DEFAULT=OFF",
    "-DCMAKE_BUILD_TYPE=Release",
]
subprocess.run(cmd, check=True, env=env)

# Build
subprocess.run([cmake, "--build", str(BUILD), "--parallel"], check=True, env=env)

# Package UEFI images (FAT12 ESP + validation)
subprocess.run([
    sys.executable, str(ROOT / "scripts" / "make_esp.py"),
    "--build", str(BUILD),
    "--out", str(DIST / "vqeaf-x86_64-uefi.img"),
    "--entry", "normal",
], check=True, env=env)
subprocess.run([
    sys.executable, str(ROOT / "scripts" / "make_esp.py"),
    "--build", str(BUILD),
    "--out", str(DIST / "vqeaf-x86_64-recovery.img"),
    "--entry", "recovery",
], check=True, env=env)
subprocess.run([
    sys.executable, str(ROOT / "scripts" / "package_bootimg.py"),
    "--build", str(BUILD),
    "--out", str(DIST / "vqeaf-x86_64-uefi.img"),
    "--entry", "normal",
], check=True, env=env)
subprocess.run([
    sys.executable, str(ROOT / "scripts" / "package_bootimg.py"),
    "--build", str(BUILD),
    "--out", str(DIST / "vqeaf-x86_64-recovery.img"),
    "--entry", "recovery",
], check=True, env=env)

# Test disks
subprocess.run([sys.executable, str(ROOT / "scripts" / "make_test_disk.py")], check=True, env=env)
subprocess.run([
    sys.executable, str(ROOT / "scripts" / "make_test_disk.py"),
    "--out", str(BUILD / "ahci-test.img"),
    "--name", "AHCI.TXT",
    "--content", "VQEAF OS AHCI/SATA FAT16 read-only mount OK",
], check=True, env=env)

print("UEFI boot, Recovery ESP, VirtIO and AHCI FAT16 test disks built.")
