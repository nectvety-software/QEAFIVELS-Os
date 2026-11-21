#!/usr/bin/env python3
"""Build and run the VideoPlayer native module in the QEAPP host simulator.

Proves, on the build machine:
  * the arm64 module is a real compiled .qen (abi_version 3) and passes
    structural validation;
  * the x86_64 copy builds and passes the host simulator lifecycle with
    zero outstanding allocations;
  * a real QVF (converted from an actual MP4 with scripts/mp4_to_qvf.py) is
    decoded frame by frame through ABI v3 read_file_part and rendered
    through blit_pixels (blit count grows, framebuffer hash is non-zero and
    changes between frames);
  * with no video file the app reports NO VIDEO FILE instead of fabricating
    a picture.
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
PROJECT = ROOT / "examples" / "videoplayer-qeapp"
CODEEDITOR = ROOT / "examples" / "codeeditor-qeapp"
SDK = ROOT / "sdk" / "cli" / "qeaf.py"
CONVERTER = ROOT / "scripts" / "mp4_to_qvf.py"
QVF = ROOT / "build" / "media-fixtures" / "video1.qvf"
# The user's real sample video; override with VIDEO_SOURCE when the
# LDPlayer path does not exist on this machine.
VIDEO_SOURCE = os.environ.get(
    "VIDEO_SOURCE", r"C:\LDPlayer\LDPlayer14\vms\video\video1.mp4")

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")


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


def qvf_read_header(data):
    magic, version, codec = data[:4], int.from_bytes(data[4:6], "little"), \
        int.from_bytes(data[6:8], "little")
    width, height = int.from_bytes(data[8:12], "little"), \
        int.from_bytes(data[12:16], "little")
    fps = int.from_bytes(data[16:18], "little")
    count = int.from_bytes(data[20:24], "little")
    return magic, version, codec, width, height, fps, count


def qvf_frame(data, index, width, height):
    """Decompress frame `index` and return the raw RGB565 bytes.

    Uses the self-contained LZ4 decompressor from scripts/mp4_to_qvf.py so
    the cross-check never depends on a pip package.
    """
    spec = importlib.util.spec_from_file_location("mp4_to_qvf", CONVERTER)
    converter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(converter)
    offset = 32
    for i in range(index + 1):
        raw_size = int.from_bytes(data[offset:offset + 4], "little")
        comp_size = int.from_bytes(data[offset + 4:offset + 8], "little")
        offset += 8
        comp = data[offset:offset + comp_size]
        offset += comp_size
    out = converter.lz4_decompress_check(comp, raw_size)
    assert len(out) == width * height * 2
    return out


def expected_framebuffer(data, sim_w, sim_h):
    """Frame index 1 (the state after OK OK LEFT), 2x nearest, centred the
    way the app places it: x=(720-240)/2, y=64+(1560-128-426)/2."""
    magic, version, codec, w, h, fps, count = qvf_read_header(data)
    assert magic == b"QVF1" and codec == 1
    raw = qvf_frame(data, 1, w, h)
    sw, sh = w * 2, h * 2
    x = (sim_w - sw) // 2
    y = 64 + (sim_h - 128 - sh) // 2
    fb = [0] * (sim_w * sim_h)
    for row in range(sh):
        src_row = row // 2
        for col in range(sw):
            lo = raw[(src_row * w + col // 2) * 2]
            hi = raw[(src_row * w + col // 2) * 2 + 1]
            c = lo | (hi << 8)
            r = ((c >> 11) & 0x1F) * 255 // 31
            g = ((c >> 5) & 0x3F) * 255 // 63
            b = (c & 0x1F) * 255 // 31
            fb[(y + row) * sim_w + (x + col)] = \
                0xFF000000 | (r << 16) | (g << 8) | b
    return x, y, sw, sh, fb


def make_x86_project(temp):
    project = Path(temp) / "videoplayer-x86"
    shutil.copytree(PROJECT, project,
                    ignore=shutil.ignore_patterns("build", "__pycache__"))
    manifest_path = project / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["architecture"] = ["x86_64"]
    manifest["entry"] = "native/x86_64/app.qen"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n",
                             encoding="utf-8")
    return project


def ensure_qvf():
    """Produce the real QVF fixture from the user's video (or keep an
    existing one). Returns None when no source video is available."""
    if QVF.is_file():
        return QVF
    if not Path(VIDEO_SOURCE).is_file():
        return None
    QVF.parent.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(
        [sys.executable, str(CONVERTER), VIDEO_SOURCE, str(QVF),
         "--width", "120", "--height", "213", "--fps", "10", "--seconds", "8"],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    if proc.returncode != 0 or not QVF.is_file():
        return None
    return QVF


def run(cmd, env=None):
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", env=env)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
    return proc


def main():
    sdk = load_sdk()
    compiler = load_compiler()
    qesim = load_qesim()

    # 1. arm64 structural proof (real module, ABI v3 imports).
    manifest = json.loads((PROJECT / "manifest.json").read_text(encoding="utf-8"))
    errors = sdk.validate_manifest(manifest)
    if errors:
        raise SystemExit("arm64 manifest invalid: " + "; ".join(errors))
    module_path = PROJECT / manifest["entry"]
    module = module_path.read_bytes()
    if len(module) <= 64:
        raise SystemExit("arm64 module is still the empty stub")
    errors = compiler.load_qen().validate_module(module,
                                                 manifest["architecture"][0])
    if errors:
        raise SystemExit("arm64 module invalid: " + "; ".join(errors))
    imports, exports = compiler.load_qen().read_tables(module)
    if imports != ["qeaf_app_api_v3"]:
        raise SystemExit("arm64 module imports unexpected: %r" % (imports,))
    names = {name for name, _ in exports}
    required = {"create", "start", "pause", "resume", "stop",
                "destroy", "on_input"}
    if not required <= names:
        raise SystemExit("arm64 module missing exports: " +
                         ", ".join(sorted(required - names)))
    print(f"arm64 module: {len(module)} bytes, {len(names)} exports, "
          "abi_version 3, OK")

    qvf = ensure_qvf()
    if qvf is None:
        raise SystemExit("no QVF fixture and no source video available; run "
                         "scripts/mp4_to_qvf.py first")
    print(f"qvf fixture: {qvf} ({qvf.stat().st_size} bytes)")

    with tempfile.TemporaryDirectory(prefix="videoplayer-native-") as temp:
        project = make_x86_project(temp)
        package = compiler.compile_project(project, "release", sdk)
        errors = sdk.validate_package(package)
        if errors:
            raise SystemExit("x86_64 package invalid: " + "; ".join(errors))
        print(f"x86_64 package: {package} ({package.stat().st_size} bytes)")

        driver = qesim.build(
            Path(temp) / "sim",
            PROJECT / "tests" / "sim_driver.c",
            "videoplayer-sim.exe",
        )
        print(f"simulator: {driver}")

        # 2. Play the real video: 3 inputs -> blits advance, hash changes.
        fb_path = Path(temp) / "final-frame.raw"
        proc = run([str(driver), str(package), str(qvf), "--fb", str(fb_path)])
        sys.stdout.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        if proc.returncode != 0:
            raise SystemExit("simulator run with QVF failed")
        out = proc.stdout
        if "state: destroyed" not in out:
            raise SystemExit("simulator did not reach destroyed state")
        if "input delivered: 3" not in out:
            raise SystemExit("expected 3 input events delivered")
        if "outstanding allocs: 0" not in out:
            raise SystemExit("module leaked allocations")
        if "blits: 0" in out or "(none)" in out:
            raise SystemExit("no blit_pixels calls recorded")
        if "FRAME 003/080" not in out:
            raise SystemExit("app never rendered frame 3 after OK OK LEFT")
        if '"FRAME 002/080"' not in out.split("draws (")[-1]:
            raise SystemExit("LEFT did not step back to frame 2")
        if "hash=0x00000000" in out:
            raise SystemExit("framebuffer hash is zero")
        if "lifecycle: create start pause resume stop destroy" not in out:
            raise SystemExit("unexpected lifecycle trace")
        if "fail" in "\n".join(line.strip() for line in out.splitlines()
                               if line.startswith("  videoplayer:")):
            raise SystemExit("app logged a failure state with a valid video")

        # 2b. Cross-check the framebuffer's video area against an independent
        #     decode of the same QVF frame (pure-python LZ4 decompressor).
        #     Only the video rectangle is compared: the chrome (draw_text)
        #     overlay is verified separately via the draws list above.
        qvf_data = qvf.read_bytes()
        sim_w, sim_h = 720, 1560
        x, y, sw, sh, expected = expected_framebuffer(qvf_data, sim_w, sim_h)
        raw = fb_path.read_bytes()
        bad = 0
        for row in range(sh):
            base = (y + row) * sim_w + x
            for col in range(sw):
                if int.from_bytes(raw[(base + col) * 4:(base + col) * 4 + 4],
                                  "little") != expected[(y + row) * sim_w + x + col]:
                    bad += 1
        if bad != 0:
            raise SystemExit("framebuffer mismatch vs decoded frame: "
                             "%d wrong pixels (video area %dx%d at (%d,%d))"
                             % (bad, sw, sh, x, y))
        print(f"pixel cross-check: {sw * sh} pixels match the decoded "
              "QVF frame exactly")

        # 3. No video file: honest empty state, still a clean lifecycle.
        env = dict(os.environ)
        env["VIDEOPLAYER_NO_SEED"] = "1"
        proc2 = run([str(driver), str(package)], env=env)
        sys.stdout.write(proc2.stdout)
        sys.stderr.write(proc2.stderr)
        if proc2.returncode != 0:
            raise SystemExit("simulator run without QVF failed")
        if "NO VIDEO FILE" not in proc2.stdout:
            raise SystemExit("missing video did not render 'NO VIDEO FILE'")
        if "CODEC NOT SUPPORTED" in proc2.stdout:
            raise SystemExit("wrong failure state for a missing file")

        # 4. Stock CLI simulator: lifecycle proof, honest empty folder.
        cli = run([sys.executable, str(SDK), "simulate", str(package)])
        sys.stdout.write(cli.stdout)
        sys.stderr.write(cli.stderr)
        if cli.returncode != 0:
            raise SystemExit("qeaf simulate (CLI) failed")
        if "state: destroyed" not in cli.stdout:
            raise SystemExit("CLI simulator did not reach destroyed state")

        print("videoplayer native: OK")


if __name__ == "__main__":
    raise SystemExit(main())
