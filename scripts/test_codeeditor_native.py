#!/usr/bin/env python3
"""Build and run the CodeEditor native module in the QEAPP host simulator.

Proves, on the build machine:
  * the arm64 module is a real compiled .qen (not the empty stub) and
    passes structural validation;
  * the x86_64 copy builds and passes the host simulator lifecycle with
    zero outstanding allocations;
  * a seeded source file is actually read and rendered (no fabricated data);
  * an empty storage folder renders "NO SOURCE FILES" instead.
"""
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "examples" / "codeeditor-qeapp"
SDK = ROOT / "sdk" / "cli" / "qeaf.py"


def load_sdk():
    spec = importlib.util.spec_from_file_location("qeaf_cli", SDK)
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    return sdk


def load_compiler():
    spec = importlib.util.spec_from_file_location(
        "qeaf_compiler", ROOT / "sdk" / "compiler" / "qecompile.py")
    compiler = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compiler)
    return compiler


def load_qesim():
    spec = importlib.util.spec_from_file_location(
        "qeaf_qesim", ROOT / "sdk" / "simulator" / "qesim.py")
    qesim = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(qesim)
    return qesim


def make_x86_project(temp):
    project = Path(temp) / "codeeditor-x86"
    shutil.copytree(PROJECT, project, ignore=shutil.ignore_patterns("build", "__pycache__"))
    manifest_path = project / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["architecture"] = ["x86_64"]
    manifest["entry"] = "native/x86_64/app.qen"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return project


def run(cmd, **kw):
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", **kw)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
    return proc


def main():
    sdk = load_sdk()
    compiler = load_compiler()
    qesim = load_qesim()

    # 1. arm64 structural proof.
    manifest = json.loads((PROJECT / "manifest.json").read_text(encoding="utf-8"))
    errors = sdk.validate_manifest(manifest)
    if errors:
        raise SystemExit("arm64 manifest invalid: " + "; ".join(errors))
    module_path = PROJECT / manifest["entry"]
    module = module_path.read_bytes()
    if len(module) <= 64:
        raise SystemExit("arm64 module is still the empty stub")
    errors = compiler.load_qen().validate_module(module, manifest["architecture"][0])
    if errors:
        raise SystemExit("arm64 module invalid: " + "; ".join(errors))
    imports, exports = compiler.load_qen().read_tables(module)
    if imports != ["qeaf_app_api_v2"]:
        raise SystemExit("arm64 module imports unexpected: %r" % (imports,))
    names = {name for name, _ in exports}
    required = {"create", "start", "pause", "resume", "stop", "destroy", "on_input"}
    if not required <= names:
        raise SystemExit("arm64 module missing exports: " +
                         ", ".join(sorted(required - names)))
    print(f"arm64 module: {len(module)} bytes, {len(names)} exports, OK")

    # 2. x86_64 package build.
    with tempfile.TemporaryDirectory(prefix="codeeditor-native-") as temp:
        project = make_x86_project(temp)
        package = compiler.compile_project(project, "release", sdk)
        errors = sdk.validate_package(package)
        if errors:
            raise SystemExit("x86_64 package invalid: " + "; ".join(errors))
        print(f"x86_64 package: {package} ({package.stat().st_size} bytes)")

        # 3. Build the custom simulator driver.
        driver = qesim.build(
            Path(temp) / "sim",
            PROJECT / "tests" / "sim_driver.c",
            "codeeditor-sim.exe",
        )
        print(f"simulator: {driver}")

        # 4. Run with a seeded real file.
        proc = run([str(driver), str(package)])
        sys.stdout.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        if proc.returncode != 0:
            raise SystemExit("simulator run with seeded file failed")
        out = proc.stdout
        if "state: destroyed" not in out:
            raise SystemExit("simulator did not reach destroyed state")
        if "input delivered: 2" not in out:
            raise SystemExit("expected 2 input events delivered")
        if "outstanding allocs: 0" not in out:
            raise SystemExit("module leaked allocations")
        if "seeded by simulator" not in out:
            raise SystemExit("seeded file content was not rendered")
        if "MAIN.C" not in out:
            raise SystemExit("seeded file name was not rendered")
        if "framebuffer:" not in out or "hash=0x00000000" in out:
            raise SystemExit("framebuffer hash is zero")
        # lifecycle order
        if "lifecycle: create start pause resume stop destroy" not in out:
            raise SystemExit("unexpected lifecycle trace: " +
                             [l for l in out.splitlines() if l.startswith("lifecycle:")][0])

        # 5. Run the same driver without a seeded file: the list screen must report
        #    an empty folder rather than fabricating a file.
        env = dict(os.environ)
        env["CODEEDITOR_NO_SEED"] = "1"
        proc2 = run([str(driver), str(package)], env=env)
        sys.stdout.write(proc2.stdout)
        sys.stderr.write(proc2.stderr)
        if proc2.returncode != 0:
            raise SystemExit("simulator run without seeded file failed")
        if "NO SOURCE FILES" not in proc2.stdout:
            raise SystemExit("empty storage did not render 'NO SOURCE FILES'")
        if "seeded by simulator" in proc2.stdout or "MAIN.C" in proc2.stdout:
            raise SystemExit("empty run fabricated file data")

        # 6. Run the stock CLI simulator on the same package (no seeded file,
        #    no input): lifecycle proof only.
        cli = run([sys.executable, str(SDK), "simulate", str(package)])
        sys.stdout.write(cli.stdout)
        sys.stderr.write(cli.stderr)
        if cli.returncode != 0:
            raise SystemExit("qeaf simulate (CLI) failed")
        if "state: destroyed" not in cli.stdout:
            raise SystemExit("CLI simulator did not reach destroyed state")
        if "SCANNED 0 FILES" not in cli.stdout:
            raise SystemExit("CLI run did not report an empty scan")
        if "seeded by simulator" in cli.stdout:
            raise SystemExit("CLI run fabricated seeded data")

        print("codeeditor native: OK")


if __name__ == "__main__":
    raise SystemExit(main())
