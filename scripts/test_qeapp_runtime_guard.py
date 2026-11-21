#!/usr/bin/env python3
"""Phase 4 host gate: guarded native runtime (crash isolation, timeout, bounded
arena, canvas, resource release).

Builds clean/crash/hang fixtures from freestanding C plus an over-bound-arena
module, builds the runner (simulator CLI) and the guard test (which links the
device runtime supervisor), then runs the guard test. The crash and hang cases
prove the supervisor survives a faulting/hanging child; the clean case proves a
720x1560 XRGB8888 canvas is returned; the recovery case proves resources are
released across runs. Host (x86_64) evidence only.
"""
import importlib.util
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "qeapp-runtime-guard"
FIXTURES = ROOT / "tests" / "fixtures" / "qeapp-sim"
TEMPLATE = ROOT / "sdk" / "templates" / "hello-qeapp-native"
GUARD_TEST = ROOT / "tests" / "vqeaf_runtime_guard_test.c"
RUNTIME_C = ROOT / "platform" / "violet" / "ui" / "vqeaf_qeapp_runtime.c"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sim_gate = load("sim_gate", ROOT / "scripts" / "test_qeapp_simulator.py")
qen = sim_gate.qen
qesim = sim_gate.qesim


def build_fixtures(fixdir):
    fixdir.mkdir(parents=True, exist_ok=True)
    hello = sim_gate.build_module_from_c(TEMPLATE / "src" / "main.c", "x86_64",
                                         abi=5)
    crash = sim_gate.build_module_from_c(FIXTURES / "crash.c", "x86_64")
    hang = sim_gate.build_module_from_c(FIXTURES / "hang.c", "x86_64")
    perms = ["ui.display"]
    for name, module, ident in (
            ("hello.qeapp", hello, "org.vqeaf.guard-hello"),
            ("crash.qeapp", crash, "org.vqeaf.guard-crash"),
            ("hang.qeapp", hang, "org.vqeaf.guard-hang")):
        abi = 5 if name == "hello.qeapp" else 2
        sim_gate.assemble(fixdir / name,
                          sim_gate.manifest("x86_64", perms, ident, name, abi=abi),
                          module)
    parts = sim_gate.split_module(hello)
    big = qen.build_module(parts["code"], parts["rodata"], parts["data"],
                           bss_size=3 * 1024 * 1024, exports=parts["exports"],
                           arch="x86_64", min_api=2, entry_offset=parts["entry"])
    errors = qen.validate_module(big, "x86_64")
    if errors:
        raise SystemExit("bigarena module invalid: %s" % errors)
    sim_gate.assemble(fixdir / "bigarena.qeapp",
                      sim_gate.manifest("x86_64", perms, "org.vqeaf.guard-big",
                                        "Big Arena"), big)


def main():
    try:
        qesim.find_zig()
    except ValueError as exc:
        raise SystemExit("test_qeapp_runtime_guard: %s" % exc)
    OUT.mkdir(parents=True, exist_ok=True)
    fixdir = OUT / "fixtures"
    build_fixtures(fixdir)
    runner = qesim.build_simulator(OUT)
    guard = qesim.build(OUT, GUARD_TEST, "vqeaf-runtime-guard-test.exe",
                        extra_sources=[RUNTIME_C])
    import subprocess
    proc = subprocess.run([str(guard), str(runner), str(fixdir)],
                          capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    rc = proc.returncode
    print("test_qeapp_runtime_guard: %s" % ("ALL OK" if rc == 0 else "FAILED"))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())