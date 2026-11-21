# QVF — QEAFIVELS Video Format v1

QVF is the only video container QEAFIVELS OS decodes. The OS ships no
H.264/HEVC decoder (and will not: a full video codec stack does not fit the
freestanding kernel scope), so real MP4 files are transcoded on the build
machine into QVF with `scripts/mp4_to_qvf.py`, and the result is played by
the OS: natively by a `.qeapp` media app (framework API v3) or as a frame
preview in the shell Video page.

## Design goals

- Decodable by a freestanding C app in a few hundred lines (LZ4 block +
  RGB565 is the whole codec surface).
- Streamable frame by frame: a player never needs the whole file in memory
  (the QEAPP memory quota is 1 MiB by default).
- Honest failures: bad magic, unknown codec version, bad flags and bad frame
  entries are all detectable before any pixel is drawn.

## Container layout

All integers are little-endian. Header is exactly 32 bytes:

```text
offset  size  field
0       4     magic "QVF1"
4       2     version          = 1
6       2     codec            = 1 (LZ4 block of RGB565le pixels)
8       4     width            (pixels, > 0)
12      4     height           (pixels, > 0)
16      2     fps              (1..30)
18      2     reserved         (0)
20      4     frame_count      (> 0)
24      4     flags            (0; nonzero = reject)
28      4     reserved         (0)
```

Frames follow immediately, each as:

```text
u32 raw_size      must equal width * height * 2
u32 comp_size     LZ4 block length, > 0
u8  comp[comp_size]
```

Decompressing `comp` with the LZ4 block format must yield exactly
`raw_size` bytes of RGB565 (little-endian, `RRRRRGGG GGGBBBBB`).

## Rules a decoder must enforce

1. Magic is `QVF1`; version is 1; codec is 1; flags are 0. Anything else is
   a terminal, user-visible failure (`UNSUPPORTED FORMAT`, `CODEC NOT
   SUPPORTED`, `BAD QVF FLAGS`).
2. Frame dimensions must fit the player (the reference players accept up to
   240x426; the source aspect is usually 9:16).
3. Every frame entry's `raw_size` must equal `width * height * 2`; a
   mismatch is `BAD FRAME ENTRY`.
4. Frame reads that fail or decompress to the wrong length are
   `FRAME READ FAILED` / `FRAME DECODE FAILED`. Never substitute a
   placeholder picture.
5. Decoders must support overlapping LZ4 matches (offset < 4 repeats
   byte-by-byte), which RGB565 frames produce routinely.

## Producer

`scripts/mp4_to_qvf.py` (self-contained LZ4 compressor, ffmpeg via
imageio-ffmpeg for decoding):

```text
python scripts/mp4_to_qvf.py INPUT.mp4 OUTPUT.qvf \
    --width 120 --height 213 --fps 10 --seconds 8
```

Every frame is round-trip self-checked against the pure-python LZ4
decompressor, and (when the pip `lz4` package is importable) verified
against the reference implementation as well.

## Consumers

- `examples/videoplayer-qeapp` — native `.qeapp` (framework ABI v3):
  streams frame entries with `read_file_part`, decodes LZ4, renders with
  `blit_pixels` at 2x. Validated in the host simulator with a real video
  converted from `video1.mp4`.
- `platform/violet/ui/vqeaf_video.c` — shell Video page: scans a directory,
  parses QVF headers, decodes single frames for the preview, and reports
  MP4 files honestly (`codec H.264/AAC: OS chưa hỗ trợ`).
