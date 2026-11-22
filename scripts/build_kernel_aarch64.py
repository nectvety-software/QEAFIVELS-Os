#!/usr/bin/env python3
"""Build the independent VQEAF kernel for AArch64 (QEMU virt).

Freestanding aarch64-linux-musl via zig, linked at 0x40000000 with
kernel/arch/aarch64/kernel.ld. Produces build/kernel-aarch64/vqeaf-kernel-aarch64.elf.

This is the independent OS kernel port, NOT the Linux-musl violet prototype
(build_violet.py) and NOT the x86_64 UEFI kernel (scripts/build.py).
"""
import os
import subprocess
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
ARCH = ROOT / "kernel" / "arch" / "aarch64"
OUT = ROOT / "build" / "kernel-aarch64"


def find_zig():
    env = os.environ.get("ZIG")
    if env and Path(env).is_file():
        return env
    base = os.environ.get("QEAFDROPOS") or "D:\\QeafivelsOfficial\\QeafdropOs"
    candidate = Path(base) / "out" / "toolchain" / "zig-windows-x86_64-0.13.0" / "zig.exe"
    if candidate.is_file():
        return str(candidate)
    raise SystemExit("zig toolchain not found; set ZIG=/path/to/zig")


def run(cmd, timeout=180):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = find_zig()
    # Embed the EL0 user ELF (generated header) before compiling entry.c.
    gen = run([sys.executable, str(ROOT / "scripts" / "gen_user_elf_aarch64.py")])
    if gen.returncode != 0:
        raise SystemExit("user ELF aarch64 generation FAILED:\n"
                         + (gen.stderr or gen.stdout))
    genq = run([sys.executable, str(ROOT / "scripts" / "gen_qen_aarch64.py")])
    if genq.returncode != 0:
        raise SystemExit("user .qen aarch64 generation FAILED:\n"
                         + (genq.stderr or genq.stdout))
    inc = ["-I", str(ROOT / "kernel" / "include")]
    flags = ["-target", "aarch64-linux-musl", "-ffreestanding", "-fno-pie",
             "-fno-builtin", "-fno-stack-protector", "-mstrict-align",
             "-O2", "-std=c11", "-Wall", "-Wextra", "-DVQEAF_ELF_MACHINE=0xB7"]
    flags += os.environ.get("VQEAF_EXTRA_CFLAGS", "").split()
    sources = [ARCH / "start.S", ARCH / "uart.c", ARCH / "vm.c",
               ARCH / "vectors.S", ARCH / "irq.c", ARCH / "sched.c",
               ARCH / "user.S",                ARCH / "entry.c", ARCH / "display.c",
               ARCH / "compositor.c", ARCH / "window.c",
               ROOT / "kernel" / "core" / "memory.c",
               ROOT / "kernel" / "core" / "elf.c",
               ROOT / "kernel" / "core" / "qen.c",
               ROOT / "kernel" / "core" / "ams.c",
               ROOT / "kernel" / "core" / "security.c",
               ROOT / "kernel" / "core" / "vfs.c",
               ROOT / "kernel" / "core" / "ramfs.c",
               ROOT / "kernel" / "core" / "freestanding.c",
               ROOT / "kernel" / "drivers" / "virtio_mmio_blk.c",
               ROOT / "kernel" / "drivers" / "wcn3990_snoc.c",
               ROOT / "kernel" / "drivers" / "wcn3990_phase2.c",
               ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" / "wifi_device.c",
               ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" / "wifi_firmware.c",
               ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" / "qrtr_transport.c",
               ROOT / "kernel" / "drivers" / "wifi" / "wcn3990" / "qmi_service.c",
               ROOT / "system" / "core" / "driver.c",
               ROOT / "system" / "core" / "capability.c",
               ROOT / "system" / "core" / "block.c"]
    objs = []
    for src in sources:
        obj = OUT / (src.stem + ".o")
        proc = run([zig, "cc"] + flags + inc + ["-c", str(src), "-o", str(obj)])
        if proc.returncode != 0:
            raise SystemExit("aarch64 kernel compile FAILED %s:\n%s"
                             % (src.name, proc.stderr or proc.stdout))
        objs.append(obj)
    elf = OUT / "vqeaf-kernel-aarch64.elf"
    proc = run([zig, "cc", "-target", "aarch64-linux-musl", "-nostdlib",
                "-ffreestanding", "-fno-pie", "-no-pie",
                "-Wl,-T," + (ARCH / "kernel.ld").as_posix(),
                "-Wl,--build-id=none", "-o", str(elf)] + objs)
    if proc.returncode != 0:
        raise SystemExit("aarch64 kernel link FAILED:\n" + (proc.stderr or proc.stdout))
    head = elf.read_bytes()[:20]
    if head[:4] != b"\x7fELF" or head[18:20] != b"\xb7\x00":
        raise SystemExit("aarch64 kernel: output is not an AArch64 ELF")
    return elf


def main():
    elf = build()
    print("build_kernel_aarch64: linked %s (%d bytes)"
          % (elf.as_posix(), elf.stat().st_size))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())