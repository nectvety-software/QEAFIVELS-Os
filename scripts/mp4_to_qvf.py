#!/usr/bin/env python3
"""Convert an MP4 (or any ffmpeg-readable video) to a QVF QEAF Video File.

QVF is the video container the OS can actually decode: RGB565 frames
compressed per frame with the LZ4 block format. Full H.264 decoding inside
the OS is out of scope, so heavy source codecs are transcoded on the build
machine and the OS plays the lightweight result.

QVF layout (little-endian, docs/QEAF_VIDEO_FORMAT.md):

    0   char[4] magic "QVF1"
    4   u16     version = 1
    6   u16     codec = 1 (LZ4 block of RGB565)
    8   u32     width
    12  u32     height
    16  u16     fps
    18  u16     reserved (0)
    20  u32     frame_count
    24  u32     flags (0)
    28  u32     reserved (0)
    32  frames: [u32 raw_size][u32 comp_size][comp_size bytes] ...

The LZ4 compressor below is self-contained (no pip packages). Frames are
pulled from ffmpeg as raw rgb565le video over a pipe.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

QVF_MAGIC = b"QVF1"
QVF_VERSION = 1
QVF_CODEC_LZ4_RGB565 = 1
HEADER_SIZE = 32
FRAME_HEADER_SIZE = 8

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")


# ---- LZ4 block compressor (self-contained) ---------------------------------

def _hash4(data, i):
    value = (data[i] | (data[i + 1] << 8) | (data[i + 2] << 16)
             | (data[i + 3] << 24))
    return ((value * 2654435761) & 0xFFFFFFFF) >> 16


def _sequence(out, lit, offset, match_len, last):
    """Append one LZ4 sequence: literals then an optional match."""
    n = len(lit)
    tok = (n if n < 15 else 15) << 4
    if not last:
        m = match_len - 4
        tok |= m if m < 15 else 15
    out.append(tok)
    if n >= 15:
        r = n - 15
        while r >= 255:
            out.append(255)
            r -= 255
        out.append(r)
    out += lit
    if not last:
        out.append(offset & 0xFF)
        out.append((offset >> 8) & 0xFF)
        m = match_len - 4
        if m >= 15:
            r = m - 15
            while r >= 255:
                out.append(255)
                r -= 255
            out.append(r)


def lz4_compress(src):
    """Greedy single-pass LZ4 block compressor.

    Follows the LZ4 block constraints: matches must start at least 12 bytes
    before the end of the block and the final 5 bytes stay literals.
    """
    n = len(src)
    out = bytearray()
    if n == 0:
        out.append(0)
        return bytes(out)
    table = {}
    anchor = 0
    i = 0
    match_limit = n - 12      # matches must not start within the last 12 bytes
    end_limit = n - 5         # matches must not run into the last 5 bytes
    while i < match_limit:
        key = _hash4(src, i)
        j = table.get(key, -1)
        table[key] = i
        if (j >= 0 and i - j < 65536 and src[j:j + 4] == src[i:i + 4]):
            length = 4
            while i + length < end_limit and src[j + length] == src[i + length]:
                length += 1
            _sequence(out, src[anchor:i], i - j, length, False)
            i += length
            anchor = i
        else:
            i += 1
    _sequence(out, src[anchor:n], 0, 0, True)
    return bytes(out)


def lz4_decompress_check(src, expected):
    """Pure-python LZ4 block decompressor, used to self-check every frame."""
    dst = bytearray()
    ip = 0
    n = len(src)
    while ip < n:
        token = src[ip]
        ip += 1
        lit = token >> 4
        if lit == 15:
            while True:
                if ip >= n:
                    raise ValueError("truncated literal length")
                b = src[ip]
                ip += 1
                lit += b
                if b != 255:
                    break
        if ip + lit > n:
            raise ValueError("truncated literals")
        dst += src[ip:ip + lit]
        ip += lit
        if ip >= n:
            break
        if ip + 2 > n:
            raise ValueError("truncated match offset")
        offset = src[ip] | (src[ip + 1] << 8)
        ip += 2
        if offset == 0 or offset > len(dst):
            raise ValueError("invalid match offset")
        mlen = token & 0x0F
        if mlen == 15:
            while True:
                if ip >= n:
                    raise ValueError("truncated match length")
                b = src[ip]
                ip += 1
                mlen += b
                if b != 255:
                    break
        mlen += 4
        start = len(dst) - offset
        for k in range(mlen):
            dst.append(dst[start + k])
    if len(dst) != expected:
        raise ValueError("roundtrip size mismatch: %d != %d"
                         % (len(dst), expected))
    return bytes(dst)


# ---- ffmpeg plumbing --------------------------------------------------------

def find_ffmpeg():
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        pass
    # The platformio interpreter often bundles imageio_ffmpeg even when the
    # default python does not.
    penv = os.path.join(os.path.expanduser("~"), ".platformio", "penv")
    for name in ("Scripts", "bin"):
        candidate = os.path.join(penv, name, "python.exe")
        if os.path.isfile(candidate):
            probe = subprocess.run(
                [candidate, "-c", "import imageio_ffmpeg; "
                 "print(imageio_ffmpeg.get_ffmpeg_exe())"],
                capture_output=True, text=True)
            path = probe.stdout.strip()
            if probe.returncode == 0 and path and os.path.isfile(path):
                return path
    raise SystemExit("ERROR: ffmpeg not found (install it or `pip install "
                     "imageio-ffmpeg`)")


def frame_count_never(meta):
    return 0


def _lz4_lib_verify(comp, raw):
    """Cross-check against the reference LZ4 implementation when available."""
    try:
        import lz4.block
    except ImportError:
        return True
    out = lz4.block.decompress(comp, uncompressed_size=len(raw))
    return out == raw


def convert(input_path, output_path, width, height, fps, seconds, start,
            check=True):
    if width <= 0 or height <= 0 or not 1 <= fps <= 30:
        raise SystemExit("ERROR: invalid geometry")
    ffmpeg = find_ffmpeg()
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error",
           "-ss", str(start), "-i", input_path]
    if seconds > 0:
        cmd += ["-t", str(seconds)]
    cmd += ["-vf", "scale=%d:%d" % (width, height), "-r", str(fps),
            "-f", "rawvideo", "-pix_fmt", "rgb565le", "-"]
    raw_bytes = width * height * 2
    print("ffmpeg: %s" % " ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)

    frames = []
    comp_frames = []
    while True:
        raw = proc.stdout.read(raw_bytes)
        if not raw:
            break
        if len(raw) != raw_bytes:
            raise SystemExit("ERROR: short frame read from ffmpeg")
        comp = lz4_compress(raw)
        if check:
            lz4_decompress_check(comp, raw_bytes)
            if not _lz4_lib_verify(comp, raw):
                raise SystemExit("ERROR: frame %d failed reference-LZ4 "
                                 "verification" % len(frames))
        frames.append(raw)
        comp_frames.append(comp)
    proc.stdout.close()
    stderr = proc.stderr.read().decode("utf-8", "replace")
    proc.stderr.close()
    rc = proc.wait()
    if rc != 0:
        raise SystemExit("ERROR: ffmpeg failed (%d): %s" % (rc, stderr))
    if not frames:
        raise SystemExit("ERROR: no frames decoded (bad source or range?)")

    header = bytearray(HEADER_SIZE)
    header[0:4] = QVF_MAGIC
    struct.pack_into("<HH", header, 4, QVF_VERSION, QVF_CODEC_LZ4_RGB565)
    struct.pack_into("<II", header, 8, width, height)
    struct.pack_into("<HH", header, 16, fps, 0)
    struct.pack_into("<III", header, 20, len(frames), 0, 0)

    total_raw = 0
    total_comp = 0
    with open(output_path, "wb") as out:
        out.write(header)
        for raw, comp in zip(frames, comp_frames):
            out.write(struct.pack("<II", len(raw), len(comp)))
            out.write(comp)
            total_raw += len(raw)
            total_comp += len(comp)

    size = os.path.getsize(output_path)
    print("qvf: %s  %d frames %dx%d @%dfps, raw %d bytes -> compressed %d "
          "bytes (file %d bytes)"
          % (output_path, len(frames), width, height, fps,
             total_raw, total_comp, size))
    return output_path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--width", type=int, default=120)
    parser.add_argument("--height", type=int, default=213)
    parser.add_argument("--fps", type=int, default=10)
    parser.add_argument("--seconds", type=float, default=8.0,
                        help="clip length in seconds (0 = whole video)")
    parser.add_argument("--start", type=float, default=0.0)
    parser.add_argument("--no-check", action="store_true",
                        help="skip per-frame LZ4 roundtrip self-check")
    args = parser.parse_args(argv)
    if not os.path.isfile(args.input):
        raise SystemExit("ERROR: input video not found: %s" % args.input)
    convert(args.input, args.output, args.width, args.height, args.fps,
            args.seconds, args.start, check=not args.no_check)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
