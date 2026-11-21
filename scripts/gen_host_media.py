#!/usr/bin/env python3
"""Generate the host-only sample media header for the Files viewer test tree.

The PNG and JPEG are real, decodable 32x32 images captured once from a browser
canvas (a gradient with a white square) and embedded as base64 below so the
generator is reproducible without a browser. BMP/GIF/WAV/MP3/MP4/PDF/text are
hand-built real files. The output header is included only by the host branch of
vqeaf_files.c; the device build never sees these bytes.

Output: platform/violet/ui/vqeaf_host_media.h
"""
import base64
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mp4_to_qvf import QVF_CODEC_LZ4_RGB565, QVF_VERSION, HEADER_SIZE as QVF_HEADER_SIZE, \
    FRAME_HEADER_SIZE as QVF_FRAME_ENTRY, lz4_compress  # noqa: E402

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "platform", "violet", "ui", "vqeaf_host_media.h")

PNG_B64 = (
    "iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAES0lEQVR4AbxWUWsdRRg9u7m3f8UXn3RBBQVRUB/qgwq21dZK0LRqqpaL1oJSEQtWE7WRYCNoIQ9BtEWMSCtUKmJRUBHyoCiITz755pM7mzKe883M3slyY4IbGjjMNzvf7H7nnG8mt/zn5gf937ft8X/dudf/ec8+/8e9D/lfH3jYr+3d7384cMBfmX7EXz580F888qhfHU37cy9M+5WXHvNnX3ncL7024xfmD/m5hcP+5Jkn/In3n/THl5/yow9n/ez5I35m9Wl/8MIzft+Xz/r7vz7qd3838nf9OPK3rz3nb/nleX/j78d86XYVEJqhxtJiN9S4Ec0uzpVja8rdiEZrytH7LIf5Ng/jpP24OkRZc2OCGwIh1igUGD9TXMT1yeM4F8wTik33Y30ArE+hHFceKnWpamPBF3AechR3wBytGTvmtXsVc81JDcbKCXHYL+aG9WFQIFQO1IMIU4KxqRPHQca6jeNayt/GfmPeTAECVQg9YNVuVKBh5Y4FOFsrQm8YoxA3WlOOnlnO1vuNNX3Px9gDQG0sik09q/lBQUVpFFzcU9sIvqPYdL8xp+fyPcSUi3MqECs3FnwBWQV2ijtQDgsxdZj3fzwHfW8VYFya7xmDwKZAHX2eObaE2aNnMJp9F8cPLeLE9CJO7n8Hc3sWsHDfaSztfhtn734LK3e8iXO3zmP1pjlcvOENXL7+dVy57hQ7nUzlt0DPIVjMPmDcKmCsyM6JpXyNQN+/juc5e8Xl2EfKzQLqDvp+P3W7MafnE3ug4UddZLxhpBq9C8gVoOcQsmdlbf6PPa9ZTPK/5lrvAuhzUCF4Ds2TEs0AtECngPJ3FAiqlL2/L58NOXPFApWgAmQv1hEujjVHN+z//a7n3TlPQcaenjt+uMnOeO8SIlNTgYy7Y1AgnvmajGsWUNsI3gW9Px/ugcxz0PegAnuCcatA8typF6REVKF3CWItFQTFQhYHBYx1kd3jQFCh6P39wJa3Yd79islea/Z7oIlsJ93tvSsQ4/9AVCAyZqG1kHqAyvQuIDIVW/O/Mw89kHmeVDBVdqIAsc88n3wKMsbO4nA3uJ0owE4AZc2ZqwcMU7wJk/+mQsFfPiXCiVC8A00oBbqQIgKfl13PU/fX8W6ofnsR1c8vo1p7FdVPp1B9P4/q29OovllE9dV7qC59gOqLZVSfr6D69GNU5z9B9dFnqFYuoFq+BOTMFQumCu8BqsAeKANrKUHJXasE2bPCrmftPDKwuWIh5edxejZpZB7/GYFnvjDI85pFCO1/Lfv1Qg9ZrXWyVb+NeYdp+77OflOg4UedbsAIY6WKWWEbp3n3mZ5vBe0RUl4Wl3Xq+ui5VSrWgrEmW4uDZ7ZuLDgXS2G78/Q+5ds7BzwF19hzZOylLhUI/sOqIytVZzGZW6xnigXFgmJBscA4V2K7+6kCeyDrdlUn5F7l8/R8q1F7hKtDiCXyuLP3XwAAAP//PXlLqAAAAAZJREFUAwBjsVJMrIqVyQAAAABJRU5ErkJggg=="
)
JPG_B64 = (
    "/9j/4AAQSkZJRgABAQAAAQABAAD/4gHYSUNDX1BST0ZJTEUAAQEAAAHIAAAAAAQwAABtbnRyUkdCIFhZWiAH4AABAAEAAAAAAABhY3NwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAA9tYAAQAAAADTLQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAlkZXNjAAAA8AAAACRyWFlaAAABFAAAABRnWFlaAAABKAAAABRiWFlaAAABPAAAABR3dHB0AAABUAAAABRyVFJDAAABZAAAAChnVFJDAAABZAAAAChiVFJDAAABZAAAAChjcHJ0AAABjAAAADxtbHVjAAAAAAAAAAEAAAAMZW5VUwAAAAgAAAAcAHMAUgBHAEJYWVogAAAAAAAAb6IAADj1AAADkFhZWiAAAAAAAABimQAAt4UAABjaWFlaIAAAAAAAACSgAAAPhAAAts9YWVogAAAAAAAA9tYAAQAAAADTLXBhcmEAAAAAAAQAAAACZmYAAPKnAAANWQAAE9AAAApbAAAAAAAAAABtbHVjAAAAAAAAAAEAAAAMZW5VUwAAACAAAAAcAEcAbwBvAGcAbABlACAASQBuAGMALgAgADIAMAAxADb/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAAgACADASIAAhEBAxEB/8QAGAABAAMBAAAAAAAAAAAAAAAACQADBQb/xAAcEAACAgIDAAAAAAAAAAAAAAAABCExAQIFYrH/xAAZAQACAwEAAAAAAAAAAAAAAAAABgMFBwj/xAAkEQABAwQBBAMBAAAAAAAAAAARAQIDAAQFEgYhIkFhExRRgf/aAAwDAQACEQMRAD8ArTTqDcTTqCJp1AnYsW9v9nbuAp05DyGXJSviifRwppVBpZzqrp28EMDaffuSwhxLpJUain+Vy3zGzuIJIlfLsj9ugAA9qSavTSqBHg8851V07eGM+/chiYZZHKjU6KKLPmMkFxK9YvkRwHcAD6Uk0koWvIP3JOQfuTm337k2jAYB6v3f1I8VJlMoudWJVi00Pkkj0n5X/9k="
)


def synchsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def box(kind, body):
    return struct.pack(">I", 8 + len(body)) + kind + body


def gen_bmp():
    w = h = 16
    stride = ((w * 3 + 3) // 4) * 4
    rows = b""
    for y in range(h):
        row = b""
        for x in range(w):
            row += bytes([x * 16, y * 16, 128])
        row += b"\x00" * (stride - w * 3)
        rows += row
    bmp = b"BM" + struct.pack("<IHHI", 14 + 40 + len(rows), 0, 0, 54)
    bmp += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(rows),
                       2835, 2835, 0, 0)
    return bmp + rows


def gen_gif():
    def frame():
        return (b"\x2C" + struct.pack("<HHHH", 0, 0, 4, 4) + b"\x00"
                + b"\x02" + b"\x02\x84\x00" + b"\x00")
    return b"GIF89a" + struct.pack("<HH", 4, 4) + b"\x00\x00\x00" \
        + frame() + frame() + b"\x3B"


def gen_wav():
    data = b"\x00\x00" * 8000
    fmt = struct.pack("<HHIIHH", 1, 1, 8000, 16000, 2, 16)
    return (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
            + b"fmt " + struct.pack("<I", len(fmt)) + fmt
            + b"data" + struct.pack("<I", len(data)) + data)


