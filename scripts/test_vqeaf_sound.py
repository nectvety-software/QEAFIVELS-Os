#!/usr/bin/env python3
"""Build and run the host-only QEAF sound service contract tests.

The executable links only vqeaf_sound.c, its generated sample data, and the
host test harness. The retained WAV assets are validated independently, then
compared byte-for-byte with samples exported by the compiled C descriptors.
No display, device, network access, or external downloads are required.
"""
import glob
import hashlib
import os
import struct
import subprocess
import sys
import wave

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEAF = os.environ.get("QEAFDROPOS") or "D:\\QeafivelsOfficial\\QeafdropOs"
ZIG = os.environ.get("ZIG") or os.path.join(
    QEAF, "out", "toolchain", "zig-windows-x86_64-0.13.0", "zig.exe")
UI = os.path.join(ROOT, "platform", "violet", "ui")
OUTDIR = os.path.join(ROOT, "build", "ui-host")
EXE = os.path.join(OUTDIR, "vqeaf-sound-test.exe")
TEST = os.path.join(ROOT, "tests", "vqeaf_sound.c")
SOUND = os.path.join(UI, "vqeaf_sound.c")
SOUND_DATA = os.path.join(UI, "sounds", "vqeaf_sound_data.c")
ASSET_DIR = os.path.join(ROOT, "assets", "sounds")
EVENT_NAMES = (
    "tap", "drop", "navigate", "notify", "capture", "record", "warning",
    "charge",
)
DUMP_MAGIC = b"VQEAF_SOUND_CLIPS_V1\0"

INCLUDES = [
    "-I", UI,
    "-I", os.path.join(ROOT, "platform", "violet"),
    "-I", os.path.join(ROOT, "platform", "lvgl"),
    "-I", os.path.join(ROOT, "platform", "lvgl", "src"),
]
CFLAGS = [
    "-O1", "-g", "-Wall", "-Wextra", "-std=c11",
    "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_LVGL_H_INCLUDE_SIMPLE",
]


class TestFailure(Exception):
    pass


def run_command(command, binary=False):
    """Run argv directly, print the exact command and observed exit code."""
    print("command: " + subprocess.list2cmdline([str(arg) for arg in command]),
          flush=True)
    kwargs = {
        "stdout": subprocess.PIPE,
        "stderr": subprocess.PIPE,
        "shell": False,
    }
    if binary:
        proc = subprocess.run(command, **kwargs)
    else:
        proc = subprocess.run(command, text=True, encoding="utf-8",
                              errors="replace", **kwargs)
        if proc.stdout:
            sys.stdout.write(proc.stdout)
        if proc.stderr:
            sys.stderr.write(proc.stderr)
    if binary and proc.returncode != 0:
        sys.stderr.write((proc.stderr or b"").decode("utf-8", errors="replace"))
    print("exit code: %d" % proc.returncode, flush=True)
    return proc


def validate_assets():
    paths = glob.glob(os.path.join(ASSET_DIR, "qeaf-*.wav"))
    expected_files = sorted("qeaf-%s.wav" % name for name in EVENT_NAMES)
    actual_files = sorted(os.path.basename(path) for path in paths)
    if actual_files != expected_files:
        raise TestFailure(
            "expected exactly %s, found %s" % (expected_files, actual_files))

    assets = {}
    for name in EVENT_NAMES:
        filename = "qeaf-%s.wav" % name
        path = os.path.join(ASSET_DIR, filename)
        try:
            with open(path, "rb") as handle:
                raw = handle.read()
            with wave.open(path, "rb") as wav:
                channels = wav.getnchannels()
                sample_width = wav.getsampwidth()
                sample_rate = wav.getframerate()
                compression = wav.getcomptype()
                frames = wav.getnframes()
                pcm = wav.readframes(frames)
        except (OSError, EOFError, wave.Error) as exc:
            raise TestFailure("%s: cannot read WAV: %s" % (filename, exc))

        if channels != 1 or sample_width != 2 or compression != "NONE":
            raise TestFailure(
                "%s: expected mono uncompressed PCM16, got channels=%d "
                "width=%d compression=%s" %
                (filename, channels, sample_width, compression))
        if sample_rate != 22050:
            raise TestFailure(
                "%s: expected 22050 Hz, got %d Hz" % (filename, sample_rate))
        if frames == 0 or frames > 22050:
            raise TestFailure(
                "%s: expected 1..22050 frames, got %d" % (filename, frames))
        if len(pcm) != frames * 2:
            raise TestFailure(
                "%s: read %d PCM bytes for %d frames" %
                (filename, len(pcm), frames))
        if not any(pcm):
            raise TestFailure("%s: asset is silent" % filename)

        digest = hashlib.sha256(raw).hexdigest()
        assets[name] = {"frames": frames, "pcm": pcm, "sha256": digest}
        print("asset: %s frames=%d sha256=%s" %
              (filename, frames, digest))
    return assets


