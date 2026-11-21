# VideoPlayer.qeapp — native QVF video player (framework ABI v3)

A native `.qeapp` that plays real video inside the OS: frames are streamed
from a QVF file (see `docs/QEAF_VIDEO_FORMAT.md`) with `read_file_part`,
decompressed with a built-in LZ4 block decoder, and rendered with
`blit_pixels` at 2x nearest-neighbour — both calls are the framework API v3
additions this app exists to exercise.

## Honest failure states

The player never fabricates video. With a missing or malformed file it
draws exactly what happened:

- `NO VIDEO FILE` — the QVF is not in app storage
- `UNSUPPORTED FORMAT` / `UNSUPPORTED VERSION` — not a QVF1 v1 container
- `CODEC NOT SUPPORTED` — unknown codec id
- `BAD QVF FLAGS` — nonzero flags
- `VIDEO TOO LARGE` — frame geometry/count outside the player's limits
- `BAD FRAME ENTRY` / `FRAME READ FAILED` / `FRAME DECODE FAILED`

## Keys

- OK / RIGHT: next frame
- LEFT: previous frame

## Layout

- `src/main.c` — the freestanding module (only exports the lifecycle)
- `tests/sim_driver.c` — host-simulator driver; seeds the real QVF, feeds
  OK OK LEFT, prints state/draws/blits/hash; `--fb out.raw` dumps the final
  framebuffer for pixel-exact cross-checks
- `tests/test_project.py` — manifest/module/package validation
- `compiler/qecompile.py` — thin wrapper over the shared SDK compiler

## Build and prove

```text
python sdk/cli/qeaf.py build examples/videoplayer-qeapp --config release
python scripts/mp4_to_qvf.py VIDEO.mp4 build/media-fixtures/video1.qvf
python scripts/test_videoplayer_native.py
python -m unittest discover -s examples/videoplayer-qeapp/tests -p test_project.py
```

The native test runs the arm64 structural proof and the x86_64 module in the
host simulator against a real QVF converted from an actual MP4 recording.