def gen_mp3():
    def frame(fid, text):
        body = b"\x00" + text.encode("latin1")
        return fid + struct.pack(">I", len(body)) + b"\x00\x00" + body
    tag = frame(b"TIT2", "Test Title") + frame(b"TPE1", "Test Artist")
    tag += frame(b"TALB", "Test Album")
    return b"ID3\x03\x00\x00" + synchsafe(len(tag)) + tag \
        + b"\xFF\xFB\x90\x00" + b"\x00" * 200


def gen_mp4():
    ftyp = box(b"ftyp", b"isom" + struct.pack(">I", 0) + b"isom")
    mvhd = box(b"mvhd", struct.pack(">I", 0) + struct.pack(">II", 0, 0)
               + struct.pack(">II", 1000, 2000) + b"\x00" * 80)
    tkhd = box(b"tkhd", struct.pack(">I", 0) + struct.pack(">II", 0, 0)
               + struct.pack(">I", 1) + struct.pack(">I", 0)
               + struct.pack(">I", 2000) + b"\x00" * 8
               + struct.pack(">hhhh", 0, 0, 0, 0) + b"\x00" * 36
               + struct.pack(">II", 320 << 16, 240 << 16))
    return ftyp + box(b"moov", mvhd + box(b"trak", tkhd))


def gen_pdf():
    pdf = b"%PDF-1.4\n"
    pdf += b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
    pdf += b"2 0 obj\n<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>\nendobj\n"
    for i in range(3, 6):
        pdf += ("%d 0 obj\n<< /Type /Page /Parent 2 0 R >>\nendobj\n" % i).encode()
    return pdf + b"trailer\n<< /Root 1 0 R >>\n%%EOF\n"


