#!/usr/bin/env python3
"""Generate multimedia fixtures and execute the media inspection core test.

Fixtures are real, parseable files (a real 512x512 PNG plus hand-built BMP, GIF,
JPEG, WAV, MP3, MP4, PDF, ZIP and text). The C test compiles
platform/violet/ui/vqeaf_media.c against them and asserts the parsed metadata.
"""
import os
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_phone_ui as r  # noqa: E402

ROOT = r.ROOT
OUT = os.path.join(ROOT, "build", "media-fixtures")
TEST = os.path.join(ROOT, "tests", "vqeaf_media_test.c")
MEDIA = os.path.join(ROOT, "platform", "violet", "ui", "vqeaf_media.c")
EXE = os.path.join(OUT, "vqeaf-media-test.exe")


def synchsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def box(kind, body):
    return struct.pack(">I", 8 + len(body)) + kind + body


def gen_fixtures():
    os.makedirs(OUT, exist_ok=True)
    shutil.copyfile(os.path.join(ROOT, "sdk", "templates", "hello-qeapp",
                                 "assets", "icon.png"),
                    os.path.join(OUT, "sample.png"))

    # BMP 10x8 24-bit
    stride = ((10 * 3 + 3) // 4) * 4
    pixels = b"\x40\x80\xC0" * 10
    pixels += b"\x00" * (stride - 30)
    pixels *= 8
    bmp = b"BM" + struct.pack("<IHHI", 14 + 40 + len(pixels), 0, 0, 54)
    bmp += struct.pack("<IiiHHIIiiII", 40, 10, 8, 1, 24, 0, len(pixels),
                       2835, 2835, 0, 0)
    bmp += pixels
    open(os.path.join(OUT, "sample.bmp"), "wb").write(bmp)

    # GIF 4x4, two frames, no colour tables
    def frame():
        return (b"\x2C" + struct.pack("<HHHH", 0, 0, 4, 4) + b"\x00"
                + b"\x02" + b"\x02\x84\x00" + b"\x00")
    gif = b"GIF89a" + struct.pack("<HH", 4, 4) + b"\x00\x00\x00"
    gif += frame() + frame() + b"\x3B"
    open(os.path.join(OUT, "sample.gif"), "wb").write(gif)

    # JPEG with SOF0: 16x12
    jpg = b"\xFF\xD8\xFF\xC0" + struct.pack(">HBHH", 11, 8, 12, 16)
    jpg += b"\x01\x01\x11\x00\xFF\xD9"
    open(os.path.join(OUT, "sample.jpg"), "wb").write(jpg)

    # WAV 8kHz mono 16-bit, 1 second
    data = b"\x00\x00" * 8000
    fmt = struct.pack("<HHIIHH", 1, 1, 8000, 16000, 2, 16)
    wav = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    wav += b"fmt " + struct.pack("<I", len(fmt)) + fmt
    wav += b"data" + struct.pack("<I", len(data)) + data
    open(os.path.join(OUT, "sample.wav"), "wb").write(wav)

    # MP3: ID3v2.3 tag + 128kbps 44.1kHz frame sync
    def frame(fid, text):
        body = b"\x00" + text.encode("latin1")
        return fid + struct.pack(">I", len(body)) + b"\x00\x00" + body
    tag = frame(b"TIT2", "Test Title") + frame(b"TPE1", "Test Artist")
    tag += frame(b"TALB", "Test Album")
    mp3 = b"ID3\x03\x00\x00" + synchsafe(len(tag)) + tag
    mp3 += b"\xFF\xFB\x90\x00" + b"\x00" * 200
    open(os.path.join(OUT, "sample.mp3"), "wb").write(mp3)

    # MP4: ftyp isom + moov(mvhd 2s + trak tkhd 320x240)
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 0) + b"isom")
    mvhd = box(b"mvhd", struct.pack(">I", 0) + struct.pack(">II", 0, 0)
               + struct.pack(">II", 1000, 2000) + b"\x00" * 80)
    tkhd = box(b"tkhd", struct.pack(">I", 0) + struct.pack(">II", 0, 0)
               + struct.pack(">I", 1) + struct.pack(">I", 0)
               + struct.pack(">I", 2000) + b"\x00" * 8
               + struct.pack(">hhhh", 0, 0, 0, 0) + b"\x00" * 36
               + struct.pack(">II", 320 << 16, 240 << 16))
    trak = box(b"trak", tkhd)
    moov = box(b"moov", mvhd + trak)
    open(os.path.join(OUT, "sample.mp4"), "wb").write(ftyp + moov)

    # PDF: 3 pages (plus a /Pages node that must not be counted)
    pdf = b"%PDF-1.4\n"
    pdf += b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
    pdf += b"2 0 obj\n<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>\nendobj\n"
    for i in range(3, 6):
        pdf += ("%d 0 obj\n<< /Type /Page /Parent 2 0 R >>\nendobj\n" % i).encode()
    pdf += b"trailer\n<< /Root 1 0 R >>\n%%EOF\n"
    open(os.path.join(OUT, "sample.pdf"), "wb").write(pdf)

    open(os.path.join(OUT, "sample.txt"), "wb").write(b"Hello media\nDong hai\n")
    with zipfile.ZipFile(os.path.join(OUT, "sample.zip"), "w",
                         zipfile.ZIP_STORED) as z:
        z.writestr("a.txt", "hello")


def compile_and_run():
    objects = []
    flags = r.CFLAGS + r.INCLUDES
    for src in (MEDIA, TEST):
        obj = os.path.join(OUT, os.path.basename(src).replace(".", "_") + ".o")
        cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu"] + flags + \
              ["-c", src, "-o", obj]
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        if proc.returncode != 0:
            print(proc.stderr or proc.stdout, file=sys.stderr)
            raise SystemExit("test_vqeaf_media: compile FAILED %s" % src)
        objects.append(obj)
    cmd = [r.ZIG, "cc", "-target", "x86_64-windows-gnu", "-o", EXE] + objects
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(proc.stderr or proc.stdout, file=sys.stderr)
        raise SystemExit("test_vqeaf_media: link FAILED")
    proc = subprocess.run([EXE, OUT], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    return proc.returncode


def main():
    if not os.path.isfile(r.ZIG):
        raise SystemExit("test_vqeaf_media: zig not found at %s" % r.ZIG)
    gen_fixtures()
    rc = compile_and_run()
    if rc == 0:
        print("test_vqeaf_media: ALL OK")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())