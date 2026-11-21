#!/usr/bin/env python3
"""Drive the violet shell from the host and pull real SCREEN.RAW captures.

The rescue channel exposes three verbs this script relies on:

  * ``tap X Y``            - click at UI-canvas pixels (bound to vio_touch, so
                             the hit-test is the one a finger uses);
  * ``swipe X0 Y0 X1 Y1``  - press, walk to the end point over several polls,
                             release, so a scrollable page moves like a drag;
  * ``snap``               - write SCREEN.RAW, 240x520 RGB565 of the scanout.

It walks a page list and captures each one, so the render-vs-framebuffer
checklist in docs/UI_DEVICE_TEST.md needs no finger on the glass.

    python scripts/ui_capture_pages.py            # dry run, prints every step
    python scripts/ui_capture_pages.py --run      # actually talks to the phone

Two hardware behaviours this script has to absorb, both seen on violet:

  * **Orientation.** Taps are canvas pixels. After ``rotate 90`` the canvas is
    1560x720, every portrait coordinate below lands outside it, the device
    answers ``tap: refused (outside the canvas or UI down)`` and the capture
    silently shows the previous page. ``--run`` refuses to start unless
    ``status`` reports ``orientation=portrait``.
  * **Settling.** The UI consumes a queued tap, LVGL redraws, and only then is
    the scanout current. A single ``snap`` at 0.8 s returned the previous page.
    Each capture now repeats snap/read until two consecutive reads are
    byte-identical (``--tries``), which is also the proof that the frame is no
    longer moving.

``--run`` needs QeafdropOs (``QEAFDROPOS``, default
``D:\\QeafivelsOfficial\\QeafdropOs``) with its ``send_rescue_cmd.py`` and
``read_rescue_raw.py``, and the rescue LUN mounted (``--volume``, default F:).

Coordinates are for the 720x1560 portrait canvas. The floating dock now holds
Phone/Messages/Browser/Gallery, so navigation uses the home bar, the status
bar and horizontal swipes instead of dock taps: swipe up on lock, tap the home
bar at (360,1540), tap "Tat ca" at (654,346), tap the status bar at (100,30)
for Control Center, then swipe left from (600,1100) to (120,1100) for Settings.
Settings rows are 124 px tall with 22 px gaps; the first row starts at y=256,
so "Wi-Fi" (1st) is at y=318 and "Luu tru" (6th) at y=1047. Two 728 px drags
reach the scroll limit; then the last rows are Lock theme ~y=921, Sync
~y=1067 and About ~y=1213.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "build" / "screens" / "device"
SWIPE_STEPS = 10


def tap(x, y):
    return ("tap", x, y)


def swipe(x0, y0, x1, y1):
    return ("swipe", x0, y0, x1, y1)


# Ordered steps from a fresh boot. "lock" needs none: it is what the shell
# shows after boot, so it must be captured first (or after a reboot).
UNLOCK = [swipe(360, 1250, 360, 250)]
SETTINGS_STEPS = [swipe(360, 1250, 360, 250), tap(100, 30),
                  swipe(600, 1100, 120, 1100)]
SCROLL_BOTTOM = [swipe(360, 1200, 360, 300), swipe(360, 1200, 360, 300)]

PAGES = [
    ("lock", []),
    ("home", UNLOCK),
    ("apps", UNLOCK + [tap(654, 346)]),
    ("control", UNLOCK + [tap(100, 30)]),
    ("settings", SETTINGS_STEPS),
    ("storage", SETTINGS_STEPS + [tap(360, 1047)]),
    ("wifi", SETTINGS_STEPS + [tap(360, 318)]),
    ("drivers", SETTINGS_STEPS + [tap(360, 1330)]),
    ("sync", SETTINGS_STEPS + SCROLL_BOTTOM + [tap(360, 1067)]),
    ("lock-theme", SETTINGS_STEPS + SCROLL_BOTTOM + [tap(360, 921)]),
    ("files", UNLOCK + [tap(654, 346), tap(101, 675)]),
    ("about", SETTINGS_STEPS + SCROLL_BOTTOM + [tap(360, 1213)]),
]
RAW_BYTES = 240 * 520 * 2


def qeafdropos() -> Path:
    return Path(os.environ.get("QEAFDROPOS", r"D:\QeafivelsOfficial\QeafdropOs"))


def tool(path: Path) -> Path:
    if path.is_file():
        return path
    raise SystemExit("thieu cong cu QeafdropOs: %s" % path)


def run(argv) -> subprocess.CompletedProcess:
    printable = " ".join(str(a) if " " not in str(a) else '"%s"' % a for a in argv)
    print("  +", printable)
    return subprocess.run([str(a) for a in argv], capture_output=True, text=True)


def command(op):
    if op[0] == "tap":
        return "tap %d %d" % (op[1], op[2])
    return "swipe %d %d %d %d %d" % (op[1], op[2], op[3], op[4], SWIPE_STEPS)


def read_screen(send, read, args):
    """One snap + raw read, or (None, reason)."""
    proc = run([sys.executable, send, "--volume", args.volume, "--cmd", "snap"])
    if proc.returncode != 0 or "snap: wrote SCREEN.RAW" not in proc.stdout:
        return None, "snap that bai: %s%s" % (proc.stdout.strip(),
                                              proc.stderr.strip())
    time.sleep(max(args.wait / 3.0, 1.0))
    with tempfile.TemporaryDirectory(prefix="qeaf-raw-") as td:
        proc = run([sys.executable, read, "--drive", args.volume,
                    "--out", td, "--files", "SCREEN.RAW"])
        if proc.returncode != 0:
            return None, "doc SCREEN.RAW that bai: %s" % proc.stderr.strip()
        found = sorted(Path(td).rglob("SCREEN.RAW"))
        if not found:
            return None, "khong thay SCREEN.RAW trong thu muc doc ve"
        return found[0].read_bytes(), None


def settled_screen(send, read, args):
    """Snapshot until two consecutive raw reads are identical."""
    previous = None
    for attempt in range(1, args.tries + 1):
        data, why = read_screen(send, read, args)
        if data is None:
            return None, why
        if data == previous:
            print("    khung on dinh sau %d lan snap" % attempt)
            return data, None
        previous = data
    print("    canh bao: khung chua dung sau %d lan snap, dung lan cuoi"
          % args.tries)
    return previous, None


def capture(page: str, steps, args) -> bool:
    send = tool(qeafdropos() / "scripts" / "send_rescue_cmd.py")
    read = tool(qeafdropos() / "scripts" / "read_rescue_raw.py")
    for op in steps:
        proc = run([sys.executable, send, "--volume", args.volume,
                    "--cmd", command(op)])
        answer = proc.stdout.strip()
        if proc.returncode != 0:
            print("    %s that bai (exit %d): %s"
                  % (op[0], proc.returncode, proc.stderr.strip()))
            return False
        if answer:
            print("    " + answer)
        if "unavailable" in answer or "refused" in answer:
            print("    %s khong duoc ap dung; dung lai de kiem tra toa do,"
                  " orientation va trang thai UI" % op[0])
            return False
        time.sleep(args.wait)

    data, why = settled_screen(send, read, args)
    if data is None:
        print("    " + why)
        return False
    if len(data) != RAW_BYTES:
        print("    %s: %d byte, mong doi %d" % (page, len(data), RAW_BYTES))
        return False

    args.out.mkdir(parents=True, exist_ok=True)
    target = args.out / (page + ".raw")
    target.write_bytes(data)
    print("    -> %s (%d byte)" % (target, len(data)))
    return True


def describe(op):
    if op[0] == "tap":
        return "tap %d %d" % (op[1], op[2])
    return "swipe %d %d -> %d %d (%d buoc)" % (op[1], op[2], op[3], op[4],
                                               SWIPE_STEPS)


def main() -> int:
    default_pages = ",".join(name for name, _ in PAGES)
    parser = argparse.ArgumentParser(
        description="Walk the violet shell and capture SCREEN.RAW per page.")
    parser.add_argument("--run", action="store_true",
                        help="actually send commands (default: dry run)")
    parser.add_argument("--pages", default=default_pages,
                        help="comma separated page list, in capture order")
    parser.add_argument("--volume", default="F:", help="rescue LUN drive")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT,
                        help="where to keep <page>.raw")
    parser.add_argument("--wait", type=float, default=3.0,
                        help="seconds after each gesture (default 3)")
    parser.add_argument("--tries", type=int, default=5,
                        help="snap attempts until two frames agree (default 5)")
    args = parser.parse_args()
    args.out = args.out if args.out.is_absolute() else (ROOT / args.out)

    wanted = [p.strip() for p in args.pages.split(",") if p.strip()]
    known = dict(PAGES)
    unknown = [p for p in wanted if p not in known]
    if unknown:
        print("trang khong ho tro tu dong: " + ", ".join(unknown))
        print("co: " + ", ".join(name for name, _ in PAGES))
        return 2
    if "lock" in wanted and wanted[0] != "lock":
        print("canh bao: lock phai chup dau tien sau khi boot, khong phai o vi tri nay")

    plans = [(p, known[p]) for p in wanted]
    if not args.run:
        print("dry run (them --run de gui len may)")
        for page, steps in plans:
            print("%s:" % page)
            for op in steps:
                print("  " + describe(op))
            print("  snap (lap toi khi on dinh)")
            print("  read SCREEN.RAW -> %s" % (args.out / (page + ".raw")))
        print("\n%d trang. Chay xong thi so sanh:" % len(plans))
        for page, _ in plans:
            print("  python scripts/ui_snapshot_compare.py --raw %s --page %s"
                  % (args.out / (page + ".raw"), page))
        return 0

    # A tap is in canvas pixels; in landscape the canvas is 1560x720 and the
    # portrait coordinates below fall outside it, so the device refuses them
    # and the capture shows the previous page.
    send = tool(qeafdropos() / "scripts" / "send_rescue_cmd.py")
    proc = run([sys.executable, send, "--volume", args.volume, "--cmd", "status"])
    if "orientation=landscape" in proc.stdout:
        print("may dang o landscape: chay `rotate 0` truoc khi chup")
        return 2

    ok = True
    for page, steps in plans:
        print("%s:" % page)
        if not capture(page, steps, args):
            ok = False
            break
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
