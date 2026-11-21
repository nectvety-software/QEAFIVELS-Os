"""Build helpers for the QEAPP native host simulator.

Shared by the ``qeaf simulate`` CLI command and scripts/test_qeapp_simulator.py
so both compile the exact same sources: the real device package inspector
(vqeaf_qeapp.c + vqeaf_qeapp_native.c) plus the simulator core. Only the final
translation unit differs (CLI driver vs. test driver).
"""

import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "platform" / "violet" / "ui"

INCLUDES = [
    "-I", str(UI),
    "-I", str(UI / "fonts"),
    "-I", str(ROOT / "platform" / "violet"),
    "-I", str(ROOT / "platform" / "violet" / "channel"),
    "-I", str(ROOT / "platform" / "lvgl"),
    "-I", str(ROOT / "platform" / "lvgl" / "src"),
    "-I", str(ROOT / "framework" / "application_api"),
    "-I", str(ROOT / "framework" / "simulator"),
    "-I", str(ROOT / "framework" / "security"),
    "-I", str(ROOT / "system" / "ui" / "include"),
]
# -mno-red-zone: the API callbacks are sysv_abi but call Win64 libc (snprintf,
# malloc). SysV's 128-byte red zone below rsp is not respected by Win64 callees,
# so a callback local kept there gets clobbered mid-call. Disabling the red zone
# forces real stack frames and makes the mixed-ABI host safe at any -O level.
CFLAGS = ["-O1", "-g", "-Wall", "-Wextra", "-std=c11", "-mno-red-zone",
          "-D__linux__", "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_LVGL_H_INCLUDE_SIMPLE"]
CORE_SOURCES = [
    UI / "vqeaf_qeapp.c",
    UI / "vqeaf_qeapp_native.c",
    ROOT / "framework" / "security" / "qeaf_broker.c",
    ROOT / "framework" / "simulator" / "qeaf_device.c",
    ROOT / "framework" / "simulator" / "qeaf_sim.c",
    ROOT / "framework" / "simulator" / "qeaf_sim_stubs.c",
    ROOT / "platform" / "violet" / "panel_scale.c",
]
CLI_MAIN = ROOT / "framework" / "simulator" / "qeaf_sim_main.c"


def find_zig():
    env = os.environ.get("ZIG")
    if env and Path(env).is_file():
        return env
    base = os.environ.get("QEAFDROPOS") or "D:\\QeafivelsOfficial\\QeafdropOs"
    candidate = Path(base) / "out" / "toolchain" / "zig-windows-x86_64-0.13.0" / "zig.exe"
    if candidate.is_file():
        return str(candidate)
    raise ValueError("zig toolchain not found; set ZIG=/path/to/zig")


def _run(cmd):
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise ValueError("command failed: %s\n%s"
                         % (" ".join(str(c) for c in cmd), proc.stderr or proc.stdout))
    return proc


def _object(source, outdir, zig):
    obj = outdir / (str(source).replace(":", "").replace(os.sep, "_")
                    .replace("/", "_").replace(".", "_") + ".o")
    if obj.is_file() and obj.stat().st_mtime >= Path(source).stat().st_mtime:
        return obj
    _run([zig, "cc", "-target", "x86_64-windows-gnu"] + CFLAGS + INCLUDES
         + ["-c", str(source), "-o", str(obj)])
    return obj


def build(outdir, main_source, exe_name, extra_sources=(), libs=()):
    """Compile the simulator core + a main translation unit and link an exe."""
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    zig = find_zig()
    objects = [_object(src, outdir, zig) for src in CORE_SOURCES]
    objects += [_object(src, outdir, zig) for src in extra_sources]
    objects.append(_object(main_source, outdir, zig))
    exe = outdir / exe_name
    _run([zig, "cc", "-target", "x86_64-windows-gnu", "-o", str(exe)]
         + [str(o) for o in objects] + list(libs))
    return exe


def build_simulator(outdir):
    # The CLI's --ui mode uses plain Win32 + GDI for its window.
    return build(outdir, CLI_MAIN, "qeaf-sim.exe",
                 libs=("-luser32", "-lgdi32"))