def gen_qvf(width=60, height=106, fps=8, frames=8):
    """Real QVF1 clip: an animated diagonal colour sweep, LZ4-compressed.

    Built with the same compressor (scripts/mp4_to_qvf.py) the MP4 converter
    uses, so the fixture exercises the exact container the OS decodes.
    """
    out = bytearray(QVF_HEADER_SIZE)
    out[0:4] = b"QVF1"
    struct.pack_into("<HH", out, 4, QVF_VERSION, QVF_CODEC_LZ4_RGB565)
    struct.pack_into("<II", out, 8, width, height)
    struct.pack_into("<HH", out, 16, fps, 0)
    struct.pack_into("<III", out, 20, frames, 0, 0)
    for f in range(frames):
        px = bytearray()
        for y in range(height):
            for x in range(width):
                # Diagonal sweep: phase moves one band per frame.
                band = ((x + y + f * 13) // 15) % 4
                if band == 0:
                    r, g, b = 0xF8, 0x00, 0x00      # red
                elif band == 1:
                    r, g, b = 0x07, 0xE0, 0x00      # green
                elif band == 2:
                    r, g, b = 0x00, 0x00, 0xF8      # blue
                else:
                    r, g, b = 0xFF, 0xFF, 0x00      # yellow
                # Store as native rgb565le bytes.
                c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                px.append(c & 0xFF)
                px.append((c >> 8) & 0xFF)
        comp = lz4_compress(bytes(px))
        out += struct.pack("<II", len(px), len(comp))
        out += comp
    return bytes(out)


def c_array(name, data):
    lines = ["static const uint8_t %s[] = {" % name]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + ",".join("0x%02x" % b for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    media = [
        ("logo.png", base64.b64decode(PNG_B64)),
        ("photo.jpg", base64.b64decode(JPG_B64)),
        ("pattern.bmp", gen_bmp()),
        ("anim.gif", gen_gif()),
        ("tone.wav", gen_wav()),
        ("song.mp3", gen_mp3()),
        ("clip.mp4", gen_mp4()),
        ("clip.qvf", gen_qvf()),
        ("doc.pdf", gen_pdf()),
        ("note.txt", b"Xin chao VQEAF\nDay la tep van ban mau.\n"),
        ("song.lrc", ("[00:00.00]Test Title\n"
                      "[00:04.00]Đây là lời bài hát mẫu\n"
                      "[00:08.00]Dòng thứ hai của lời\n"
                      "[00:12.00]Dòng thứ ba của lời\n").encode("utf-8")),
    ]
    parts = [
        "/* SPDX-License-Identifier: MIT */",
        "/* Generated by scripts/gen_host_media.py - host-only sample media for",
        " * the Files viewer test tree. Real decodable PNG/JPEG/BMP plus real",
        " * metadata fixtures. Included only by the host branch of vqeaf_files.c;",
        " * the device build never compiles these bytes. Do not edit by hand. */",
        "#ifndef VQEAF_HOST_MEDIA_H",
        "#define VQEAF_HOST_MEDIA_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
    ]
    for name, data in media:
        parts.append(c_array("host_media_" + name.replace(".", "_"), data))
        parts.append("")
    parts.append("typedef struct {")
    parts.append("    const char *path;")
    parts.append("    const uint8_t *data;")
    parts.append("    size_t len;")
    parts.append("} host_media_t;")
    parts.append("")
    parts.append("static const host_media_t host_media[] = {")
    for name, data in media:
        parts.append('    { "/media/%s", host_media_%s, sizeof(host_media_%s) },'
                     % (name, name.replace(".", "_"), name.replace(".", "_")))
    parts.append('    { "/media/sub/readme.txt", host_media_note_txt, sizeof(host_media_note_txt) },')
    parts.append("};")
    parts.append("")
    parts.append("#endif /* VQEAF_HOST_MEDIA_H */")
    parts.append("")
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(parts))
    total = sum(len(d) for _, d in media)
    print("gen_host_media: wrote %s (%d files, %d bytes)" % (OUT, len(media), total))

    # Independent expected-frame dumps for the shell video test: decode the
    # same frames here and store XRGB8888 so the C test can memcmp them.
    fixdir = os.path.join(ROOT, "build", "media-fixtures")
    os.makedirs(fixdir, exist_ok=True)
    clip = dict(media)["clip.qvf"]
    width = int.from_bytes(clip[8:12], "little")
    height = int.from_bytes(clip[12:16], "little")
    count = int.from_bytes(clip[20:24], "little")
    offset = QVF_HEADER_SIZE
    raw_frames = []
    for _ in range(count):
        raw_size, comp_size = struct.unpack_from("<II", clip, offset)
        offset += QVF_FRAME_ENTRY
        comp = clip[offset:offset + comp_size]
        offset += comp_size
        raw_frames.append(comp)   # decompressed later via lz4 helper
    from mp4_to_qvf import lz4_decompress_check
    for index in (0, 3):
        if index >= count:
            continue
        raw_size, comp_size = struct.unpack_from("<II", clip, 32)
        # recompute per index properly
        offset = QVF_HEADER_SIZE
        for i in range(index + 1):
            raw_size, comp_size = struct.unpack_from("<II", clip, offset)
            offset += QVF_FRAME_ENTRY
            comp = clip[offset:offset + comp_size]
            offset += comp_size
        raw = lz4_decompress_check(comp, raw_size)
        dump = bytearray()
        for y in range(height):
            for x in range(width):
                c = raw[(y * width + x) * 2] | \
                    (raw[(y * width + x) * 2 + 1] << 8)
                r = ((c >> 11) & 0x1F) * 255 // 31
                g = ((c >> 5) & 0x3F) * 255 // 63
                b = (c & 0x1F) * 255 // 31
                dump += struct.pack("<I", 0xFF000000 | (r << 16) | (g << 8) | b)
        name = os.path.join(fixdir, "clip-expected-%d.raw" % index)
        open(name, "wb").write(dump)
        print("gen_host_media: wrote %s (%d bytes)" % (name, len(dump)))
    print("gen_host_media: clip.qvf %dx%d @%dfps, %d frames, %d bytes"
          % (width, height, int.from_bytes(clip[16:18], "little"), count,
             len(clip)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())