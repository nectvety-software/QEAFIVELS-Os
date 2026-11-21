#!/usr/bin/env python3
"""Gate for examples/puzzle-qeapp: build, validate, run in the simulator.

Checks that the sample is a real, playable QEAPP rather than a screenshot:
  - the SDK builds and validates the package,
  - the simulator loads it, runs the lifecycle and reports the board draw,
  - the CLI's `--keys` path delivers QEAF_INPUT_KEY events to on_input(),
  - arrow keys actually change the board (framebuffer hash) and the move
    counter text, and nothing is denied.
"""
import importlib.util
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "examples" / "puzzle-qeapp"
PACKAGE = PROJECT / "build" / "releases" / "puzzle-qeapp.qeapp"
OUT = ROOT / "build" / "puzzle-qeapp"
CLI = ROOT / "sdk" / "cli" / "qeaf.py"
SIM = ROOT / "build" / "qeapp-simulator" / "qeaf-sim.exe"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(cmd, title):
    proc = subprocess.run([str(x) for x in cmd], capture_output=True, text=True,
                          encoding="utf-8", errors="replace", cwd=str(ROOT))
    text = (proc.stdout or "") + (proc.stderr or "")
    if proc.returncode != 0:
        raise SystemExit("%s failed (rc=%d):\n%s" % (title, proc.returncode, text))
    return text


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    run([sys.executable, CLI, "build", PROJECT, "--config", "release"],
        "qeaf build")
    run([sys.executable, CLI, "validate", PACKAGE], "qeaf validate")
    if not SIM.is_file():
        qesim = load("qesim", ROOT / "sdk" / "simulator" / "qesim.py")
        qesim.build_simulator(ROOT / "build" / "qeapp-simulator")

    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("ok   " if cond else "FAIL ") + msg)
        ok = ok and cond

    start = run([SIM, PACKAGE, "--device", "violet"], "sim start")
    check("lifecycle: create start pause resume stop destroy" in start,
          "lifecycle completed")
    check("Xep Hinh: board ready" in start, "board drawn at start")
    check('(48,70) "XEP HINH 4x4"' in start, "title drawn on the canvas")
    check('(168,1114) "0"' in start, "move counter starts at 0")
    check("input delivered: 0" in start, "no input delivered without --keys")
    check("denied decisions: 0" in start, "no permission denials")
    check("outstanding allocs: 0" in start, "no leaked allocations")
    start_hash = next((line.split("hash=")[1].strip()
                       for line in start.splitlines()
                       if "framebuffer:" in line), "")

    keys = "105,103,106,108"   # left, up, right, down
    moved = run([SIM, PACKAGE, "--device", "violet", "--keys", keys],
                "sim keys")
    check("input delivered: 4" in moved, "CLI --keys delivered 4 key events")
    check('(168,1114) "3"' in moved, "move counter advanced to 3 after keys")
    check("denied decisions: 0" in moved, "input path used no denied decision")
    moved_hash = next((line.split("hash=")[1].strip()
                       for line in moved.splitlines()
                       if "framebuffer:" in line), "")
    check(start_hash != "" and moved_hash != "" and start_hash != moved_hash,
          "arrow keys changed the framebuffer (%s -> %s)"
          % (start_hash, moved_hash))

    print("test_puzzle_qeapp: %s" % ("ALL OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
