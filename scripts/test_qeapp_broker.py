#!/usr/bin/env python3
"""Phase 5 gate: permission broker + quotas (memory/CPU/path scope) + per-call
timeout for native QEAPP calls.

Builds two deterministic C gates against the real simulator core via the shared
SDK build helpers:
  * tests/vqeaf_broker_test.c      - pure broker unit decisions + audit trail
  * tests/vqeaf_sim_quota_test.c   - broker/quota wiring through real modules
and signs .qeapp v2 fixtures (hello/probe/probe-noperm/slow/badpath/escalate).
Then runs CLI acceptance with the quota flags (--scope/--mem-bytes/--call-ms).

Host x86_64 only; device execution is a Phase 4 gate and is never claimed here.
The launcher wiring stays deferred until the independent OS has a process model.

Output: build/qeapp-broker/
"""
import importlib.util
import subprocess
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "qeapp-broker"
FIXTURES = ROOT / "tests" / "fixtures" / "qeapp-sim"
TEMPLATE = ROOT / "sdk" / "templates" / "hello-qeapp-native"
BROKER_C = ROOT / "tests" / "vqeaf_broker_test.c"
QUOTA_C = ROOT / "tests" / "vqeaf_sim_quota_test.c"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Reuse the Phase 3 fixture helpers so both gates sign packages identically.
sim_gate = load("sim_gate", ROOT / "scripts" / "test_qeapp_simulator.py")
qesim = sim_gate.qesim
qeaf = sim_gate.qeaf
manifest = sim_gate.manifest
build_module_from_c = sim_gate.build_module_from_c
assemble = sim_gate.assemble


def build_fixtures(fixdir):
    fixdir.mkdir(parents=True, exist_ok=True)
    hello = build_module_from_c(TEMPLATE / "src" / "main.c", "x86_64", abi=5)
    probe = build_module_from_c(FIXTURES / "probe.c", "x86_64")
    slow = build_module_from_c(FIXTURES / "slow.c", "x86_64")
    badpath = build_module_from_c(FIXTURES / "badpath.c", "x86_64")
    escalate = build_module_from_c(FIXTURES / "escalate.c", "x86_64")
    real = {
        "hello.qeapp": (manifest("x86_64", ["ui.display"],
                                 "org.vqeaf.broker-hello", "B Hello", abi=5), hello),
        "probe.qeapp": (manifest("x86_64", ["ui.display", "storage.read", "input.receive"],
                                 "org.vqeaf.broker-probe", "B Probe"), probe),
        "probe-noperm.qeapp": (manifest("x86_64", ["ui.display"],
                                        "org.vqeaf.broker-probe-noperm", "B Probe NoPerm"), probe),
        "slow.qeapp": (manifest("x86_64", ["ui.display"],
                                "org.vqeaf.broker-slow", "B Slow"), slow),
        "badpath.qeapp": (manifest("x86_64", ["ui.display", "storage.read"],
                                   "org.vqeaf.broker-badpath", "B BadPath"), badpath),
        "escalate.qeapp": (manifest("x86_64", ["ui.display"],
                                    "org.vqeaf.broker-escalate", "B Escalate"), escalate),
    }
    for name, (mani, module) in real.items():
        assemble(fixdir / name, mani, module)
        errors = qeaf.validate_package(fixdir / name)
        if errors:
            raise SystemExit("fixture %s invalid: %s" % (name, errors))
    return sorted(p.name for p in fixdir.glob("*.qeapp"))


def run(exe, args, label):
    proc = subprocess.run([str(exe)] + [str(a) for a in args],
                          capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print("== %s ==" % label)
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    return proc.returncode, proc.stdout, proc.stderr


def main():
    try:
        qesim.find_zig()
    except ValueError as exc:
        raise SystemExit("test_qeapp_broker: %s" % exc)
    OUT.mkdir(parents=True, exist_ok=True)
    fixdir = OUT / "fixtures"
    names = build_fixtures(fixdir)
    print("fixtures: %s" % ", ".join(names))

    broker_exe = qesim.build(OUT, BROKER_C, "vqeaf-broker-test.exe")
    quota_exe = qesim.build(OUT, QUOTA_C, "vqeaf-sim-quota-test.exe")
    cli_exe = qesim.build_simulator(OUT)

    rc = 0
    b_rc, _, _ = run(broker_exe, [], "broker unit gate")
    rc |= b_rc
    q_rc, _, _ = run(quota_exe, [fixdir], "simulator quota integration gate")
    rc |= q_rc

    # CLI acceptance with quota flags (no seeded files needed for these).
    h_rc, h_out, _ = run(cli_exe, [fixdir / "hello.qeapp", "--call-ms", "1000"],
                         "CLI: hello within per-call budget")
    hello_ok = (h_rc == 0
                and "lifecycle: create start pause resume stop destroy" in h_out
                and "call timeout: 0" in h_out)
    print("acceptance (hello within budget): %s" % ("PASS" if hello_ok else "FAIL"))
    rc |= 0 if hello_ok else 1

    bp_rc, bp_out, _ = run(cli_exe, [fixdir / "badpath.qeapp"],
                           "CLI: unsafe paths denied + audited")
    badpath_ok = (bp_rc == 0 and "denied decisions: 4" in bp_out
                  and "traversal" in bp_out and "absolute" in bp_out
                  and "drive" in bp_out and "backslash" in bp_out)
    print("acceptance (unsafe paths audited): %s" % ("PASS" if badpath_ok else "FAIL"))
    rc |= 0 if badpath_ok else 1

    es_rc, es_out, _ = run(cli_exe, [fixdir / "escalate.qeapp"],
                           "CLI: escalation denied + audited")
    escalate_ok = (es_rc == 0 and "denied decisions: 3" in es_out
                   and "undeclared" in es_out)
    print("acceptance (escalation audited): %s" % ("PASS" if escalate_ok else "FAIL"))
    rc |= 0 if escalate_ok else 1

    sl_rc, sl_out, sl_err = run(cli_exe, [fixdir / "slow.qeapp", "--call-ms", "5"],
                                "CLI: slow call exceeds 5 ms budget")
    slow_ok = (sl_rc != 0 and "call timeout: 1" in sl_out
               and "per-call CPU budget" in sl_err)
    print("acceptance (per-call timeout enforced): %s" % ("PASS" if slow_ok else "FAIL"))
    rc |= 0 if slow_ok else 1

    print("test_qeapp_broker: %s" % ("ALL OK" if rc == 0 else "FAILED"))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())