def compare_dump(data, assets):
    if not data.startswith(DUMP_MAGIC):
        raise TestFailure("generated clip dump has an invalid magic header")
    offset = len(DUMP_MAGIC)
    for event, name in enumerate(EVENT_NAMES):
        if offset + 8 > len(data):
            raise TestFailure("generated clip dump is truncated at %s" % name)
        dumped_event, count = struct.unpack_from("<II", data, offset)
        offset += 8
        asset = assets[name]
        if dumped_event != event:
            raise TestFailure(
                "%s: expected event %d, got %d" %
                (name, event, dumped_event))
        if count != asset["frames"]:
            raise TestFailure(
                "%s: C descriptor has %d frames, WAV has %d" %
                (name, count, asset["frames"]))
        end = offset + count * 2
        if end > len(data):
            raise TestFailure("generated clip dump is truncated in %s" % name)
        if data[offset:end] != asset["pcm"]:
            raise TestFailure(
                "%s: generated C samples differ from retained WAV" % name)
        offset = end
    if offset != len(data):
        raise TestFailure("generated clip dump has %d trailing bytes" %
                          (len(data) - offset))
    print("generated C clips exactly match all 8 PCM16 WAV assets")


def main():
    try:
        assets = validate_assets()
    except TestFailure as exc:
        print("test_vqeaf_sound: FAILED %s" % exc, file=sys.stderr)
        return 1

    if not os.path.isfile(ZIG):
        print("test_vqeaf_sound: zig not found at %s (set ZIG=...)" % ZIG,
              file=sys.stderr)
        return 2

    os.makedirs(OUTDIR, exist_ok=True)
    compile_command = [
        ZIG, "cc", "-target", "x86_64-windows-gnu",
    ] + CFLAGS + INCLUDES + [
        SOUND, SOUND_DATA, TEST, "-o", EXE,
    ]
    try:
        compiled = run_command(compile_command)
    except OSError as exc:
        print("test_vqeaf_sound: compile could not start: %s" % exc,
              file=sys.stderr)
        return 2
    if compiled.returncode != 0:
        print("test_vqeaf_sound: compile FAILED", file=sys.stderr)
        return compiled.returncode or 1

    try:
        tested = run_command([EXE])
    except OSError as exc:
        print("test_vqeaf_sound: test could not start: %s" % exc,
              file=sys.stderr)
        return 2
    if tested.returncode != 0:
        print("test_vqeaf_sound: C tests FAILED", file=sys.stderr)
        return tested.returncode or 1

    try:
        dumped = run_command([EXE, "--dump-clips"], binary=True)
    except OSError as exc:
        print("test_vqeaf_sound: clip dump could not start: %s" % exc,
              file=sys.stderr)
        return 2
    if dumped.returncode != 0:
        print("test_vqeaf_sound: clip dump FAILED", file=sys.stderr)
        return dumped.returncode or 1
    try:
        compare_dump(dumped.stdout, assets)
    except TestFailure as exc:
        print("test_vqeaf_sound: FAILED %s" % exc, file=sys.stderr)
        return 1

    print("test_vqeaf_sound: ALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
